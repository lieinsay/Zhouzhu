# 正式美术库

`registry.json` 是正式资源身份与不可覆盖版本登记；`items/<id>/vN/original.png` 保留新生成原图。旧 v1 原图及完整来源继续位于内容工坊原目录，不复制覆盖。`candidates/` 保存有意义的未采用候选。

游戏只读取 `game/art/bindings.json` 与其中指向的 `game/art/items/<id>/vN.png`。256 方图从原图裁透明边距、按预乘透明 Lanczos 缩放至统一主体尺度；不重绘内容。工坊经 `/api/art/image/...` 读取同一正式派生文件。`applications/` 保存每次美术应用前后快照，不包含内容草稿。

制作使用内置 image_gen；登记/审图/小尺寸图表：`python tools/art-assets.py --help`。制作脚本需要 Pillow，服务端仍只需标准库。完整规格见 [美术规范](../../docs/ART-DIRECTION.md)。

代理审图通过不等于用户逐项确认。工具预览、游戏绑定、运行验收分别记录。应用只写美术绑定，游戏重启后读取；不会保存存档、应用 project.visuals 或修改内容数值。
