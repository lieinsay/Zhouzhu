# 缩略图来源

本目录记录内置 `image_gen` 生成的提示词和最终原图 SHA-256。每个当前游戏道具分别调用生图，图像是内容工具使用的设计缩略图，不代表对应物品已有独立游戏模型。

最终 PNG 原图保存在 `../public/assets/thumbnails/<道具ID>-v1.png`。透明背景、主体、完整 49 个 ID、生成记录和文件指纹由以下命令检查并建立清单：

```powershell
node tools/content-studio/assets-source/build-thumbnails.mjs
```

该脚本只读图像像素和元数据，输出缩略图 `manifest.json`，不编辑、裁切或缩放生成原图。提示词与原图均按版本保留；游戏不加载这些资产。
