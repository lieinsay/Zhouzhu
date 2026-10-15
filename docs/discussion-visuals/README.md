# 河流与水完整讨论：分类动画入口

2026-10-15用户确认全部七层完整方案、各轮补充及水色/做功调整，讨论结束。本目录包含8组可直接离线查看的HTML说明页与84份独立SVG；HTML支持分类播放、暂停、拖动与分步查看，各SVG可独立循环。

| 设计范围 | 文档 | 动画入口 | 独立SVG |
| --- | --- | --- | --- |
| 第一层：成因、蓄积与外流总纲 | [完整总纲](../DISCUSS-RIVERS-WATER.md) | [第一层](first-layer/index.html) | 11 |
| 第一层：长期保存与有限更新 | [长期保存](../DISCUSS-RIVERS-WATER-LAYER1-LONGEVITY.md) | [长期保存](first-layer-longevity/index.html) | 7 |
| 第二层：水系、库存与水情 | [第二层](../DISCUSS-RIVERS-WATER-LAYER2.md) | [第二层](second-layer/index.html) | 10 |
| 第三层：床岸与宽深形态 | [第三层](../DISCUSS-RIVERS-WATER-LAYER3.md) | [第三层](third-layer/index.html) | 11 |
| 第四层：数据与几何契约 | [第四层](../DISCUSS-RIVERS-WATER-LAYER4.md) | [第四层](fourth-layer/index.html) | 10 |
| 第五层：视觉深度与尺度验收 | [第五层](../DISCUSS-RIVERS-WATER-LAYER5.md) | [第五层](fifth-layer/index.html) | 10 |
| 第六层：玩家改地与引水 | [第六层](../DISCUSS-RIVERS-WATER-LAYER6.md) | [第六层](sixth-layer/index.html) | 12 |
| 第七层：用水、交互与水流做功 | [第七层](../DISCUSS-RIVERS-WATER-LAYER7.md) | [第七层](seventh-layer/index.html) | 13 |

最终有效边界：成分以轻量基础水色表达，复杂成分/浓度/反应/分离暂缓；水量、真实通路、必要地貌材料、同版几何/水态与统一时间保留。设备取能有实际机械能来源和反馈，大型水利不能只从名义Q计算固定产出。生态环境与生物演化仍以后另做。

后续代码工作见[分阶段执行方案](../PLAN-RIVERS-WATER.md)：P00—P12均未执行，按依赖、交付物、代码入口和验收门槛推进；本次仅归档设计与形成计划。

所有图形、颜色、箭头与秒数均为非比例因果说明，不是当前游戏、生成器产物或物理/数值模拟结果。设计通过不等于参数、源数据、真实引水、受力、功率或47米原始现场验证已经完成。

各子目录README记录整理阶段已做的说明材料检查及其限制。其提及的本机`build/`临时作者脚本、检查日志和截图不随本目录发布，也不是动画的运行依赖；不会因没有这些临时文件而需要启动服务、游戏或重生成世界。
