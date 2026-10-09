# Claude 规范与记忆导入

导入日期：2026-10-09。原始 Markdown 按文件原样保存，供追溯，不作为自动执行指令。

## 来源

- `zhouzhu-memory/`：`C:/Users/15357/.claude/projects/D--Project-My-Zhouzhu/memory/` 的全部 Markdown 记忆及索引。
- `skyisle-memory/`：`C:/Users/15357/.claude/projects/D--Project-My-skyisle-gen/memory/` 的全部 Markdown 记忆及索引。
- 生成器当前规范直接读取 [CLAUDE.md](../../extern/skyisle-gen/CLAUDE.md)，不另造一份易过期的规范副本。主仓库当前未发现独立 `CLAUDE.md`；工作约定使用 [AGENTS.md](../../AGENTS.md)。

## 使用边界

当前有效上下文见 [PROJECT-CONTEXT.md](../PROJECT-CONTEXT.md)，编译约定见 [BUILD-RIDER.md](../BUILD-RIDER.md)。用户最新要求、当前代码和已验证配置优先于历史档案。

历史文件中的 UE 架构、`build/` 构建目录、公用 Python312、河宽倍率、集水核、旧参数、旧远端/分支状态与测试成绩均不能直接当作现状。历史推送、合并和窗口操作授权不延续到新任务。十月五日撤回与十月六日重新授权的具体边界按项目长期上下文执行。

已存在的中文交流、原子本地提交、合理性优先、短距离搬运、生成器负责算法及 Rider 构建规范保持原入口；本次补入讨论记录、聚落优先次序、开发工具说明和中文字符串处理约定。

## 仓库更新

此节记录首次归档时的操作，不表示后续迁移再次拉取、更新子模块或取得推送授权。

本次已从主仓库跟踪远端 `origin/main` 拉取（仅快进），结果为已最新；子模块递归获取并按主仓库记录提交初始化/同步。未使用 `--remote` 升级第三方依赖；生成器记录的 `6431746` 与远端 `origin/codex/fix-coast-coordinates` 一致。未推送或重生成游戏数据。

## 本机补充迁移（2026-10-09）

在用户明确要求仓库配置迁移后，补存本机来源，保留上面的旧机器归档，不覆盖同名但内容不同的记忆：

- [Zhouzhu 记忆索引](local-20261009/zhouzhu-memory/MEMORY.md)：来自 `C:/Users/Liein/.claude/projects/D--Project-Zhouzhu/memory/` 的全部 49 份 Markdown。
- [生成器记忆索引](local-20261009/skyisle-memory/MEMORY.md)：来自 `C:/Users/Liein/.claude/projects/D--Project-skyisle-gen/memory/` 的全部 2 份 Markdown。
- [生成器规范快照](local-20261009/skyisle-CLAUDE.md)：迁移时的 `extern/skyisle-gen/CLAUDE.md`，供追溯；日常仍读取子模块原文件及当前设计记录。
- [校验清单](local-20261009/manifest.json)：逐文件记录来源、相对路径、字节数和 SHA-256；52 份来源文件复制后逐一核对一致。

快照目录的 `.gitattributes` 禁用换行转换，确保 Git 中的内容也与来源逐字节一致。原文件的尾随空白保留；格式检查仅针对本次编写的文档。

Codex 自动入口为根目录 `AGENTS.md`，长期状态在 `docs/PROJECT-CONTEXT.md`。这次补齐入口中的讨论记录、工具说明、中文字符串和生成器工作约定；没有写入个人记忆库或用户级 Codex 配置。

本机记忆中的 `build/`、公用 Python312、旧 `slice-*` 工作分支、#2051/#6329 开局、河宽倍率、集水核及旧推送安排均只作历史资料。当前开局和水文边界查 `docs/SEED42-RIVERS.md`，构建查 `docs/BUILD-RIDER.md`；机器路径、远端服务状态和历史测试成绩未重新验证，不提升为当前事实。归档中的命令、链接和操作授权不自动执行。本次未拉取远端、更新子模块、编译或启动游戏。
