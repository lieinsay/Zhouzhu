import { mkdir, readFile, readdir, realpath, open, rename, unlink } from 'node:fs/promises';
import { resolve, relative, isAbsolute, basename } from 'node:path';
import { randomUUID, createHash } from 'node:crypto';

const MAX_OBJ_BYTES = 5 * 1024 * 1024;
const MAX_VERTICES = 100000;
const MAX_TRIANGLES = 100000;
const ID = /^[a-zA-Z0-9][a-zA-Z0-9_-]{0,127}$/;
const MODEL_ID = /^[a-zA-Z0-9][a-zA-Z0-9_:-]{0,127}$/;
const UUID = /^[a-f0-9]{8}-[a-f0-9]{4}-[a-f0-9]{4}-[a-f0-9]{4}-[a-f0-9]{12}$/;
const object = value => value !== null && typeof value === 'object' && !Array.isArray(value);
const inside = (root, path) => { const rel = relative(root, path); return rel === '' || (!rel.startsWith('..') && !isAbsolute(rel)); };
const fail = (message, status = 422) => { const error = new Error(message); error.status = status; throw error; };

async function readJson(path, fallback) {
  try { return JSON.parse(await readFile(path, 'utf8')); }
  catch (error) { if (error.code === 'ENOENT') return fallback; throw new Error(`媒体库 ${basename(path)} 无法读取：${error.message}`); }
}
async function atomicJson(path, data) {
  await mkdir(resolve(path, '..'), { recursive: true });
  const temporary = `${path}.${randomUUID()}.tmp`;
  try {
    const handle = await open(temporary, 'wx');
    try { await handle.writeFile(JSON.stringify(data, null, 2) + '\n'); await handle.sync(); } finally { await handle.close(); }
    await rename(temporary, path);
  } catch (error) { await unlink(temporary).catch(() => {}); throw error; }
}

