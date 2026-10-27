// Asset bindings belong to the tool draft. The game source is never rewritten.
export const VISUAL_COLLECTIONS = ['items', 'props', 'pieces', 'buildings', 'modules', 'deposits', 'scatters', 'tools', 'crops'];
export const MODEL_USAGE = { placed: '摆放实体', construction: '搭建构件', 'gather-source': '场景采集源', building: '建筑外壳', 'ship-module': '船舶模块' };

// Even an imported draft may only select assets the local server has indexed.
export function safeAssetUrl(value) {
  if (typeof value !== 'string' || !value.startsWith('/') || value.startsWith('//') || /[\\\s?#]/.test(value)) return null;
  let decoded;
  try { decoded = decodeURIComponent(value); } catch { return null; }
  if (decoded.includes('..') || /[\\\s?#%]/.test(decoded)) return null;
  return /^\/(?:assets|api\/assets|api\/models|api\/art\/(?:image|model|snapshot))\/[A-Za-z0-9_./%-]+$/.test(value) ? value : null;
}

export function normalizeLibrary(value) {
  return {
    thumbnails: (Array.isArray(value?.thumbnails) ? value.thumbnails : []).filter(row => row && typeof row.id === 'string' && safeAssetUrl(row.url)),
    models: (Array.isArray(value?.models) ? value.models : []).filter(row => row && typeof row.id === 'string' && safeAssetUrl(row.url) && row.format === 'mesh-json'),
    missingItems: Array.isArray(value?.missingItems) ? value.missingItems : [],
    missingModels: Array.isArray(value?.missingModels) ? value.missingModels : [],
    missingThumbnails: Array.isArray(value?.missingThumbnails) ? value.missingThumbnails : [],
  };
}

export function recordItemId(record, collection) {
  if (!record) return null;
  return collection === 'items' ? record.id : record.product || record.item || record.seed || null;
}

export function defaultVisual(record, collection, library, project) {
  const itemId = recordItemId(record, collection);
  const assetId = (collection === 'modules' ? 'module.' + record.id : collection === 'buildings' ? 'building.' + record.id : 'item.' + itemId);
  const itemBinding = collection !== 'items' ? project?.visuals?.items?.[itemId] : undefined;
  const thumbnail = itemBinding?.thumbnail !== undefined ? itemBinding.thumbnail : (library.thumbnails.find(asset => (asset.assetId === assetId || itemId && asset.itemId === itemId) && asset.formalActive) || library.thumbnails.find(asset => itemId && asset.itemId === itemId && !asset.version))?.id || null;
  const inventoryUsage = asset => !['gather-source', 'building', 'ship-module'].includes(asset.usage);
  const exact = library.models.find(asset => asset.collection === collection && asset.recordIds?.includes(record.id) && (collection !== 'items' || inventoryUsage(asset)));
  // A rock outcrop or tree is not the inventory entity of its harvested material.
  const byItem = library.models.find(asset => asset.itemIds?.includes(itemId) && inventoryUsage(asset));
  return { thumbnail, model: exact?.id || byItem?.id || null, scale: [1, 1, 1], rotation: [0, 0, 0], offset: [0, 0, 0] };
}

export function resolveVisual(project, record, collection, library) {
  const defaults = defaultVisual(record, collection, library, project);
  const binding = project.visuals?.[collection]?.[record.id];
  const config = binding ? { ...defaults, ...binding } : defaults;
  return {
    config,
    explicit: !!binding,
    thumbnail: library.thumbnails.find(asset => asset.id === config.thumbnail) || null,
    model: library.models.find(asset => asset.id === config.model) || null,
    missingThumbnail: !!config.thumbnail && !library.thumbnails.some(asset => asset.id === config.thumbnail),
    missingModel: !!config.model && !library.models.some(asset => asset.id === config.model),
  };
}

export function writeVisual(project, record, collection, library, key, value, axis) {
  const next = structuredClone(resolveVisual(project, record, collection, library).config);
  if (axis === undefined) next[key] = value;
  else { next[key] = [...next[key]]; next[key][axis] = value; }
  project.visuals ||= {};
  project.visuals[collection] ||= {};
  project.visuals[collection][record.id] = next;
  return next;
}

export function clearVisual(project, collection, id) {
  if (!project.visuals?.[collection]) return;
  delete project.visuals[collection][id];
  if (!Object.keys(project.visuals[collection]).length) delete project.visuals[collection];
  if (!Object.keys(project.visuals).length) delete project.visuals;
}

// Retain a binding while an ID is temporarily invalid or collides during typing.
const pendingIds = new WeakMap();
export function renameVisual(project, record, collection, oldId) {
  const pending = pendingIds.get(record);
  const sourceId = pending?.collection === collection ? pending.id : oldId;
  const source = project.visuals?.[collection]?.[sourceId];
  if (!source || sourceId === record.id) return;
  if (project[collection]?.some(row => row !== record && row.id === sourceId)) return;
  if (project[collection]?.some(row => row !== record && row.id === record.id)) { pendingIds.set(record, { collection, id: sourceId }); return; }
  project.visuals[collection][record.id] = source;
  delete project.visuals[collection][sourceId];
  pendingIds.delete(record);
}

export function removeRecordVisual(project, record, collection) {
  const pending = pendingIds.get(record);
  for (const id of new Set([record.id, pending?.collection === collection ? pending.id : record.id])) {
    if (!project[collection]?.some(row => row !== record && row.id === id)) clearVisual(project, collection, id);
  }
  pendingIds.delete(record);
}

export function previewTransform(config) {
  const vector = (value, fallback, positive = false) => [0, 1, 2].map(i => typeof value?.[i] === 'number' && Number.isFinite(value[i]) && (!positive || value[i] > 0) ? value[i] : fallback);
  return { scale: vector(config?.scale, 1, true), rotation: vector(config?.rotation, 0), offset: vector(config?.offset, 0) };
}
