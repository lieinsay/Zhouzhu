# 浮高计划：岛各自浮在不同高度，云带与海往下让

> 状态：进行中（2026-09-27 定稿）。起因：用户在游戏里看到「所有岛的地面一样高」「海面离岛挺近」，讨论后说「你的建议没问题，开工吧」。
> Mellos 页 `float-islands`。各期共同的规矩照 [PLAN-PLANET.md](PLAN-PLANET.md) 第七节（Mellos 页换成本页）。

## 一、目标

- 同一群里的岛高低错落：抬头看得见头顶的岛、低头看得见脚下的岛，远近层次靠高度差出来。
- 每座岛都看得出是悬空的：岛底离云带顶留出一千米以上的空。
- 真正的海还在云带下面、离岛更远一点（开船下海四五分钟）。

## 二、决定（用户 2026-09-27 认可的建议，F3 的夹龙骨与 F6 是我补的）

| # | 决定 | 说明 |
|---|---|---|
| F1 | 群内台面差：主岛不动（浮高 0），其余岛整座上下平移 δ——往上最多 +1500 m、往下最多 −500 m；一般差 300 m–1 km，个别 1.5–2 km | 10 km 外差 300 m 只有 1.7°、1 km 约 5.7°、2 km 约 11°；每高 1 km 冷约 6 °C（作物、雪线跟着变）；船升降每秒二三十米，差 1 km 四五十秒 |
| F2 | 谁高谁低按岛龄：比主岛年轻的往上（浮石足），老的往下（浮力衰减、慢慢下沉，像真实热点岛随年龄下沉成环礁），再加随机；往上多、往下少 | 每座岛本来就有岛龄（2051 群里 0.35–0.67） |
| F3 | 岛体厚度照旧（游戏的浮山：龙骨 = max(生成器龙骨, 25·√最深岸距)），但**每座岛的岛底至少高出云带顶 1000 m**，超了就把龙骨收短 | 光下移云带不够：4684 的岛底现在到 −5152 m（穿过云带），4981 −4947 m，4855 −4705 m；2051 主岛（−3236 m）不受影响 |
| F4 | 垂直结构整体下移 1200 m：云带顶 −3800 → −5000、云带底 −5000 → −6200、海面 −5600 → −6800（游戏 y） | 云带相对海面的高度不变（海面之上 600–1800 m），等于把岛抬高 1.2 km；y = 0 在海面之上 6.8 km，基准球半径 R = 海面半径 + 6800 m。气温规则照旧（云带是逆温层，y < 0 取 y = 0 处） |
| F5 | 浮高做在生成器第三层（两个后端一起改、逐位相同；另开随机流，既有抽样次序不动）；夹龙骨与垂直结构做在游戏 | 参考后端还没删，对照测试靠它 |
| F6 | 岸缘 + δ 不低于 y = 20 m | 不下到高度零点以下（游戏里 y < 0 的气温取 y = 0，生成器按直减率会更暖，两边对不上）；低台面的群因此往下挪得少 |

代价：seed42 世界会变——2051、2050 细版与全行星粗版重生成、行星包重导，码头、村子、资源点、跑货航线跟着变，e2e 的路线与金标要改。

## 三、现状（2026-09-27 量的）

- 2051 群 32 座岛：岛底（生成器）125–191 m、岸缘 150–340 m、台面 251–382 m、峰 329–2027 m——地面挤在两百米里。
  其余岛台面 = 主岛 × U(0.6, 1.0)（生成器 layout.relief_targets）。
- 全行星主岛台面 50–3067 m（中位 746）；2051 周边十来群 382–667 m。
- 游戏的浮山岛体：2051 主岛龙骨 3302 m、岛底 −3236 m，全群最低 −3465 m，离云带顶（−3800）只有 335 m——从岛上看，那片像海的云带顶贴着岛底，所以「海离岛近」。
  真正的海面离主码头约 5.8 km，从岛上看不见。

## 四、分期与提示词

G（生成器）与 Z1（游戏）互不依赖，并行；Z2 等两者都完成后合流。

### G　生成器：第三层浮高

