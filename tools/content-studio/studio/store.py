"""共享工作空间锁、旧 v1 指纹读取、原子保存和来源漂移保护。"""
import copy
import threading
import uuid
from pathlib import Path
from .catalog import load_catalog
from .common import StudioError, atomic_json, digest, read_json, timestamp
from .validation import assert_project_structure, validate_project, diff_projects


class WorkspaceStore:
    def __init__(self, repository_root, workspace_directory, media_library=None):
        self.repository_root = Path(repository_root).resolve()
        self.file = Path(workspace_directory).resolve() / 'draft.json'
        self.media_library, self.assets = media_library, None
        self.lock = threading.RLock()
        self.draft = None
        self.baseline = load_catalog(self.repository_root)
        assert_project_structure(self.baseline)
        self.read_draft()
        if media_library:
            self.assets = media_library.get_assets(self.baseline)

    def read_draft(self):
        try:
            draft = read_json(self.file)
            if draft is not None:
                if not isinstance(draft, dict) or draft.get('format') != 'zhouzhu-content-studio/v1' or not isinstance(draft.get('revision'), str) or draft.get('sha256') != digest(draft.get('project')):
                    raise ValueError('草稿封装或内容指纹不正确')
                assert_project_structure(draft['project'])
            elif self.file.exists():
                raise ValueError('草稿不是有效的项目封装')
            self.draft = draft
        except (ValueError, OSError, TypeError, OverflowError) as error:
            raise StudioError(500, f'无法加载 {self.file}：{error}。已保留原文件，请先备份检查。') from error

    def revision(self):
        return self.draft['revision'] if self.draft else 'baseline:'+digest(self.baseline['meta']['sources'])

    def snapshot(self):
        project = copy.deepcopy(self.draft['project'] if self.draft else self.baseline)
        return dict(baseline=copy.deepcopy(self.baseline), project=project, revision=self.revision(), issues=validate_project(project, self.baseline, self.assets), changes=diff_projects(self.baseline, project))

    def reload(self):
        self.baseline = load_catalog(self.repository_root)
        assert_project_structure(self.baseline)
        self.read_draft()

    def get_workspace(self):
        with self.lock:
            self.reload()
            if self.media_library:
                self.assets = self.media_library.get_assets(self.draft['project'] if self.draft else self.baseline)
            return self.snapshot()

    def persist(self, project):
        draft = dict(format='zhouzhu-content-studio/v1', revision=str(uuid.uuid4()), savedAt=timestamp(), project=project, sha256=digest(project))
        atomic_json(self.file, draft)
        self.draft = draft

    def save_project(self, project, revision):
        with self.lock:
            self.reload()
            if not isinstance(revision, str) or revision != self.revision():
                raise StudioError(409, '草稿已被其他窗口保存，或源基线已变化。请重新加载并核对差异后重试。')
            try:
                assert_project_structure(project)
            except (ValueError, TypeError, OverflowError) as error:
                raise StudioError(422, str(error)) from error
            sources = project['meta']['sources']
            if len(sources) != len(self.baseline['meta']['sources']) or any(not any(row['path'] == source['path'] and row['sha256'] == source['sha256'] for row in sources) for source in self.baseline['meta']['sources']):
                raise StudioError(409, '草稿来源指纹与当前源码不同。请先导出草稿保全，再重置到当前基线并整理变更。')
            clean = copy.deepcopy(project)
            if self.media_library:
                self.assets = self.media_library.get_assets(clean)
            for field in ('rawItems', 'externalItems', 'enums'):
                clean['meta'][field] = copy.deepcopy(self.baseline['meta'][field])
            for collection in ('items', 'recipes', 'props', 'pieces', 'buildings', 'modules', 'deposits', 'scatters', 'tools', 'crops'):
                sources_by_id = {row['id']: row['source'] for row in self.baseline[collection]}
                for row in clean[collection]:
                    if row['id'] in sources_by_id:
                        row['source'] = copy.deepcopy(sources_by_id[row['id']])
                    else:
                        row.pop('source', None)
            self.persist(clean)
            return self.snapshot()

    def reset_project(self, revision):
        with self.lock:
            self.reload()
            if not isinstance(revision, str) or revision != self.revision():
                raise StudioError(409, '草稿已变化，请重新加载后再重置。')
            if self.draft:
                backup = self.file.with_name(f'draft.backup.{uuid.uuid4()}.json')
                with backup.open('xb') as handle:
                    handle.write(self.file.read_bytes())
                    handle.flush()
                    import os
                    os.fsync(handle.fileno())
            self.persist(copy.deepcopy(self.baseline))
            return self.snapshot()
