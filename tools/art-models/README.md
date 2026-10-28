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

## 家具、货箱与独立实体应用

`prepare_furniture.py`在同一便携Blender后台运行，读取当前家具定义和已归档的原生模型，适配已存Poly Haven作者glTF；输出`build/prepared-furniture/`。原生精修保留占地和用途，水缸必须开口有内壁，不能直接把带盖壶当成水缸。模型材质通过项目着色器统一，不烘入方向性阳光。

准备文件经`externalize_textures.py`拆出原作者贴图，在新的`game/art/models/<namespace>/<id>/vN/`形成GLB与侧车纹理。试接入可显式传`-- --model-bindings=res://art/model-bindings-review.json`，仅接受art目录内JSON。此文件不进正式版本；普通启动仍读`model-bindings.json`。`furniture_models_e2e.tscn`检查13家具与统一货箱的实际GLB、占地、生产材质，并在获准的GPU窗口导出512透明预览。定义宽深是占地上限，不为填满占地拉伸原有器物。

审图后`python tools/art-models/publish_furniture.py --review <验收目录> --model-version vN --image-version vN`登记真实编辑源/GLB/依赖、作者许可和同源家具预览，不覆盖已有版本，也不会自动应用。项目改编与自有精修不会被标成外部作者原作或自动转授CC0。再到工坊正式美术页分别暂存、应用三维实体与图片。实际房屋、剖层/摆放预览、玩家货箱与船上货位读取相同实体身份；碰撞仍来自原有定义。