/** Geometry only: material libraries/textures are never read or resolved. */
export function parseObj(content) {
  if (typeof content !== 'string' || Buffer.byteLength(content, 'utf8') > MAX_OBJ_BYTES) fail('OBJ 内容必须是最多 5 MiB 的文本');
  if (content.includes('\0')) fail('OBJ 含有非法空字节');
  const vertices = [], normals = [], texcoords = [], faces = [];
  let triangleCount = 0;
  let polygonChecks = 0;
  const number = (token, line) => {
    const value = Number(token);
    if (token === undefined || token.trim() === '' || !Number.isFinite(value) || Math.abs(value) > 1000000) fail(`OBJ 第 ${line} 行坐标不是允许范围内的有限数值`);
    return value;
  };
  const index = (token, count, line, type) => {
    if (!/^-?\d+$/.test(token ?? '')) fail(`OBJ 第 ${line} 行 ${type} 索引格式错误`);
    const value = Number(token);
    const result = value > 0 ? value - 1 : count + value;
    if (!Number.isSafeInteger(value) || value === 0 || result < 0 || result >= count) fail(`OBJ 第 ${line} 行 ${type} 索引越界`);
    return result;
  };
  for (const [offset, raw] of content.split(/\r?\n/).entries()) {
    const line = offset + 1;
    const text = raw.replace(/#.*$/, '').trim();
    if (!text) continue;
    const [op, ...tokens] = text.split(/\s+/);
    if (op === 'v' || op === 'vn') {
      if (tokens.length < 3 || tokens.length > (op === 'v' ? 7 : 3)) fail(`OBJ 第 ${line} 行 ${op} 需要三维坐标`);
      const vector = tokens.slice(0, 3).map(token => number(token, line));
      // Homogeneous and optional vertex colors are accepted; imported material stays neutral.
      for (const token of tokens.slice(3)) number(token, line);
      if (op === 'v' && tokens.length === 4 && number(tokens[3], line) !== 1) fail(`OBJ 第 ${line} 行暂不支持非 1 的齐次坐标`);
      (op === 'v' ? vertices : normals).push(vector);
      if (vertices.length > MAX_VERTICES || normals.length > MAX_VERTICES) fail('OBJ 顶点/法线数超过 100000');
    } else if (op === 'vt') {
      if (tokens.length < 1 || tokens.length > 3) fail(`OBJ 第 ${line} 行纹理坐标格式错误`);
      texcoords.push(tokens.map(token => number(token, line)));
      if (texcoords.length > MAX_VERTICES) fail('OBJ 纹理坐标数超过 100000');
    } else if (op === 'f') {
      if (tokens.length < 3 || tokens.length > 1024) fail(`OBJ 第 ${line} 行面需含 3～1024 个顶点`);
      const corners = tokens.map(token => {
        const parts = token.split('/');
        if (parts.length > 3) fail(`OBJ 第 ${line} 行面索引格式错误`);
        const vertex = index(parts[0], vertices.length, line, '顶点');
        if (parts[1]) index(parts[1], texcoords.length, line, '纹理');
        const normal = parts[2] ? index(parts[2], normals.length, line, '法线') : null;
        return { vertex, normal };
      });
      if (corners.length > 3) {
        polygonChecks += corners.length * corners.length;
        if (polygonChecks > 5000000) fail('OBJ 多边形复杂度过高，请先在建模软件中三角化再上传');
        const points = corners.map(corner => vertices[corner.vertex]);
        const anchor = points[0];
        let planeNormal;
        for (let i = 1; i < points.length - 1 && !planeNormal; i++) {
          const a = points[i].map((v, axis) => v - anchor[axis]);
          const b = points[i + 1].map((v, axis) => v - anchor[axis]);
          const candidate = [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]];
          const length = Math.hypot(...candidate);
          if (length > 1e-12) planeNormal = candidate.map(value => value / length);
        }
        if (!planeNormal) fail(`OBJ 第 ${line} 行多边形面积为零`);
        const span = Math.max(...points.map(point => Math.hypot(...point.map((value, axis) => value - anchor[axis]))));
        const sideTolerance = Math.max(1e-12, span * span * 1e-8);
        for (const point of points) {
          const distance = point.reduce((sum, value, axis) => sum + (value - anchor[axis]) * planeNormal[axis], 0);
          if (Math.abs(distance) > Math.max(1e-8, span * 1e-5)) fail(`OBJ 第 ${line} 行多边形不共面，请先在建模软件中三角化再上传`);
        }
        // Every point must lie on the same inside half-plane of every edge.
        // This also rejects self-intersecting polygons rather than silently filling them.
        for (let i = 0; i < points.length; i++) {
          const start = points[i], end = points[(i + 1) % points.length];
          const edge = end.map((value, axis) => value - start[axis]);
          for (const point of points) {
            const delta = point.map((value, axis) => value - start[axis]);
            const cross = [edge[1] * delta[2] - edge[2] * delta[1], edge[2] * delta[0] - edge[0] * delta[2], edge[0] * delta[1] - edge[1] * delta[0]];
            if (cross.reduce((sum, value, axis) => sum + value * planeNormal[axis], 0) < -sideTolerance) fail(`OBJ 第 ${line} 行是凹面或自交多边形，请先在建模软件中三角化再上传`);
          }
        }
      }
      triangleCount += corners.length - 2;
      if (triangleCount > MAX_TRIANGLES) fail('OBJ 三角面数超过 100000');
      for (let i = 1; i < corners.length - 1; i++) faces.push([corners[0], corners[i], corners[i + 1]]);
    } else if (!['o', 'g', 's', 'usemtl', 'mtllib'].includes(op)) fail(`OBJ 第 ${line} 行暂不支持指令 ${op}，仅支持多边形几何`);
  }
  if (!vertices.length || !faces.length) fail('OBJ 至少需要三个顶点和一个面');
  const positions = [], outputNormals = [], colors = [], indices = [];
  const bounds = { min: [Infinity, Infinity, Infinity], max: [-Infinity, -Infinity, -Infinity] };
  for (const face of faces) {
    const [a, b, c] = face.map(corner => vertices[corner.vertex]);
    const ab = b.map((v, i) => v - a[i]), ac = c.map((v, i) => v - a[i]);
    const cross = [ab[1] * ac[2] - ab[2] * ac[1], ab[2] * ac[0] - ab[0] * ac[2], ab[0] * ac[1] - ab[1] * ac[0]];
    const length = Math.hypot(...cross);
    if (length <= 1e-12) continue;
    const flatNormal = cross.map(v => v / length);
    for (const corner of face) {
      const point = vertices[corner.vertex];
      for (let i = 0; i < 3; i++) { bounds.min[i] = Math.min(bounds.min[i], point[i]); bounds.max[i] = Math.max(bounds.max[i], point[i]); }
      const given = corner.normal === null ? flatNormal : normals[corner.normal];
      const normalLength = Math.hypot(...given);
      if (normalLength <= 1e-12) fail('OBJ 含有零长度法线');
      positions.push(...point); outputNormals.push(...given.map(v => v / normalLength)); colors.push(0.62, 0.55, 0.43, 1); indices.push(indices.length);
    }
  }
  if (!indices.length) fail('OBJ 不包含有效面积的三角面');
  return { schemaVersion: 1, winding: 'ccw', units: 'm', parts: [{ positions, normals: outputNormals, color: [0.62, 0.55, 0.43], colors, indices }], bounds, triangleCount: indices.length / 3 };
}

