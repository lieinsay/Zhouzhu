# 美术制作与接入验收

## 首批八项

`millet`、`copper_ore`、`stone_axe`、`chest`、`pottery`、`cloth`、`floatstone`、`roof_tile` 各自使用内置 image_gen，正式版本 v2；保留旧 v1、完整提示词、风格参考指纹和受控缩放派生。粟首次候选因穗部近似麦穗被拒，保留候选和拒绝理由，重制为干燥粟粒。

代理已查看八张原图、明暗底 32/48/64 像素表，主体语义、透明边缘、尺度留白和小尺寸辨识通过。用户确认方向与推进授权，未逐张确认。

工坊经真实 HTTP `/api/art/stage`、`/api/art/apply` 应用8项，内容草稿原字节保持；正式页已在浏览器查看，主题对比问题修正。代码按现有 Rider MSVC14.38 / Ninja / Debug、英文包含前缀 `VSLANG=1033` 编译，退出0；手册11项、0失败；Python14项、相关前端17项通过。

真实 `world_seed42.tscn` 开局场景使用独立测试存档。无头检查退出0，八项均从正式绑定加载，拖拽、快捷栏数量/选择态、船舱搬运、共享纹理身份、未绑定回退通过。用户授权后打开一次可见验收窗口，按真实时间顺序截图后自动关闭，Vulkan / RTX4070 运行退出0。实际查看背包、快捷栏、船舱截图，数量与状态覆盖清楚，未改天气或生成器数据。

使用 PCKPacker 打包实际导入纹理、导入映射和绑定 JSON，在没有美术源目录的隔离 Godot 项目中加载同一游戏 Debug DLL，八项 C++ 加载及缺图回退通过，退出0。未运行全游戏发布导出，不将此检查表述为完整发布包验收。

证据：[明底小图](../../output/art-production-20261026/first-batch-review/light.png)、[暗底小图](../../output/art-production-20261026/first-batch-review/dark.png)、[游戏背包与配方](../../output/art-production-20261026/game-first-batch/01_pack_and_recipes.png)、[快捷栏](../../output/art-production-20261026/game-first-batch/03_hotbar.png)、[船舱](../../output/art-production-20261026/game-first-batch/04_container.png)、[无头日志](../../output/art-production-20261026/game-headless.log)、[像素验收日志](../../output/art-production-20261026/game-first-batch.log)、[隔离PCK日志](../../output/art-production-20261026/pack-probe.log)。

## 后续批次

首批流程已通过，可以分批扩展；其余物品、模型预览与材质的完成情况后续追加，不将工坊可见当作游戏已验收。

41 项旧 v1 已逐项检查原图总览与明暗底32/48/64，登记各自沿用理由，保留原字节与旧提示词。经14/14/13项三批应用后，物品覆盖49/49（8新制、41复用）。正式派生均为256透明方图；49项原图和派生SHA逐一核对，真实开局无头加载49项通过，退出0。所有 v1/v2 均未标记用户逐项确认。物品图标沿用不代表模型外观已经完成风格化。

## 模型预览批次

当前DLL重新导出49个模型到独立新版本快照，13类家具直接复用当前纯C++几何导出；旧工坊模型库保持原字节。统一相机/光照生成8模块、5建筑、13家具共26张真实预览，并保留模型JSON、来源指纹、米制定义、相机与派生参数。已查看全部26张总览；代理审图仅确认当前模型快照准确，不确认精绘模型完成。具体缺口见 [model-gaps.json](model-gaps.json)。

26项已通过独立美术API应用，正式绑定共75个身份。实际B建筑、T室内家具、画样台E改装入口的无头及Vulkan像素检查均退出0，验证26项纹理和命名空间隔离。已查看三张游戏截图；模块仍保留程序占格图和功能文字，建筑显示真实尺寸，家具显示当前所选工具的模型。模型中的方盒水缸/鼓/麻袋等简化形体明确保留为缺口。

证据：[模块总览](../../output/art-production-20261026/model-previews/page-1.png)、[游戏建筑面板](../../output/art-production-20261026/preview-game/01_buildings.png)、[家具工具](../../output/art-production-20261026/preview-game/02_furniture_bed.png)、[改装卡片](../../output/art-production-20261026/preview-game/03_modules.png)。
