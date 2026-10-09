---
name: skyisle-town-progress
description: skyisle-gen 聚落营建器（PLAN-TOWN）做到哪了、在哪个分支、怎么验收
metadata:
  node_type: memory
  type: project
  originSessionId: 8d8baaf7-c084-46cc-a18e-fc7eea98cd9d
  modified: 2026-09-29T07:41:20.981Z
---

**用途（用户定，2026-09-29）：营建器只给用户做参考，暂不接进游戏**——Zhouzhu 不读它，用户开口前别往游戏里接（文档、命令帮助、/town.html 都已注明）。
2026-09-29：PLAN-TOWN 第一、二、三、五步已做，town-plan 分支已**快进并入 main（3cfedc7）并推送 origin**（town-plan 分支本身没推，本地还留着）；之后在 main 上接着做。
第三步 + 第五步是 a2c7991..6b09769；「户少就摊开」与两处压路 382359a..c4aba6e；「参考用」86d967d；**地形优先**（用户定：地形 > 合理 > 风格，DESIGN-NOTES 四点三十三）38febf1..3cfedc7：
plan_site 住不下就放宽地面上限两档（TP-terrain）、organic_fill 最后换风格里最小的院子、TP-style 里 λ / 朝向的「地形所致、不算」。
还没做：第四步集镇与城、第六步专业聚落与整群批跑。已知短处：窑洞台少；放宽只有地面上限一个维度。房子稀不算短处，见 [[feedback-town-reasonable-over-metrics]]。

**Why:** 分支状态与验收口径不在代码里，换会话容易重做或误推。

**How to apply:** 改营建算法后：①「十种合成地形 × fishbone/organic、60 户、--style 华北集村」20 例 + `town site 2051 --run out/seed42 --site 村037 --style 华北集村`（应是 70 院 269 房 52 路 1 桥、院子朝北），硬项全过；
② `town gallery --terrain 河谷 --styles all --households 40` 十二格硬项全过；③ pytest tests/test_town.py（81 个）；④ 全矩阵（每风格 × 各算子强制 + 默认 × 十地形 40 户，300 例）现在应**全过**。
十种地形名：平原 曲流平原 河谷 朝阳坡 山顶 湖岸 峡湾岸 黄土沟 两河交汇 岛缘崖台。风格覆写用 `--set style.ground.max_slope_deg=…`（不带 style. 前缀不生效）。
用户的生成器服务（`skyisle_gen.cli serve --host 127.0.0.1,10.8.0.2`）占着 _core.pyd 时重编会 LNK1104：**别停它**，把 pyd 挪到 build/stale/ 再编（Windows 允许改名已加载的 DLL），让用户自己重启。
页面改动用无头 Edge + CDP 驱动（服务起在别的端口，别占 8642）。补丁脚本用 Write 写，不用 bash heredoc（反斜杠会被吞）。推送 / 并 main 等用户开口（快进用 `git fetch . 分支:main`，不动工作区、不扰用户的服务）。
环境坑见 [[skyisle-env-numpy]]、[[zhouzhu-console-utf8]]；仓库背景见 [[planet-generator-split-out]]。
