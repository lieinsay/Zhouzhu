import { previewTransform } from './media.js';

const normalize = v => { const n = Math.hypot(...v) || 1; return v.map(x => x / n); };
const cross = (a, b) => [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]];
const sub = (a, b) => a.map((x, i) => x - b[i]);
const dot = (a, b) => a.reduce((sum, x, i) => sum + x * b[i], 0);

// Column-major matrices, Y-up. Config rotations are degrees around X, Y, Z.
export function modelMatrix(config) {
  const { scale, rotation, offset } = previewTransform(config);
  const [x, y, z] = rotation.map(degree => degree * Math.PI / 180);
  const [cx, sx, cy, sy, cz, sz] = [Math.cos(x), Math.sin(x), Math.cos(y), Math.sin(y), Math.cos(z), Math.sin(z)];
  return new Float32Array([
    cz * cy * scale[0], sz * cy * scale[0], -sy * scale[0], 0,
    (cz * sy * sx - sz * cx) * scale[1], (sz * sy * sx + cz * cx) * scale[1], cy * sx * scale[1], 0,
    (cz * sy * cx + sz * sx) * scale[2], (sz * sy * cx - cz * sx) * scale[2], cy * cx * scale[2], 0,
    ...offset, 1,
  ]);
}
export function transformPoint(matrix, position) {
  const [x, y, z] = position;
  return [matrix[0] * x + matrix[4] * y + matrix[8] * z + matrix[12], matrix[1] * x + matrix[5] * y + matrix[9] * z + matrix[13], matrix[2] * x + matrix[6] * y + matrix[10] * z + matrix[14]];
}

export function prepareMesh(mesh, config) {
  if (mesh?.schemaVersion !== 1 || !Array.isArray(mesh.parts)) throw new Error('模型文件格式不受支持。');
  const matrix = modelMatrix(config);
  const scale = previewTransform(config).scale;
  const vertices = [], lines = [];
  const min = [Infinity, Infinity, Infinity], max = [-Infinity, -Infinity, -Infinity];
  let triangles = 0;
  for (const part of mesh.parts) {
    const positions = part.positions, indices = part.indices;
    if (!Array.isArray(positions) || !Array.isArray(indices) || positions.length % 3 || indices.length % 3) throw new Error('模型顶点或三角形不完整。');
    if (!positions.every(Number.isFinite) || indices.some(i => !Number.isSafeInteger(i) || i < 0 || i * 3 >= positions.length)) throw new Error('模型顶点或索引无效。');
    triangles += indices.length / 3;
    if (triangles > 500000) throw new Error('模型超过预览的 500000 三角形上限。');
    const color = [0, 1, 2].map(i => Number.isFinite(part.color?.[i]) ? Math.max(0, Math.min(1, part.color[i])) : .65);
    const alpha = Number.isFinite(part.alpha) ? Math.max(0, Math.min(1, part.alpha)) : 1;
    const colors = part.colors;
    if (colors !== undefined && (!Array.isArray(colors) || colors.length !== positions.length / 3 * 4 || !colors.every(Number.isFinite))) throw new Error('模型顶点色无效。');
    const vertexColor = index => colors ? colors.slice(index * 4, index * 4 + 4).map(value => Math.max(0, Math.min(1, value))) : [...color, alpha];
    const normals = part.normals;
    const smooth = Array.isArray(normals) && normals.length === positions.length && normals.every(Number.isFinite);
    for (let i = 0; i < indices.length; i += 3) {
      const ids = indices.slice(i, i + 3);
      const points = ids.map(index => transformPoint(matrix, positions.slice(index * 3, index * 3 + 3)));
      const face = normalize(cross(sub(points[1], points[0]), sub(points[2], points[0])));
      for (let k = 0; k < 3; k++) {
        const p = points[k];
        let normal = face;
        if (smooth) {
          const source = normals.slice(ids[k] * 3, ids[k] * 3 + 3).map((value, axis) => value / (scale[axis] * scale[axis]));
          normal = normalize(transformPoint(matrix, source).map((value, axis) => value - matrix[12 + axis]));
        }
        vertices.push(...p, ...normal, ...vertexColor(ids[k]));
        for (let axis = 0; axis < 3; axis++) { min[axis] = Math.min(min[axis], p[axis]); max[axis] = Math.max(max[axis], p[axis]); }
      }
      for (const [a, b] of [[0, 1], [1, 2], [2, 0]]) lines.push(...points[a], ...face, ...vertexColor(ids[a]), ...points[b], ...face, ...vertexColor(ids[b]));
    }
  }
  if (!triangles) throw new Error('模型没有可显示的三角形。');
  const center = min.map((value, axis) => (value + max[axis]) / 2);
  const radius = Math.max(.05, Math.hypot(...sub(max, min)) / 2);
  return { vertices: new Float32Array(vertices), lines: new Float32Array(lines), bounds: { min, max, center, radius }, triangles };
}

