# 缩略图来源

本目录记录内置 `image_gen` 生成的提示词和最终原图 SHA-256。每个当前游戏道具分别调用生图，图像是内容工具使用的设计缩略图，不代表对应物品已有独立游戏模型。

最终 PNG 原图保存在 `../public/assets/thumbnails/<道具ID>-v1.png`。透明背景、主体、完整 49 个 ID、生成记录和文件指纹由以下命令检查并建立清单：

```powershell
node tools/content-studio/assets-source/build-thumbnails.mjs
```

该脚本只读图像像素和元数据，输出旧缩略图 `manifest.json`，不编辑、裁切或缩放生成原图。提示词与原图均按版本保留。2026-10-26 起正式美术登记在 `assets/art/registry.json`；游戏与工坊共享其规范化派生图，原 v1 文件不覆盖。新制作使用 `tools/art-assets.py`，工坊 `/art.html` 独立应用美术，不应用内容草稿。


2026-10-27：旧49张v1与本目录来源继续保留。当前场景正式图片、完整提示词和风格参照归档见 assets/art/registry.json、assets/art/materials/source-prompts-scene-20261027.json、assets/art/characters/source-prompts-20261027-v2.json 与 assets/art/styles/painted-jrpg-3d-v1/manifest.json。101项实际绑定和分阶段验收见 assets/art/acceptance/20261027.json；当前同源模型快照由 assets/art/model-library.json 指向新版本，旧public模型不覆盖。