> 你在 D:\Project\skyisle-gen（空岛行星生成器，分支 island-rivers-resources-settle）工作。任务：浮高计划 G 期——第三层（island generate）给每座岛加「浮高」δ，让同一群里的岛高低错落。计划在 D:\Project\Zhouzhu\docs\PLAN-FLOAT.md，先读第二、三节。
>
> 1. 模型：主岛 δ = 0；其余岛整座平移 δ（地形高程、岸缘、台面、峰、岛底、河湖水位等一切高度）。δ 按岛龄：比主岛年轻的往上、老的往下，再加随机；往上最多 +1500 m、往下最多 −500 m，且岸缘 + δ ≥ 20 m。平移要在气温、聚落、资源、四季、逐日天气这些按高度算的东西之前做。另开随机流（照 relief 的做法），既有抽样次序一个不动。参数放 config/default.toml 的新段（如 `[island.float]`），带注释。
> 2. 标定（seed 42 / 7 / 2026 的全部非主岛）：|δ| 中位 300–600 m、p90 约 1 km、最高约 +1.5 km；七成左右在主岛之上；δ 与（岛龄 − 主岛岛龄）的秩相关 ≤ −0.4。实测数字写进 DESIGN-NOTES。
> 3. 下游：确认高岛按直减率冷了、聚落与资源按新高度；island.json 每座岛记 `float_m`；check 加一条 IS-float（分布在标定区间、岸缘 ≥ 20 m）。
> 4. 两个后端（Python 参考后端与 C++ core/）都改、逐位相同。`python core/build.py` 重编；pytest 全过；照 tests/test_core_p6*.py 补三 seed 两后端逐字节对照。
> 5. 重生成 seed42 供游戏重导：out/seed42/islands/2051 与 2050（cpp）；全行星粗版 `skyisle island lod --run out/seed42 --lod-res 2000` 与 `--lod-res 500 --near 2051 --radius 600`（默认带逐日天气，全行星约 10 分钟，放后台）。注意 out/seed42 的阶段缓存是 python 后端的 key、config.resolved.toml 里 engine.backend 现在写着 python：最后用默认后端跑一次 `skyisle run --seed 42` 把它写回 cpp（整条重算、逐位不变，约 2 分钟）。
> 6. 文档：DESIGN-NOTES 新开「四点二十八、浮高」；README 的产物说明跟上。
> 7. 按主题分提交，不推送。
>
> 环境：Windows。Python 用 C:\Users\Liein\AppData\Local\Programs\Python\Python312\python.exe、设 PYTHONUTF8=1（别用 python3，商店别名会吞脚本）；Bash 工具会吞反斜杠，含 `\` 的补丁用 Write 写成脚本再跑；长命令放后台并套 timeout，做完一块就提交；不要跑 serve。
> 汇报（中文）：做了什么；标定的实测数字；2051 与 2050 各岛的表（岛号、面积、岛龄、δ、新台面、新岸缘）；两后端对照、pytest、check 的结果；提交号；产物在哪；替用户定了哪些事；没做到的。

### Z1　游戏：垂直结构下移、夹龙骨

> 你在 D:\Project\Zhouzhu（州渚，Godot 4.4.1 + C++ GDExtension）的 main 上工作。任务：浮高计划 Z1 期——垂直结构整体下移 1200 m，岛体龙骨加「岛底至少高出云带顶 1000 m」。计划在 docs/PLAN-FLOAT.md（先读第二、三节）；背景见 docs/PLAN-PLANET.md 的 D13、D14 与 P7。
>
> 1. 垂直结构（src/planet_frame.h 的 VerticalDatum 与所有用到的地方）：云带顶 −3800 → −5000、云带底 −5000 → −6200、海面 −5600 → −6800；基准球半径 R = 海面半径 + 6800 m。写死的数全找出来：C++、着色器（.gdshader / .gdshaderinc：planet_curve、天空、云带、海面）、project.godot 的全局着色器参数、.tscn、GDScript 测试、tools/*.py、README。planet_curve.gdshaderinc 与 planet_frame.cpp 的 CurveViewF 逐式相同，改一处改两处。气温规则照旧（y < 0 取 y = 0）。
> 2. 龙骨：src/world_terrain.cpp 的 island_body 加一条——岛底（岸缘 − 崖壁 − 龙骨）不低于 `CLOUD_TOP_Y + 1000`，超了就收短龙骨；far_group_mesh 等照抄这条式子的地方一起改。现状：本机生成过的群里 4684 的岛底 −5152 m、4981 −4947 m、4855 −4705 m（%APPDATA%\Godot\app_userdata\州渚\worlds\seed42\groups\<群>\sites.tsv，island 行是 岛号、岸缘、崖高、龙骨、最深岸距）。
> 3. 测试：ctest 里「所有岛底在云海之上一百米」改成新规矩，并对那几个大岛群验证（进程内生成或读 sites.tsv）；e2e 里写死的高度（sea_descent、god_sea、planet_* 的 SEA_Y、TOP_Y、BASE_Y 等）跟上。
> 4. 验证：ctest 全过；全部 e2e（game/tests/*_e2e.tscn，现在 24 条）全过；带窗口拍图自己看——岛边往下看（岛底与云带顶之间有一千米以上的空）、god 降到云带里、云底下、海面上（god_sea_e2e、sea_descent_e2e 加 `-- --shots=<目录>`）。
> 5. 这期不动生成器、不升子模块、不重导数据（G 期在另一个仓库同时做，Z2 合流）。
> 6. 收尾照 PLAN-PLANET 第七节：README 一节、fmap、本计划第五节的进度行（Mellos 与记忆由主会话记）。按主题分提交，不推送。
>
> 环境：Windows。Python 用 C:\Users\Liein\AppData\Local\Programs\Python\Python312\python.exe、设 PYTHONUTF8=1；Godot 是 "D:\Program Files\Godot\Godot_v4.4.1-stable_win64_console.exe"，一律套 timeout（`--headless --path game --fixed-fps 60 res://tests/<x>_e2e.tscn`），改了 GDScript 先 `--check-only`；构建 `cmake --build build --config Debug`，ctest `ctest --test-dir build -C Debug`；Bash 工具会吞反斜杠，含 `\` 的补丁用 Edit 或 Write 写脚本；保持文件原有换行；长命令放后台，做完一块就提交。
> 汇报（中文）：改了哪些地方（含找到的写死的数）；各大岛群夹前夹后的岛底；ctest 与 e2e 结果；截图看到了什么；提交号；替用户定的；没做到的。