export class MediaLibrary {
  constructor({ publicDirectory, workspaceDirectory }) {
    this.publicDirectory = resolve(publicDirectory);
    this.directory = resolve(workspaceDirectory, 'assets');
    this.file = resolve(this.directory, 'models.json');
    this.queue = Promise.resolve();
  }
  async getAssets(project) {
    const thumbnailDirectory = resolve(this.publicDirectory, 'assets/thumbnails');
    const files = await readdir(thumbnailDirectory).catch(error => { if (error.code === 'ENOENT') return []; throw error; });
    const thumbnailManifest = await readJson(resolve(thumbnailDirectory, 'manifest.json'), null);
    const rows = Array.isArray(thumbnailManifest) ? thumbnailManifest : thumbnailManifest?.thumbnails;
    if (thumbnailManifest && !Array.isArray(rows)) fail('缩略图 manifest 必须包含 thumbnails 数组', 500);
    const thumbnails = [];
    const available = new Set(files);
    if (rows) {
      for (const row of rows) {
        if (!object(row) || !ID.test(row.id ?? '') || !ID.test(row.itemId ?? '')) fail('缩略图 manifest 的 ID 格式错误', 500);
        const filename = row.filename ?? (typeof row.url === 'string' && row.url.startsWith('/assets/thumbnails/') ? row.url.slice('/assets/thumbnails/'.length) : `${row.id}-v1.png`);
        if (!/^[a-zA-Z0-9][a-zA-Z0-9_.-]*\.png$/.test(filename)) fail('缩略图 manifest 文件名必须是库内 PNG', 500);
        if (available.has(filename)) thumbnails.push({ ...row, filename, url: `/assets/thumbnails/${filename}`, origin: 'imagegen' });
      }
    } else {
      for (const item of project.items) {
        const filename = available.has(`${item.id}-v1.png`) ? `${item.id}-v1.png` : available.has(`${item.id}.png`) ? `${item.id}.png` : null;
        if (filename) thumbnails.push({ id: item.id, itemId: item.id, name: item.name, filename, url: `/assets/thumbnails/${filename}`, origin: 'imagegen' });
      }
    }
    const modelManifest = await readJson(resolve(this.publicDirectory, 'assets/models/manifest.json'), { models: [], missingItems: [] });
    if (!object(modelManifest) || !Array.isArray(modelManifest.models)) fail('模型 manifest 必须包含 models 数组', 500);
    const models = modelManifest.models.map(row => {
      if (!object(row) || !MODEL_ID.test(row.id ?? '') || row.format !== 'mesh-json' || typeof row.url !== 'string' || !/^\/assets\/models\/[a-zA-Z0-9][a-zA-Z0-9_.-]*\.json$/.test(row.url)) fail('模型 manifest 的 ID、格式或库内 URL 错误', 500);
      return { ...row };
    });
    const uploaded = await this.readUploaded();
    models.push(...uploaded.models);
    for (const [kind, values] of [['缩略图', thumbnails], ['模型', models]]) if (new Set(values.map(row => row.id)).size !== values.length) fail(`${kind} manifest 存在重复 ID`, 500);
    const thumbnailsByItem = new Set(thumbnails.map(row => row.itemId));
    return { thumbnails, models, missingItems: modelManifest.missingItems ?? [], missingThumbnails: project.items.filter(row => !thumbnailsByItem.has(row.id)).map(row => ({ id: row.id, name: row.name })) };
  }
  async readUploaded() {
    const data = await readJson(this.file, { format: 'zhouzhu-model-library/v1', models: [] });
    if (data.format !== 'zhouzhu-model-library/v1' || !Array.isArray(data.models) || data.models.some(row => !object(row) || !UUID.test(row.id ?? '') || row.url !== `/api/models/${row.id}` || row.kind !== 'imported' || row.format !== 'mesh-json')) fail('上传模型库清单损坏，已保留原文件', 500);
    return data;
  }
  upload(data) {
    const operation = this.queue.then(async () => {
      if (!object(data) || Object.keys(data).some(key => !['name', 'filename', 'content'].includes(key)) || typeof data.name !== 'string' || !data.name.trim() || data.name.length > 120 || typeof data.filename !== 'string' || !/\.obj$/i.test(data.filename) || data.filename.length > 255 || /[\\/\0]/.test(data.filename)) fail('上传格式为 {name, filename: "模型.obj", content: "OBJ 文本"}', 400);
      const mesh = parseObj(data.content);
      const id = randomUUID();
      const model = { id, name: data.name.trim(), filename: data.filename, url: `/api/models/${id}`, format: 'mesh-json', kind: 'imported', usage: 'custom', itemIds: [], triangleCount: mesh.triangleCount, uploadedAt: new Date().toISOString(), source: { filename: data.filename, sha256: createHash('sha256').update(data.content).digest('hex') }, notes: ['仅导入 OBJ 三角面或凸共面多边形几何；凹面需先三角化；MTL、贴图和动画未导入。'] };
      const library = await this.readUploaded();
      const path = resolve(this.directory, `${id}.json`);
      await atomicJson(path, { ...mesh, id, name: model.name });
      try { await atomicJson(this.file, { ...library, models: [...library.models, model] }); }
      catch (error) { await unlink(path).catch(() => {}); throw error; }
      return model;
    });
    this.queue = operation.catch(() => {});
    return operation;
  }
  async readModel(id) {
    if (!UUID.test(id)) fail('上传模型 ID 不存在', 404);
    const library = await this.readUploaded();
    if (!library.models.some(row => row.id === id)) fail('上传模型不存在', 404);
    const path = await realpath(resolve(this.directory, `${id}.json`)).catch(error => { if (error.code === 'ENOENT') fail('上传模型文件缺失', 404); throw error; });
    if (!inside(await realpath(this.directory), path)) fail('不允许访问媒体库外的文件', 403);
    return JSON.parse(await readFile(path, 'utf8'));
  }
}
