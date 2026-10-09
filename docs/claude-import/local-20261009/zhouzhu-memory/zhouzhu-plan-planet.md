---
name: zhouzhu-plan-planet
description: 行星计划（渲染整颗行星、任意岛群可去）docs/PLAN-PLANET.md P0–P7、P6a–e 2026-09-27 全部完成并已推送；⑨ 附庸 bug 已修；升生成器子模块要重导行星包头；Mellos 页 plan-planet
metadata:
  node_type: memory
  type: project
  originSessionId: e854470c-c3d8-45a6-8052-1ce4de69478a
  modified: 2026-09-27T09:19:14.280Z
---

2026-09-26 用户要「生成器负责给地图数据，游戏负责实时生成，目标是能渲染整个行星」，说「任务庞大，你来计划，每个阶段的提示词放到计划里」。
计划写在 `docs/PLAN-PLANET.md`（第五节每期一段提示词；第六节记进度；第七节是各期共同规矩），Mellos 页 `plan-planet`（标签前缀是期号）。
用户指出漏了「岛下是云带、云带之下是全球海洋、空艇可降至海面」→ 补了 D13（云带 −3800…−5000、海面 −5600、y<0 气温取 0 处、R = 6,376,600 m）、D14 与 P7「降到海面」。
用户问过「像 MC 挖地」：答复与行星计划互不牵扯（改动按群内坐标 + 地形版本记），用户决定先不做、继续行星计划——并且在同一会话里直接开工了 P0（不是新会话贴提示词）。

- 分期：P0 行星数据与坐标地基（已完成）→ P1 地平线外（着色器弯曲、邻群低模、日月按经纬）→ P2 上到轨道 → P3 多岛群与换锚 → P4 按需生成 → P5 全球天气与风 → P6 第三层移植 C++；P7 降到海面（P2 之后随时）。
- 我定的关键决定：AEQD、单精度 + 着色器按真球面弯曲 + 换锚、物理只在锚群、钟 = 152.15°E 地方时、行星包 + 粗版 ≤ 60 MB 进仓库、邻群不许叠。
- 2026-09-27 用户定方向（写进计划 D15、D16、D3，提交 a4 字头见 git log）：剧情模式用 seed42（出厂预烘），生存模式随机种子、像 MC 一样走近了才生成；
  问「生成器和游戏是不是合并、后端变 C++、前端不变还能用」→ 答：仓库不合并、算法合一——skyisle-gen 新增 C++ core/（nanobind 给 Python 前端用、Zhouzhu 以子模块编进扩展）。
  切到 C++ 时 seed42 世界会整体变一次，所以剧情内容放到 P6d 之后。随机世界要不要邦国文化没单独问——因为整条管线都移植，自然都有。
- **P0 的坑与事实**：
  - 生成器的布局随分辨率变（2051 在 1 km 下生成岛心差中位 24 km）→ 粗版一律「原生分辨率生成再降采样」（`skyisle island lod`），全行星 28 进程约两小时，产物在 skyisle-gen `out/seed42/islands_lod/{2000,1000,500}`。
  - 势力范围（skyisle-gen 7e5a919）：全行星重摆 67%，34 群仍越线、14 对邻群陆地还挨着（③ 群心太挤，#342–343 只隔 3 km），要根治得改 ③。
  - 行星包 `game/planet/seed42`：planet.bin 7.9 MB（按名字的列 + 30 张 1° 场，纯 C++ 读、头是「键 值」文本）、lods.bin 45.6 MB（2 km 全行星 + 2051 周围 500 m，每格 4 字节、空格行程编码、按需解）。1 km 全行星要 160 MB，没导。
  - `planet_curve.gdshaderinc` 与 `planet_frame.cpp` 的 CurveViewF 逐式相同，改一处改两处；δ 展开式要带 y·k²/24 与 k³ 项，否则 600 km 差 17 mm。
  - C++ 单测里 `check(f(err), "…" + err)` 实参求值次序不定——先调用再拼提示。
  - hash-object 分段暂存要带 `--path <文件>`，否则 CRLF 不转、整文件显示改动。
- 提交：skyisle-gen 7e5a919、4e4baf7、2c1231b；Zhouzhu 602ebd3、6a937b5（计划）、a769ef7（坐标）、3ab0cef（行星包）、1ecee7a（文档）、de1c08c（fmap）；2026-09-27 用户说推送后已推送 Zhouzhu main → origin 与 mepro（de1c08c，连同时令与天气的 12 个），skyisle-gen island-rivers-resources-settle → origin（2c1231b）。验证：ctest 3/3（core 1156）、18 条 e2e 全过（interior 的结果行写「通过」不写 PASS，批跑 grep 要两个都认）。

