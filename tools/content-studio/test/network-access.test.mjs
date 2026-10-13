import test from 'node:test';
import assert from 'node:assert/strict';
import { mkdtemp, mkdir, copyFile, writeFile, rm } from 'node:fs/promises';
import { dirname, resolve, relative, isAbsolute } from 'node:path';
import { fileURLToPath } from 'node:url';
import { networkInterfaces, tmpdir } from 'node:os';
import { createServer, request } from 'node:http';
import { SOURCE_PATHS } from '../lib/catalog.mjs';
import { createStudioServer } from '../server.mjs';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '../../..');
const localAddresses = Object.values(networkInterfaces()).flat().filter(address => address?.family === 'IPv4' && !address.internal).map(address => address.address);
const networkAddress = localAddresses[0];
const foreignAddress = ['203.0.113.1', '198.51.100.1', '192.0.2.1'].find(address => !localAddresses.includes(address));

async function fixture(context) {
  const directory = await mkdtemp(resolve(tmpdir(), 'zhouzhu-content-studio-network-test-'));
  context.after(async () => {
    const rel = relative(tmpdir(), directory);
    assert(!rel.startsWith('..') && !isAbsolute(rel) && rel.startsWith('zhouzhu-content-studio-network-test-'));
    await rm(directory, { recursive: true, force: true });
  });
  const repositoryRoot = resolve(directory, 'repository');
  const workspaceDirectory = resolve(directory, 'workspace');
  const publicDirectory = resolve(directory, 'public');
  await mkdir(resolve(repositoryRoot, 'src'), { recursive: true });
  await mkdir(publicDirectory);
  await writeFile(resolve(publicDirectory, 'index.html'), '<!doctype html><title>工坊网络测试</title>');
  await Promise.all(SOURCE_PATHS.map(path => copyFile(resolve(ROOT, path), resolve(repositoryRoot, path))));
  return { repositoryRoot, workspaceDirectory, publicDirectory };
}

function send(url, path = '/', { method = 'GET', headers = {}, data } = {}) {
  return new Promise((resolveResponse, reject) => {
    const payload = data === undefined ? undefined : JSON.stringify(data);
    const req = request(`${url}${path}`, {
      method,
      headers: { ...(payload === undefined ? {} : { 'Content-Type': 'application/json' }), ...headers },
    }, res => {
      const chunks = [];
      res.on('data', chunk => chunks.push(chunk));
      res.on('end', () => resolveResponse({ status: res.statusCode, body: Buffer.concat(chunks).toString('utf8') }));
      res.on('error', reject);
    });
    req.setTimeout(5000, () => req.destroy(new Error('工坊网络测试请求超时')));
    req.on('error', reject);
    req.end(payload);
  });
}

function listen(server, port, address) {
  return new Promise((resolveListen, reject) => {
    server.once('error', reject);
    server.listen(port, address, () => {
      server.removeListener('error', reject);
      resolveListen();
    });
  });
}

function closeServer(server) {
  return new Promise((resolveClose, reject) => server.close(error => error ? reject(error) : resolveClose()));
}

test('默认仅回环绑定，未授权网络地址不能作为 Host', async context => {
  const studio = await createStudioServer(await fixture(context));
  const url = await studio.listen(0);
  context.after(() => studio.close());
  assert.deepEqual(studio.servers, [studio.server]);
  assert.equal(studio.server.address().address, '127.0.0.1');
  assert.equal((await send(url)).status, 200);
  const port = new URL(url).port;
  assert.equal((await send(url, '/', { headers: { Host: `localhost:${port}` } })).status, 200);
  assert.equal((await send(url, '/', { headers: { Host: `${networkAddress ?? '10.8.0.2'}:${port}` } })).status, 403);
});

