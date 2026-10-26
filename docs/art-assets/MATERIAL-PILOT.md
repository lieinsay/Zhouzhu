# 共享材质小样 v1

2026-10-26。四张基础色分别由内置 image_gen 生成，原图、完整提示词、源文件指纹和512像素派生保存在 `assets/art/materials/` 与正式登记中。只使用 baseColor；不从彩色图片推算法线、AO、金属度或粗糙度贴图。

| 身份 | 图像代表尺度 | 当前接入 |
| --- | --- | --- |
| material.wood | 0.5 × 0.5m | SharedLooks 的 FLOOR 木楼板 |
| material.rammed_earth | 1 × 1m | SharedLooks 的 WALL 夯土墙 |
| material.terracotta | 0.5 × 0.5m | SharedLooks 的 CLAY 陶土表面 |
| material.iron | 0.5 × 0.5m | 仅受控样件；图片位于游戏目录外，正式绑定禁止选择 |

输入尺度来自这批图的制作规格，是美术映射尺度，不是地形、经济或生产数据。局部米制三平面映射在曲率变形前取样，保持相机和对象移动时的表面身份。纹理按线性色均值归一后调制原材质基色，限幅0.6–1.4；不更改既有配色为任意生成色。正式三张采用mipmap与各向异性过滤，原图不重绘。

湿润输入沿用运行时 `Atmosphere` 设置的 `wx_wet`。本轮基础色在干→湿时乘1→0.88、粗糙度乘1→0.72，是有界的美术插值参数，并非实测材料模型，不改变天气、水量、时间序列或生成器数据。金属反光由样件明确的metallic/roughness标量产生，不烘进图像。

代理实际查看四张原图、2×2重复图及游戏样件的近景、换向光、湿润和远景图。三种正式基础色通过无头、Vulkan像素与真实房屋材质检查；真实房屋找到三个对应纹理路径。四种样件的尺度、纹理身份和缺图素色回退通过；湿润前后采样7256处像素发生可见变化。铁只在样件内临时读取，未进入正式绑定或PCK。

一次补充建筑验收因脚本作用域错误超时150秒，已保留失败日志、修正，并先通过无头检查后完成受硬超时保护的像素复验。最终运行退出0、无脚本/着色器错误。

仍保留的缺口：木梁、家具等逐构件纹理方向与接合需要模型/UV配合，因此未扩大木纹绑定；铁尚无统一的现有实体材质语义，不按颜色猜绑；尚无法线等其他贴图通道；其他材料、地表、植被与人物按实际依赖继续。26张预览v1保留其生成时的几何与原基色快照，不宣称包含这批材质小样的全部表现。旧浮石舱青色自发光、方盒造型仍与普通矿物方向不符，列入模型材质缺口，不当作新风格完成。

新增版本用 `python tools/art-assets.py register-material --help`，铁版本仍自动限定为样件。使用工坊 `/art.html` 选择版本、暂存、独立应用；游戏重启后读取。样件场景为 `res://tests/art_material_e2e.tscn`。

证据：[近景](../../output/art-production-20261026/material-game/01_dry_near.png)、[变光](../../output/art-production-20261026/material-game/02_changed_light.png)、[湿润](../../output/art-production-20261026/material-game/03_wet.png)、[远景](../../output/art-production-20261026/material-game/04_far.png)、[真实房屋](../../output/art-production-20261026/preview-materials/02_furniture_bed.png)、[样件报告](../../output/art-production-20261026/material-game/report.json)。