- **2026-09-27 凌晨起的工作方式**：用户说「做完计划之后直接开始工作，直到做完，上下文过长你自己解决」然后去休息。主会话每期派一个 general-purpose 子任务（后台、上下文全新），
  两条线并行：A 线 Zhouzhu（P1 → P2 → P3 → P4 → P5 → P7 → P6e）、B 线 skyisle-gen（P6a → P6b → P6c → P6d）。子任务自己按主题提交、不推送，写 README / fmap / 计划里本期的进度；
  B 线的计划进度、Mellos 地图、记忆都由主会话统一记。子任务结束后主会话复核（看提交、跑 ctest、抽看截图）再派下一期。提示词要带上这台机器的坑（见本文件与 [[zhouzhu-console-utf8]]）。
  子任务会被「10 分钟没进展」的看门狗停掉（连接卡住）：用 SendMessage 给它的 agentId 发一句「接着做」就续上，上下文还在；提示词里要求长命令放后台、做完一块就提交。
- P6a 完成（skyisle-gen 5d37473..3813684）：core/（CMake + nanobind 3.1，`python core/build.py`），第三层地形段两个后端**逐位相同**（随机流照 numpy PCG64 + ziggurat、浮点照 numpy 次序），地形 + 水系 ×11 快；
  所以 D15「切 C++ 时 seed42 会变」可能不成立，看 P6d 的结论。
- P1 完成（Zhouzhu e77e339..b1a53d5）：PlanetView、弯曲材质、弯曲云带顶面、FarGroups（600 km 内 19 群）、日月按经纬、god 升降随高度。
  **远平面仍 60 km**（Godot 投影矩阵 CPU 单精度，1,200 km 时每帧报错）：着色器把 10 km 外的东西沿视线压进 10–26 km，雾因此远处停在五成；轨道视角要重新想（交给了 P2）。
  主码头地面上看不到 2049（被本岛挡住），主峰上看得到。
- P6b 完成（skyisle-gen 1d76935..b01e5ee）：第三层整群 generate 进 C++，两后端整套产物逐字节相同，4 线程中位 0.62 s，全行星粗版 28 进程约 4 分钟。
  逐位只在本机验过（Windows、numpy 2.5.2、OpenBLAS SkylakeX）。之后派了 P6c（行星层 ①–④）与 P2（轨道）。
- P2 完成（Zhouzhu 90db0b0..b432f44）：行星与大气**不建网格**、在天空着色器里逐像素算（单精度下两万公里高海面与云带顶两层网格会打架）；Hillaire 2020；
  地面保留原天空、20–60 km 交叉淡化；行星贴图启动时后台烘（Release 约 3 s）、BC3 自压；F7 轨道相机；11 km 以上 HUD 报「高空」、100 km 以上「太空」；
  相机正下方离锚群 > 620 km 就藏起锚群。飞一圈后私有提交 1488 MB（离 1.5 GB 预算很近）。之后派了 P3。
- P6c 完成（skyisle-gen e54076b..a13fbff）：①–④ 进 C++，三 seed 逐位相同；缓存 key 只给 cpp 加后缀、STAGE_VERSIONS 没加（加了会让 out/seed42 白白重算）；带了 pocketfft 头文件追 np.fft。
- P6d 派出时主会话改了计划原文：切默认到 cpp 后 **Python 算法先不删**，冻结成参考后端，等 ME Pro Linux 编过验过再删；并要求验证 seed42 切换后不变（不变则游戏行星包不用重导）。
- P6d 完成（skyisle-gen 8777934..00442c8）：⑤–⑨ 进 C++，**默认后端 cpp**，**seed42 切换后不变**（包不用重导、剧情内容不用等）；管线 ①–⑨ 43.4 s → 2.5 s，内存里新建世界 1.2 s。
  核心库接法：静态库 `skyisle_core`（SKYISLE_PYTHON=OFF、SKYISLE_TESTS=OFF），头 `skyisle/planet/{planet,civ,view}.hpp`、`skyisle/island/generate.hpp`；游戏要自己的扁平配置来源（P6e）。
  注意：默认配置在 out/seed42 上再 `run --seed 42` 会整个重算（逐位不变但重写目录），要么别跑、要么 `--backend python`。
  **待用户拍板**：⑨ 附庸判定的 bug（两后端照抄；修了附庸 0 → 约 150，改 polities.json 与九格表 ⑧，不影响邦 / 人口 / 第三层）。

- P3 完成（Zhouzhu 5200f77..dd85df1）：GroupWorld + PlanetJobs、GroupStream（近景 55/70 km、细版 30/40 km）、换锚（只在云海上空，那一帧 11 ms、画面跳 3 mm）、PlanetHost（别的群码头号 = 群号 × 1000 + 号）、
  账本留内存回来补结、天气每群一份与地方时、2050 细版进仓库；内存最高 1255 MB。可疑处交给 P4 先查：2050 覆盖成晴后 HUD 晴、雨雪粒子还在下。
