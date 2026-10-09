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

本次已从主仓库跟踪远端 `origin/main` 拉取（仅快进），结果为已最新；子模块递归获取并按主仓库记录提交初始化/同步。未使用 `--remote` 升级第三方依赖；生成器记录的 `6431746` 与远端 `origin/codex/fix-coast-coordinates` 一致。未推送或重生成游戏数据。