test('指定本机非回环 IPv4 后双地址同端口访问，共用草稿且双方同源保存有效', { skip: !networkAddress && '本机没有非回环 IPv4' }, async context => {
  const studio = await createStudioServer({ ...await fixture(context), networkAddress });
  const loopbackUrl = await studio.listen(0);
  context.after(() => studio.close());
  const port = Number(new URL(loopbackUrl).port);
  const networkUrl = `http://${networkAddress}:${port}`;
  assert.equal(studio.servers.length, 2);
  assert.equal(studio.servers[0], studio.server);
  assert.deepEqual(studio.servers.map(server => [server.address().address, server.address().port]), [['127.0.0.1', port], [networkAddress, port]]);
  for (const url of [loopbackUrl, networkUrl]) {
    const response = await send(url);
    assert.equal(response.status, 200);
    assert.match(response.body, /工坊网络测试/);
  }
  const first = JSON.parse((await send(loopbackUrl, '/api/workspace')).body);
  first.project.items[0].name = '网络地址保存草稿';
  const saved = await send(networkUrl, '/api/workspace', { method: 'POST', headers: { Origin: networkUrl, 'Sec-Fetch-Site': 'same-origin' }, data: { project: first.project, revision: first.revision } });
  assert.equal(saved.status, 200);
  const networkSaved = JSON.parse(saved.body);
  const loopbackRead = JSON.parse((await send(loopbackUrl, '/api/workspace')).body);
  assert.equal(loopbackRead.project.items[0].name, '网络地址保存草稿');
  assert.equal(loopbackRead.revision, networkSaved.revision);
  loopbackRead.project.items[0].name = '回环地址保存草稿';
  const loopbackSaved = await send(loopbackUrl, '/api/workspace', { method: 'POST', headers: { Origin: loopbackUrl }, data: { project: loopbackRead.project, revision: loopbackRead.revision } });
  assert.equal(loopbackSaved.status, 200);
  const networkRead = JSON.parse((await send(networkUrl, '/api/workspace')).body);
  assert.equal(networkRead.project.items[0].name, '回环地址保存草稿');
  assert.equal(networkRead.revision, JSON.parse(loopbackSaved.body).revision);
});

test('网络入口继续拒绝陌生 Host、跨入口 Origin、跨站写入及目录穿越', { skip: !networkAddress && '本机没有非回环 IPv4' }, async context => {
  const studio = await createStudioServer({ ...await fixture(context), networkAddress });
  const loopbackUrl = await studio.listen(0);
  context.after(() => studio.close());
  const port = new URL(loopbackUrl).port;
  const networkUrl = `http://${networkAddress}:${port}`;
  const first = JSON.parse((await send(networkUrl, '/api/workspace')).body);
  const data = { project: first.project, revision: first.revision };
  for (const [url, otherUrl] of [[loopbackUrl, networkUrl], [networkUrl, loopbackUrl]]) {
    assert.equal((await send(url, '/', { headers: { Host: `evil.example:${port}` } })).status, 403);
    assert.equal((await send(url, '/', { headers: { Host: `${foreignAddress}:${port}` } })).status, 403);
    assert.equal((await send(url, '/%2e%2e%2fserver.mjs')).status, 403);
    for (const headers of [{ Origin: otherUrl }, { Origin: 'https://evil.example' }, { 'Sec-Fetch-Site': 'cross-site' }]) {
      const response = await send(url, '/api/workspace', { method: 'POST', headers, data });
      assert.equal(response.status, 403);
      assert.equal(JSON.parse(response.body).error, '只允许同源页面写入草稿');
    }
  }
  assert.equal(JSON.parse((await send(networkUrl, '/api/workspace')).body).revision, first.revision);
});

test('网络绑定拒绝全接口地址、外部地址、回环地址、IPv6 和主机名', async context => {
  const f = await fixture(context);
  for (const address of ['0.0.0.0', foreignAddress, '127.0.0.1', '::1', 'localhost']) {
    await assert.rejects(createStudioServer({ ...f, networkAddress: address }), `不允许绑定 ${address}`);
  }
});