- P4 完成（Zhouzhu fbce91d..50bfdab 等 15 个）：调生成器只经 `src/gen_process.*` + `tools/zhouzhu_gen.py`（生成 + 导出一群约 3 s）；世界存档 user://worlds/<世界>/{planet,groups,lods,…}；
  F2「行星」地球仪、T 瞬移 / H 航向；新建随机世界 5.5 s 建好、6.6 s 能走；换锚新规：脚下是别群的地就当场换；存档只存账（人、船、摆放、跑货还不存）。
  P6e 只换 gen_process 这一层（照写同样的文件）。雨雪粒子那处是平滑档不同 + 大步拨钟 + 截图时机，已修并加断言。之后派了 P5。
- P5 完成（Zhouzhu 6276466..1f6340b）：weather.bin 9.5 MB（8000 群逐日天气、无损，包合计 63.1 MB 超预算 3.1 MB）、纯 C++ 行星天气场、云与雨在群间连续、风带推船、永暴带与 G 圈是往外推的风；
  锋面不跨群传播（生成器各群逐日天气彼此无关，要改生成器）；顺手修了 river_e2e 的偶发崩溃（NaN 优先级把任务池排乱）。之后派了 P7，最后是 P6e。
- P7 完成（Zhouzhu 3880862..b34380b）：云带里雾与颠簸、云底下阴天光、SeaSurface 海面网格铺到 300 km（不透明、远处接天空着色器的海球）、浪按行星风、白礁（岛龄 0.38–0.72 算老）、
  船在海上最低离海面 15 m（岛上仍靠碰撞）、坠落线以下按 E 不离舵「海上无处落脚」、y<0 气温取 0 处。之后派了最后一期 P6e（进程内 C++ + 两个导出脚本移 C++，要求与 Python 路径逐字节相同；子模块指向未推送的 skyisle-gen 提交，推送要先推 skyisle-gen）。
- P5 前置（skyisle-gen d92e5d3..ef3525a）：island lod 默认带每群第 0 年逐日天气（npz 里 weather_* 列 + weather_meta 头，键与 export_skyisle 写 weather.tsv 头的对应），全行星 cpp 22 进程 8.5 分钟；
  与 weather_y0.csv 逐行相同、粗版与 P0 逐位相同。P0 备份 out/seed42/islands_lod_p0/（390 MB）没删。
- P6e 完成（Zhouzhu 5329f33..82db923）：子模块 extern/skyisle-gen（指向 ef3525a，只编 core/）与 extern/zlib 1.3.1；行星包多 config.tsv；gen_export / py_format / scipy_edt 把「生成器产物 → 游戏文件」做成纯 C++，与 tools/zhouzhu_gen.py 逐字节相同（97 群 × 7 份 + 3 个新世界 + 游戏里生成的，0 份不同）；GenJob 进程内生成，项目设置 zhouzhu/generator/backend 默认进程内，Python 子进程留作开发时对照；命令行 zhouzhu_gen；一群中位 0.80 s、新建世界 2 s、游戏里 3.6–4.9 s 能走；大群生成临时多约 750 MB；升级子模块后要重导 config.tsv。验证：ctest 5/5、23 条 e2e 全过。
- **收尾状态（2026-09-27）**：计划「全部完成」、Mellos 全部 done。用户说「修复bug，然后推送」→ ⑨ 附庸修了（skyisle-gen aede58a 两后端一起改、⑨ 版本 1→2、新测 test_vassals_found；三 seed 附庸 147/158/164、两后端逐字节同；seed42 只变 polities.json 的 overlord/vassals 与九格表 ⑧），out/seed42 的 ⑨⑩ 用 `--backend python` 重算（保住原缓存 key）。Zhouzhu 子模块升到 b96b83a，行星包重导：planet.bin、weather.bin 只差头里 source 行的提交号（单测 gen_seed42 拿子模块提交号比，**以后每次升子模块都要重导**），config.tsv 别从 python 后端跑过的 run 重导（会写成 backend python）。已推送：skyisle-gen island-rivers-resources-settle → origin（b96b83a），Zhouzhu main → origin 与 mepro（d083be2）。遗留：ME Pro Linux 编过后删 Python 参考后端、锋面跨群、包超预算 3.1 MB、存档只存账、逐位只在本机验过。

**Why:** 以后每期开工都从这份计划和这页地图接着做；改决定要改计划的第三节。
**How to apply:** 用户贴某期提示词或说「继续」时，先读计划对应节与 Mellos 页；做完照第七节收尾（README、fmap 链路 `planet`、Mellos done、计划进度、记忆）。见 [[planet-generator-split-out]]、[[zhouzhu-extend-seasons-weather]]、[[zhouzhu-refactor-stream-loading]]。
