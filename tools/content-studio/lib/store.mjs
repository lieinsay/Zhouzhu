import { mkdir, readFile, open, rename, unlink } from 'node:fs/promises';
import { dirname, resolve } from 'node:path';
import { createHash, randomUUID } from 'node:crypto';
import { loadCatalog } from './catalog.mjs';
import { assertProjectStructure, validateProject, diffProjects } from './validation.mjs';

export class StoreError extends Error {
  constructor(status, message) { super(message); this.status = status; }
}
const hash = value => createHash('sha256').update(JSON.stringify(value)).digest('hex');
const clone = value => structuredClone(value);

export class WorkspaceStore {
  constructor({ repositoryRoot, workspaceDirectory, mediaLibrary }) {
    this.repositoryRoot = resolve(repositoryRoot);
    this.file = resolve(workspaceDirectory, 'draft.json');
    this.mediaLibrary = mediaLibrary;
    this.assets = undefined;
    this.queue = Promise.resolve();
    this.draft = null;
  }
  async init() {
    this.baseline = await loadCatalog(this.repositoryRoot);
    assertProjectStructure(this.baseline);
    await this.readDraft();
    if (this.mediaLibrary) this.assets = await this.mediaLibrary.getAssets(this.baseline);
    return this;
  }
  async readDraft() {
    try {
      this.draft = JSON.parse(await readFile(this.file, 'utf8'));
      if (this.draft.format !== 'zhouzhu-content-studio/v1' || typeof this.draft.revision !== 'string' || this.draft.sha256 !== hash(this.draft.project)) throw new Error('草稿封装或内容指纹不正确');
      assertProjectStructure(this.draft.project);
    } catch (error) {
      if (error.code !== 'ENOENT') throw new Error(`无法加载 ${this.file}：${error.message}。已保留原文件，请先备份检查。`);
      this.draft = null;
    }
  }
  revision() { return this.draft?.revision ?? `baseline:${hash(this.baseline.meta.sources)}`; }
  snapshot() {
    const project = clone(this.draft?.project ?? this.baseline);
    return { baseline: clone(this.baseline), project, revision: this.revision(), issues: validateProject(project, this.baseline, this.assets), changes: diffProjects(this.baseline, project) };
  }
  locked(operation) {
    const result = this.queue.then(operation);
    this.queue = result.catch(() => {});
    return result;
  }
  async getWorkspace() {
    return this.locked(async () => {
      this.baseline = await loadCatalog(this.repositoryRoot);
      await this.readDraft();
      if (this.mediaLibrary) this.assets = await this.mediaLibrary.getAssets(this.draft?.project ?? this.baseline);
      return this.snapshot();
    });
  }
  async saveProject(project, revision) {
    return this.locked(async () => {
      this.baseline = await loadCatalog(this.repositoryRoot);
      await this.readDraft();
      if (typeof revision !== 'string' || revision !== this.revision()) throw new StoreError(409, '草稿已被其他窗口保存，或源基线已变化。请重新加载并核对差异后重试。');
      try { assertProjectStructure(project); } catch (error) { throw new StoreError(422, error.message); }
      if (project.meta.sources.length !== this.baseline.meta.sources.length || this.baseline.meta.sources.some(source => !project.meta.sources.some(saved => saved.path === source.path && saved.sha256 === source.sha256))) throw new StoreError(409, '草稿来源指纹与当前源码不同。请先导出草稿保全，再重置到当前基线并整理变更。');
      const clean = clone(project);
      if (this.mediaLibrary) this.assets = await this.mediaLibrary.getAssets(clean);
      // The acquisition roots/enums come from the real source, never imported metadata.
      clean.meta.rawItems = clone(this.baseline.meta.rawItems);
      clean.meta.externalItems = clone(this.baseline.meta.externalItems);
      clean.meta.enums = clone(this.baseline.meta.enums);
      for (const collection of ['items', 'recipes', 'props', 'pieces', 'buildings', 'modules', 'deposits', 'scatters', 'tools', 'crops']) {
        const baselineSources = new Map(this.baseline[collection].map(row => [row.id, row.source]));
        for (const row of clean[collection]) {
          if (baselineSources.has(row.id)) row.source = clone(baselineSources.get(row.id));
          else delete row.source;
        }
      }
      await this.persist(clean);
      return this.snapshot();
    });
  }
  async persist(project) {
    const draft = { format: 'zhouzhu-content-studio/v1', revision: randomUUID(), savedAt: new Date().toISOString(), project, sha256: hash(project) };
    await mkdir(dirname(this.file), { recursive: true });
    const temporary = `${this.file}.${randomUUID()}.tmp`;
    try {
      const handle = await open(temporary, 'wx');
      try { await handle.writeFile(JSON.stringify(draft, null, 2) + '\n', 'utf8'); await handle.sync(); } finally { await handle.close(); }
      await rename(temporary, this.file);
    } catch (error) { await unlink(temporary).catch(() => {}); throw error; }
    this.draft = draft;
  }
  async resetProject(revision) {
    return this.locked(async () => {
      this.baseline = await loadCatalog(this.repositoryRoot);
      await this.readDraft();
      if (typeof revision !== 'string' || revision !== this.revision()) throw new StoreError(409, '草稿已变化，请重新加载后再重置。');
      if (this.draft) {
        const backup = resolve(dirname(this.file), `draft.backup.${randomUUID()}.json`);
        await readFile(this.file).then(async bytes => {
          const handle = await open(backup, 'wx');
          try { await handle.writeFile(bytes); await handle.sync(); } finally { await handle.close(); }
        });
      }
      await this.persist(clone(this.baseline));
      return this.snapshot();
    });
  }
}