function multiply(a, b) {
  const out = new Float32Array(16);
  for (let column = 0; column < 4; column++) for (let row = 0; row < 4; row++) for (let k = 0; k < 4; k++) out[column * 4 + row] += a[k * 4 + row] * b[column * 4 + k];
  return out;
}
function perspective(aspect, near, far) {
  const f = 1 / Math.tan(Math.PI / 8);
  return new Float32Array([f / aspect, 0, 0, 0, 0, f, 0, 0, 0, 0, (far + near) / (near - far), -1, 0, 0, 2 * far * near / (near - far), 0]);
}
function lookAt(eye, center) {
  const z = normalize(sub(eye, center)), x = normalize(cross([0, 1, 0], z)), y = cross(z, x);
  return new Float32Array([x[0], y[0], z[0], 0, x[1], y[1], z[1], 0, x[2], y[2], z[2], 0, -dot(x, eye), -dot(y, eye), -dot(z, eye), 1]);
}

export class ModelViewer {
  constructor(canvas, { onStatus = () => {} } = {}) {
    this.canvas = canvas;
    this.onStatus = onStatus;
    this.listeners = [];
    this.buffers = [];
    this.shaders = [];
    this.abort = new AbortController();
    this.gl = canvas.getContext('webgl', { antialias: true, alpha: false, preserveDrawingBuffer: false });
    if (!this.gl) throw new Error('浏览器未启用 WebGL，仍可配置模型并保存草稿。');
    const gl = this.gl;
    const shader = (type, source) => {
      const value = gl.createShader(type); gl.shaderSource(value, source); gl.compileShader(value);
      if (!gl.getShaderParameter(value, gl.COMPILE_STATUS)) { gl.deleteShader(value); this.dispose(); throw new Error('模型着色器编译失败。'); }
      this.shaders.push(value);
      return value;
    };
    const vertex = shader(gl.VERTEX_SHADER, 'attribute vec3 aPosition; attribute vec3 aNormal; attribute vec4 aColor; uniform mat4 uCamera; varying vec3 vNormal; varying vec4 vColor; void main(){gl_Position=uCamera*vec4(aPosition,1.0);vNormal=aNormal;vColor=aColor;}');
    const fragment = shader(gl.FRAGMENT_SHADER, 'precision mediump float; varying vec3 vNormal; varying vec4 vColor; uniform float uFlat; void main(){vec3 n=normalize(vNormal);float key=max(dot(n,normalize(vec3(-0.5,1.0,0.7))),0.0);float fill=max(dot(n,normalize(vec3(0.7,0.3,-0.5))),0.0);float light=mix(0.34+0.52*key+0.19*fill,1.0,uFlat);gl_FragColor=vec4(vColor.rgb*light,vColor.a);}');
    this.program = gl.createProgram(); gl.attachShader(this.program, vertex); gl.attachShader(this.program, fragment); gl.linkProgram(this.program); gl.deleteShader(vertex); gl.deleteShader(fragment);
    this.shaders = [];
    if (!gl.getProgramParameter(this.program, gl.LINK_STATUS)) { this.dispose(); throw new Error('模型预览程序初始化失败。'); }
    this.attributes = ['aPosition', 'aNormal', 'aColor'].map(name => gl.getAttribLocation(this.program, name));
    this.cameraUniform = gl.getUniformLocation(this.program, 'uCamera');
    this.flatUniform = gl.getUniformLocation(this.program, 'uFlat');
    this.vertexBuffer = this.buffer(); this.lineBuffer = this.buffer(); this.gridBuffer = this.buffer();
    this.listen(canvas, 'pointerdown', e => { this.drag = { x: e.clientX, y: e.clientY, id: e.pointerId }; canvas.setPointerCapture(e.pointerId); });
    this.listen(canvas, 'pointermove', e => { if (!this.drag || this.drag.id !== e.pointerId) return; this.yaw += (e.clientX - this.drag.x) * .009; this.pitch = Math.max(-1.35, Math.min(1.35, this.pitch + (e.clientY - this.drag.y) * .009)); this.drag.x = e.clientX; this.drag.y = e.clientY; this.schedule(); });
    const end = e => { if (this.drag?.id === e.pointerId) this.drag = null; };
    this.listen(canvas, 'pointerup', end); this.listen(canvas, 'pointercancel', end);
    this.listen(canvas, 'wheel', e => { if (!this.prepared) return; e.preventDefault(); this.distance = Math.max(this.prepared.bounds.radius * .25, Math.min(this.prepared.bounds.radius * 30, this.distance * Math.exp(e.deltaY * .001))); this.schedule(); }, { passive: false });
    this.listen(canvas, 'webglcontextlost', e => { e.preventDefault(); this.onStatus('WebGL 上下文已中断，请切换页签后重试。', true); });
    if (typeof ResizeObserver !== 'undefined') { this.resize = new ResizeObserver(() => this.schedule()); this.resize.observe(canvas); }
    else this.listen(window, 'resize', () => this.schedule());
  }
  listen(target, event, callback, options) { target.addEventListener(event, callback, options); this.listeners.push([target, event, callback, options]); }
  buffer() { const buffer = this.gl.createBuffer(); this.buffers.push(buffer); return buffer; }
  upload(buffer, data) { this.gl.bindBuffer(this.gl.ARRAY_BUFFER, buffer); this.gl.bufferData(this.gl.ARRAY_BUFFER, data, this.gl.STATIC_DRAW); }
  async load(url, transform) {
    this.onStatus('正在读取模型…');
    const response = await fetch(url, { signal: this.abort.signal });
    if (!response.ok) throw new Error('模型文件读取失败，请刷新资产库。');
    const mesh = await response.json();
    if (this.disposed) return;
    this.mesh = mesh; this.setTransform(transform); this.reset();
  }
  setTransform(transform) {
    if (!this.mesh || this.disposed) return;
    const zoom = this.prepared && this.distance ? this.distance / this.prepared.bounds.radius : 3.3;
    this.prepared = prepareMesh(this.mesh, transform);
    this.distance = this.prepared.bounds.radius * zoom;
    this.upload(this.vertexBuffer, this.prepared.vertices); this.upload(this.lineBuffer, this.prepared.lines);
    const bounds = this.prepared.bounds;
    const extent = Math.max(bounds.radius * 2, ...bounds.min.map(Math.abs), ...bounds.max.map(Math.abs), 1);
    const step = Math.pow(10, Math.floor(Math.log10(extent / 6)));
    const half = Math.ceil(extent / step) * step;
    const grid = [];
    for (let p = -half; p <= half + step * .01; p += step) {
      const color = Math.abs(p) < step * .01 ? [.35, .47, .46] : [.17, .23, .28];
      for (const position of [[p, 0, -half], [p, 0, half], [-half, 0, p], [half, 0, p]]) grid.push(...position, 0, 1, 0, ...color, 1);
    }
    this.gridCount = grid.length / 10; this.upload(this.gridBuffer, new Float32Array(grid));
    if (!this.distance) this.distance = bounds.radius * 3.3;
    this.onStatus(`${this.prepared.triangles.toLocaleString()} 个三角形 · 网格位于 Y = 0 · 拖动旋转 / 滚轮缩放`);
    this.schedule();
  }
  reset() { if (!this.prepared) return; this.yaw = .72; this.pitch = .45; this.distance = this.prepared.bounds.radius * 3.3; this.schedule(); }
  setWireframe(value) { this.wireframe = value; this.schedule(); }
  schedule() { if (this.disposed || this.frame) return; this.frame = requestAnimationFrame(() => { this.frame = 0; this.draw(); }); }
  bind(buffer) {
    const gl = this.gl; gl.bindBuffer(gl.ARRAY_BUFFER, buffer);
    for (let i = 0; i < 3; i++) { gl.enableVertexAttribArray(this.attributes[i]); gl.vertexAttribPointer(this.attributes[i], i === 2 ? 4 : 3, gl.FLOAT, false, 40, [0, 12, 24][i]); }
  }
  draw() {
    if (this.disposed || !this.prepared) return;
    const gl = this.gl, canvas = this.canvas, bounds = this.prepared.bounds;
    const ratio = Math.min(window.devicePixelRatio || 1, 2);
    const width = Math.max(1, Math.round(canvas.clientWidth * ratio)), height = Math.max(1, Math.round(canvas.clientHeight * ratio));
    if (canvas.width !== width || canvas.height !== height) { canvas.width = width; canvas.height = height; }
    gl.viewport(0, 0, width, height); gl.clearColor(.055, .078, .105, 1); gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT);
    gl.useProgram(this.program); gl.enable(gl.DEPTH_TEST); gl.disable(gl.CULL_FACE); gl.enable(gl.BLEND); gl.blendFunc(gl.SRC_ALPHA, gl.ONE_MINUS_SRC_ALPHA);
    const eye = bounds.center.map((value, i) => value + [Math.sin(this.yaw) * Math.cos(this.pitch), Math.sin(this.pitch), Math.cos(this.yaw) * Math.cos(this.pitch)][i] * this.distance);
    const camera = multiply(perspective(width / height, Math.max(.001, bounds.radius * .005), Math.max(bounds.radius * 100, this.distance * 10)), lookAt(eye, bounds.center));
    gl.uniformMatrix4fv(this.cameraUniform, false, camera); gl.uniform1f(this.flatUniform, 1);
    this.bind(this.gridBuffer); gl.drawArrays(gl.LINES, 0, this.gridCount);
    gl.uniform1f(this.flatUniform, this.wireframe ? 1 : 0);
    this.bind(this.wireframe ? this.lineBuffer : this.vertexBuffer); gl.drawArrays(this.wireframe ? gl.LINES : gl.TRIANGLES, 0, (this.wireframe ? this.prepared.lines : this.prepared.vertices).length / 10);
  }
  dispose() {
    if (this.disposed) return;
    this.disposed = true; this.abort.abort();
    if (this.frame) cancelAnimationFrame(this.frame);
    this.resize?.disconnect();
    for (const [target, event, callback, options] of this.listeners) target.removeEventListener(event, callback, options);
    if (this.gl) { for (const buffer of this.buffers) this.gl.deleteBuffer(buffer); for (const shader of this.shaders) this.gl.deleteShader(shader); if (this.program) this.gl.deleteProgram(this.program); this.gl.getExtension('WEBGL_lose_context')?.loseContext(); }
    this.mesh = null; this.prepared = null;
  }
}
