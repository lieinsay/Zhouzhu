# 作者模型与蒙皮流程

本工具适配外部作者的真实网格、UV、蒙皮与动作；不生成球柱人体，不把设定图当三维实体。来源与阶段验收见`docs/art-assets/MODEL-UPGRADE-20261027.md`。

原始来源保存于`assets/art/external/`，Blender编辑源与派生记录保存于`assets/art/models/`，运行时模型使用`game/art/models/`。独立实体绑定为`game/art/model-bindings.json`，与图片绑定和`project.visuals`玩法草稿分开；现有定义ID及命名空间继续复用。

1. 检查发布者许可和文件内元数据，下载原包、保存SHA和出处。不把“可商用”自动解释为可随游戏分发，排除无相应授权的作品。
2. 用已核对的便携Blender执行`--background --factory-startup --disable-autoexec --python tools/art-models/prepare_actors.py`。脚本保留作者人体拓扑和权重，按骨骼绑定姿态与长度适配裤装，去除不可见内层身体，重定向CC0作者动作，并输出可编辑blend、GLB与参数记录。不得再将蹲伏Idle通过动作名后缀误选为站立Idle。
3. `python tools/art-models/externalize_textures.py <准备GLB> <新版本运行时GLB>`按原字节拆出图片，便于指纹与PCK依赖核对，不重绘贴图，不改变几何/蒙皮/动画缓冲。
4. 按新版本登记，先无头检查`res://tests/actor_models_e2e.tscn`，再在已获用户允许的GPU窗口中审看待机、行走、跑动、职业握姿和衣物。`actor_world_e2e.tscn`使用原开发场景和实际世界状态；普通NPC在室内隐藏时不能把空镜头记为人物像素通过。
5. 用`item_art_pack.gd`打包实际导入资源，在没有art源目录的隔离项目中确认`imported_model=true`、服装蒙皮和作者贴图加载；只通过旧平面脸回退不能算模型PCK通过。

本机工具沿Rider MSVC14.38/Ninja/Debug，实际英文包含前缀配VSLANG=1033。便携Blender4.5.3从官方发布目录取得，仅放项目忽略的`build/model-tools`，不更改公用Python或生成器环境。

Godot4.4.1无头GLTF导入在生成**编辑器场景缩略图**时会报`Parameter "t" is null`，见官方问题108994。它不表示作者贴图/模型数据丢失，也不能据此把整个导入日志标作无错误。保留该日志，实际加载、权重、GPU像素和PCK验收分别记录；不为绕过此问题切换项目Godot版本。