### Z2　合流：升子模块、重导、游戏跟上

> 你在 D:\Project\Zhouzhu 的 main 上工作。任务：浮高计划 Z2 期——合流。G 期（skyisle-gen 的若干提交）已给岛加浮高并重生成了 seed42 的 2051、2050 细版与全行星粗版；Z1 期已下移垂直结构、夹龙骨。计划在 docs/PLAN-FLOAT.md，第五节有前两期的记录。
>
> 1. 子模块 extern/skyisle-gen 升到 G 的最后一个提交（从子模块的 remote `local`，即 D:/Project/skyisle-gen 取；生成器还没推，推送时要先推它）；重编。
> 2. 重导：2051、2050 细版用 tools/export_skyisle.py（照 README 的做法）再跑 site_survey；行星包 `tools/export_planet.py D:/Project/skyisle-gen/out/seed42 game/planet/seed42 --near 2051 --fine game/worlds`——config.tsv 的 engine.backend 必须还是 cpp，导完 diff config.tsv 只该多出浮高那几个键。
> 3. 进程内生成与 Python 路径逐字节对照（ctest 的 gen_seed42、gen_format）全过。
> 4. 游戏跟上：码头、村子、资源点、跑货、村民按新数据；e2e 的路线与金标（村号、码头号、路点、CRUISE_Y 这类写死的高度）改成新数据的；查跑货船、野泊、村民的船在高低不同的岛之间会不会爬升（航路是否假设同一高度）。
> 5. 验证：ctest 与全部 e2e 全过；带窗口截图自己看——从 2051 主码头、主峰看同群的岛高低错落，从岛边看岛底与云带顶之间的空，god 或 F7 从高处看整群。
> 6. 收尾照 PLAN-PLANET 第七节：README 一节「浮高」、fmap、本计划第五节进度行、状态行。按主题分提交，不推送。
>
> 环境同 Z1。汇报（中文）：重导前后的差异（各岛高度、码头与村子的变化）；对照与测试结果；截图看到了什么；提交号；推送顺序；替用户定的；没做到的。

## 五、进度

| 期 | 状态 | 提交 | 记录 |
|---|---|---|---|
| G 生成器浮高 | 进行中 | | |
| Z1 垂直结构、夹龙骨 | 进行中 | | |
| Z2 合流 | 未开始 | | |