test('close 关闭双入口并释放两地址上的相同端口', { skip: !networkAddress && '本机没有非回环 IPv4' }, async context => {
  const studio = await createStudioServer({ ...await fixture(context), networkAddress });
  let closed = false;
  context.after(() => closed ? undefined : studio.close());
  const url = await studio.listen(0);
  const port = Number(new URL(url).port);
  await studio.close();
  closed = true;
  assert(studio.servers.every(server => !server.listening));
  const reboundServers = [];
  context.after(async () => {
    for (const server of reboundServers) if (server.listening) await closeServer(server);
  });
  for (const address of ['127.0.0.1', networkAddress]) {
    const server = createServer();
    reboundServers.push(server);
    await listen(server, port, address);
  }
  assert(reboundServers.every(server => server.address().port === port));
});

test('允许本机网络入口所在子网的来源，同源页面能够读取和保存草稿', { skip: !networkAddress && '本机没有非回环 IPv4' }, async context => {
  const allowedNetwork = `${networkAddress.split('.').slice(0, 3).join('.')}.0/24`;
  const studio = await createStudioServer({ ...await fixture(context), networkAddress, allowedNetwork });
  const loopbackUrl = await studio.listen(0);
  context.after(() => studio.close());
  const networkUrl = `http://${networkAddress}:${new URL(loopbackUrl).port}`;
  assert.equal((await send(networkUrl)).status, 200);
  const read = await send(networkUrl, '/api/workspace');
  assert.equal(read.status, 200);
  const first = JSON.parse(read.body);
  first.project.items[0].name = '来源子网允许保存草稿';
  const saved = await send(networkUrl, '/api/workspace', { method: 'POST', headers: { Origin: networkUrl, 'Sec-Fetch-Site': 'same-origin' }, data: { project: first.project, revision: first.revision } });
  assert.equal(saved.status, 200);
  const restored = JSON.parse((await send(loopbackUrl, '/api/workspace')).body);
  assert.equal(restored.project.items[0].name, '来源子网允许保存草稿');
  assert.equal(restored.revision, JSON.parse(saved.body).revision);
});

test('来源不在指定子网时拒绝网络入口，回环入口仍能读取和保存', { skip: !networkAddress && '本机没有非回环 IPv4' }, async context => {
  const allowedNetwork = networkAddress.startsWith('203.0.113.') ? '198.51.100.0/24' : '203.0.113.0/24';
  const studio = await createStudioServer({ ...await fixture(context), networkAddress, allowedNetwork });
  const loopbackUrl = await studio.listen(0);
  context.after(() => studio.close());
  const networkUrl = `http://${networkAddress}:${new URL(loopbackUrl).port}`;
  assert.equal((await send(networkUrl)).status, 403);
  assert.equal((await send(networkUrl, '/api/workspace')).status, 403);
  assert.equal((await send(loopbackUrl)).status, 200);
  const read = await send(loopbackUrl, '/api/workspace');
  assert.equal(read.status, 200);
  const first = JSON.parse(read.body);
  first.project.items[0].name = '回环不受来源子网限制';
  const saved = await send(loopbackUrl, '/api/workspace', { method: 'POST', headers: { Origin: loopbackUrl }, data: { project: first.project, revision: first.revision } });
  assert.equal(saved.status, 200);
  assert.equal(JSON.parse(saved.body).project.items[0].name, '回环不受来源子网限制');
  assert.equal((await send(networkUrl, '/api/workspace', { method: 'POST', headers: { Origin: networkUrl }, data: { project: first.project, revision: JSON.parse(saved.body).revision } })).status, 403);
});

test('来源子网必须是合法 IPv4 CIDR，且必须显式指定网络监听地址', async context => {
  const f = await fixture(context);
  await assert.rejects(createStudioServer({ ...f, allowedNetwork: '10.8.0.0/24' }));
  for (const allowedNetwork of ['10.8.0.0', '10.8.0.0/33', '10.8.0.0/-1', '10.8.0.256/24', '::1/128', 'localhost/24']) {
    await assert.rejects(createStudioServer({ ...f, networkAddress, allowedNetwork }), `不允许来源子网 ${allowedNetwork}`);
  }
});
