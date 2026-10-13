import { createServer } from 'node:http';
import { readFile, realpath } from 'node:fs/promises';
import { dirname, extname, resolve, relative, isAbsolute } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { WorkspaceStore, StoreError } from './lib/store.mjs';
import { validateProject } from './lib/validation.mjs';
import { MediaLibrary } from './lib/media.mjs';

const TOOL_ROOT = dirname(fileURLToPath(import.meta.url));
const MAX_BODY_BYTES = 5 * 1024 * 1024;
const MIME = { '.html': 'text/html; charset=utf-8', '.css': 'text/css; charset=utf-8', '.js': 'text/javascript; charset=utf-8', '.mjs': 'text/javascript; charset=utf-8', '.json': 'application/json; charset=utf-8', '.svg': 'image/svg+xml', '.png': 'image/png', '.ico': 'image/x-icon' };
const inside = (root, path) => { const rel = relative(root, path); return rel === '' || (!rel.startsWith('..') && !isAbsolute(rel)); };

function reply(response, status, value, headers = {}) {
  response.writeHead(status, { 'Content-Type': 'application/json; charset=utf-8', 'Cache-Control': 'no-store', 'X-Content-Type-Options': 'nosniff', ...headers });
  response.end(typeof value === 'string' ? value : JSON.stringify(value));
}
async function body(request) {
  if (!/^application\/json(?:\s*;|$)/i.test(request.headers['content-type'] ?? '')) throw new StoreError(415, '写入必须使用 application/json');
  if (Number(request.headers['content-length']) > MAX_BODY_BYTES) throw new StoreError(413, 'JSON 请求超过 5 MiB');
  const chunks = [];
  let size = 0;
  for await (const chunk of request) {
    size += chunk.length;
    if (size > MAX_BODY_BYTES) throw new StoreError(413, 'JSON 请求超过 5 MiB');
    chunks.push(chunk);
  }
  try { return JSON.parse(Buffer.concat(chunks).toString('utf8')); } catch { throw new StoreError(400, 'JSON 请求格式不正确'); }
}
function checkRequestAccess(request, port) {
  const hosts = [`127.0.0.1:${port}`, `localhost:${port}`];
  if (!hosts.includes(request.headers.host)) throw new StoreError(403, 'Host 必须是本机开发工具地址');
  // 浏览器直接打开地址会标记 Sec-Fetch-Site:none，从其他页面点击链接则是
  // cross-site。GET/HEAD 不写草稿，应允许读取；写入仍须通过下面的同源检查。
  if (request.method === 'GET' || request.method === 'HEAD') return;
  const origin = request.headers.origin;
  if (origin && origin !== `http://${request.headers.host}`) throw new StoreError(403, '只允许同源页面写入草稿');
  if (!origin && request.headers['sec-fetch-site'] && request.headers['sec-fetch-site'] !== 'same-origin') throw new StoreError(403, '只允许同源页面写入草稿');
}

export function makeCppProposal(project, baseline) {
  const issues = validateProject(project, baseline);
  if (issues.some(row => row.severity === 'error')) throw new StoreError(422, '草稿仍有错误，请先修复后导出 C++ 建议片段');
  const quote = value => JSON.stringify(value);
  const float = value => `${Number.isInteger(value) ? `${value}.0` : value}f`;
  const items = project.items.map(row => `    { ${quote(row.id)}, ${quote(row.name)}, ${row.stack}, ${row.color.map(float).join(', ')} },`).join('\n');
  const recipes = project.recipes.map(row => `    { ${quote(row.id)}, ${quote(row.product)}, ${row.yield}, Station::${row.station}, { ${row.ingredients.map(input => `{ ${quote(input.item)}, ${input.count} }`).join(', ')} } },`).join('\n');
  return '// 开发者人工审阅建议，工具未改写游戏。\n// 仅覆盖道具与配方表；其他集合请按 JSON 变更报告单独实现。\n// 需核对源 SHA-256、既有 enum/行为和资产，再自行应用、构建、验证。\n\n'
    + `// src/crops.cpp · namespace catalog\nconst ItemDef ITEMS[] = {\n${items}\n};\n\n`
    + `// src/recipes.cpp · namespace recipes\nconst RecipeDef RECIPES[] = {\n${recipes}\n};\n`;
}

