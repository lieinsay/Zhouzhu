// 合并内置生图的原图与提示词，仅检查和编目，不编辑或缩放图像。
import { readFile, readdir, writeFile } from 'node:fs/promises';
import { resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
import { createHash } from 'node:crypto';
import { inflateSync } from 'node:zlib';
import { loadCatalog } from '../lib/catalog.mjs';

const sourceDirectory = dirname(fileURLToPath(import.meta.url));
const toolRoot = resolve(sourceDirectory, '..');
const repositoryRoot = resolve(toolRoot, '../..');

function inspectPng(bytes, id) {
  if (!bytes.subarray(0, 8).equals(Buffer.from([137,80,78,71,13,10,26,10]))) throw new Error(`${id} 不是 PNG`);
  const width = bytes.readUInt32BE(16), height = bytes.readUInt32BE(20);
  const depth = bytes[24], type = bytes[25];
  if (!width || !height || width > 4096 || height > 4096 || depth !== 8 || ![4,6].includes(type) || bytes[28] !== 0) throw new Error(`${id} 需要带 alpha 的非交错 8 位 PNG`);
  const channels = type === 6 ? 4 : 2;
  const chunks = [];
  for (let at = 8; at + 12 <= bytes.length;) {
    const length = bytes.readUInt32BE(at);
    if (at + 12 + length > bytes.length) throw new Error(`${id} PNG 数据截断`);
    const name = bytes.toString('ascii', at + 4, at + 8);
    if (name === 'IDAT') chunks.push(bytes.subarray(at + 8, at + 8 + length));
    at += length + 12;
    if (name === 'IEND') break;
  }
  const stride = width * channels + 1;
  const raw = inflateSync(Buffer.concat(chunks), {maxOutputLength: stride * height});
  if (raw.length !== stride * height) throw new Error(`${id} PNG 像素大小不符`);
  let previous = new Uint8Array(width), row = new Uint8Array(width);
  let transparentPixels = 0, visiblePixels = 0;
  for (let y = 0; y < height; y++) {
    const start = y * stride, filter = raw[start];
    if (filter > 4) throw new Error(`${id} PNG 过滤类型错误`);
    for (let x = 0; x < width; x++) {
      const left = x ? row[x - 1] : 0, up = previous[x], upperLeft = x ? previous[x - 1] : 0;
      let predicted = 0;
      if (filter === 1) predicted = left;
      if (filter === 2) predicted = up;
      if (filter === 3) predicted = Math.floor((left + up) / 2);
      if (filter === 4) {
        const p = left + up - upperLeft;
        const a = Math.abs(p - left), b = Math.abs(p - up), c = Math.abs(p - upperLeft);
        predicted = a <= b && a <= c ? left : b <= c ? up : upperLeft;
      }
      row[x] = (raw[start + 1 + x * channels + channels - 1] + predicted) & 255;
      if (row[x] === 0) transparentPixels++;
      else visiblePixels++;
    }
    [previous, row] = [row, previous];
  }
  if (!transparentPixels || !visiblePixels) throw new Error(`${id} 缺少透明背景或可见主体`);
  return {width, height, transparentPixels, visiblePixels};
}

const catalog = await loadCatalog(repositoryRoot);
const promptFiles = (await readdir(sourceDirectory)).filter(name => /^prompts-.*\.json$/.test(name));
const prompts = (await Promise.all(promptFiles.map(async file => JSON.parse(await readFile(resolve(sourceDirectory, file), 'utf8'))))).flat();
const byId = new Map();
for (const row of prompts) {
  if (!catalog.items.some(item => item.id === row.id) || byId.has(row.id) || typeof row.prompt !== 'string') throw new Error(`无效或重复提示词记录 ${row.id}`);
  byId.set(row.id, row);
}
const thumbnails = [];
for (const item of catalog.items) {
  const record = byId.get(item.id);
  if (!record) throw new Error(`尚缺 ${item.id} 生图记录`);
  const file = `public/assets/thumbnails/${item.id}-v1.png`;
  const bytes = await readFile(resolve(toolRoot, file));
  const sha256 = createHash('sha256').update(bytes).digest('hex');
  if (record.sha256.toLowerCase() !== sha256) throw new Error(`${item.id} 原图指纹与生成记录不同`);
  thumbnails.push({id:item.id, itemId:item.id, name:item.name, url:`/assets/thumbnails/${item.id}-v1.png`, ...inspectPng(bytes, item.id), sha256, origin:'imagegen', prompt:record.prompt});
}
const manifest = {schemaVersion:1, origin:'imagegen', notes:'每个现有道具分别使用内置生图生成，保留透明原图；仅供工具使用，不代表游戏网格。', thumbnails};
await writeFile(resolve(toolRoot, 'public/assets/thumbnails/manifest.json'), JSON.stringify(manifest, null, 2) + '\n');
console.log(`${thumbnails.length} 个道具缩略图：覆盖、指纹、尺寸、可见主体与透明背景检查通过。`);