export async function createStudioServer({ repositoryRoot = resolve(TOOL_ROOT, '../..'), workspaceDirectory = resolve(TOOL_ROOT, 'workspace'), publicDirectory = resolve(TOOL_ROOT, 'public') } = {}) {
  const media = new MediaLibrary({ publicDirectory, workspaceDirectory });
  const store = await new WorkspaceStore({ repositoryRoot, workspaceDirectory, mediaLibrary: media }).init();
  const server = createServer(async (request, response) => {
    try {
      const port = server.address()?.port;
      checkRequestAccess(request, port);
      let pathname;
      try { pathname = decodeURIComponent(new URL(request.url, 'http://127.0.0.1').pathname); } catch { throw new StoreError(400, 'URL 格式不正确'); }
      if (pathname.includes('\\') || pathname.includes('\0') || pathname.split('/').includes('..')) throw new StoreError(403, '不允许访问目录外文件');
      if (pathname.startsWith('/api/')) {
        if (request.method === 'GET') {
          if (pathname.startsWith('/api/models/')) return reply(response, 200, await media.readModel(pathname.slice('/api/models/'.length)));
          const workspace = await store.getWorkspace();
          if (pathname === '/api/assets') return reply(response, 200, await media.getAssets(workspace.project));
          if (pathname === '/api/workspace') return reply(response, 200, workspace);
          if (pathname === '/api/export') return reply(response, 200, JSON.stringify(workspace.project, null, 2) + '\n', { 'Content-Disposition': 'attachment; filename="zhouzhu-content-draft.json"' });
          if (pathname === '/api/report') return reply(response, 200, { revision: workspace.revision, sources: workspace.baseline.meta.sources, issues: workspace.issues, changes: workspace.changes });
          if (pathname === '/api/patch') return reply(response, 200, makeCppProposal(workspace.project, workspace.baseline), { 'Content-Type': 'text/plain; charset=utf-8', 'Content-Disposition': 'attachment; filename="zhouzhu-items-recipes-proposal.cpp.txt"' });
        }
        if (request.method === 'POST' && pathname === '/api/models') return reply(response, 201, { model: await media.upload(await body(request)) });
        if (request.method === 'POST' && ['/api/workspace', '/api/import'].includes(pathname)) {
          const data = await body(request);
          if (!data || typeof data !== 'object' || Array.isArray(data) || Object.keys(data).some(key => key !== 'project' && key !== 'revision')) throw new StoreError(400, '写入格式为 {project, revision}');
          return reply(response, 200, await store.saveProject(data.project, data.revision));
        }
        if (request.method === 'POST' && pathname === '/api/reset') {
          const data = await body(request);
          if (!data || typeof data !== 'object' || Array.isArray(data) || Object.keys(data).some(key => key !== 'revision')) throw new StoreError(400, '重置格式为 {revision}');
          return reply(response, 200, await store.resetProject(data.revision));
        }
        throw new StoreError(404, 'API 路径或方法不存在');
      }
      if (request.method !== 'GET' && request.method !== 'HEAD') throw new StoreError(405, '静态资源仅支持读取');
      const validationPath = '/lib/validation.mjs';
      const file = pathname === validationPath ? resolve(TOOL_ROOT, 'lib/validation.mjs') : resolve(publicDirectory, `.${pathname === '/' ? '/index.html' : pathname}`);
      if (pathname !== validationPath && !inside(resolve(publicDirectory), file)) throw new StoreError(403, '不允许访问目录外文件');
      const real = await realpath(file).catch(error => { if (error.code === 'ENOENT') throw new StoreError(404, '静态文件不存在'); throw error; });
      const allowed = pathname === validationPath ? real === await realpath(resolve(TOOL_ROOT, 'lib/validation.mjs')) : inside(await realpath(publicDirectory), real);
      if (!allowed) throw new StoreError(403, '不允许通过链接访问目录外文件');
      const bytes = await readFile(real);
      const cacheControl = /^\/assets\/thumbnails\/[a-zA-Z0-9_-]+-v\d+\.png$/.test(pathname) ? 'private, max-age=3600' : 'no-store';
      response.writeHead(200, { 'Content-Type': MIME[extname(file)] ?? 'application/octet-stream', 'Cache-Control': cacheControl, 'X-Content-Type-Options': 'nosniff', 'Content-Security-Policy': "default-src 'self'; script-src 'self'; style-src 'self' 'unsafe-inline'; img-src 'self' data:; connect-src 'self'; object-src 'none'; base-uri 'none'; frame-ancestors 'none'" });
      response.end(request.method === 'HEAD' ? undefined : bytes);
    } catch (error) {
      if (response.headersSent) { response.destroy(); return; }
      reply(response, error.status ?? 500, { error: error.message });
    }
  });
  server.requestTimeout = 15000;
  server.headersTimeout = 10000;
  return { server, store, media, listen: (port = 4176) => new Promise((resolveListen, reject) => {
    server.once('error', reject);
    server.listen(port, '127.0.0.1', () => { server.removeListener('error', reject); resolveListen(`http://127.0.0.1:${server.address().port}`); });
  }) };
}

if (process.argv[1] && import.meta.url === pathToFileURL(resolve(process.argv[1])).href) {
  const args = process.argv.slice(2);
  const options = {};
  let port = 4176;
  for (let i = 0; i < args.length; i++) {
    const argument = args[i];
    if (argument === '--root' && args[i + 1]) options.repositoryRoot = resolve(args[++i]);
    else if (argument === '--workspace' && args[i + 1]) options.workspaceDirectory = resolve(args[++i]);
    else if (argument === '--port' && args[i + 1]) port = Number(args[++i]);
    else throw new Error(`未知参数 ${argument}；支持 --root、--workspace、--port`);
  }
  if (!Number.isInteger(port) || port < 1 || port > 65535) throw new Error('端口必须为 1～65535 的整数');
  try {
    const studio = await createStudioServer(options);
    const url = await studio.listen(port);
    console.log(`Zhouzhu 内容工坊：${url}\n草稿：${studio.store.file}\n源码只读，游戏不会加载此草稿。`);
    process.once('SIGINT', () => studio.server.close());
    process.once('SIGTERM', () => studio.server.close());
  } catch (error) { console.error(error.message); process.exitCode = 1; }
}
