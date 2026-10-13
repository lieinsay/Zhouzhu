# 真实模型导出

在 Zhouzhu 根目录运行：

```powershell
node tools/content-studio/model-export/export-models.mjs
```

也可指定仓库与资源输出位置，或覆盖 Rider 记录的 Godot 路径：

```powershell
node tools/content-studio/model-export/export-models.mjs --root D:/Project/My/Zhouzhu --output D:/Project/My/Zhouzhu/tools/content-studio/public/assets/models --godot "D:/Program Files/Godot/Godot_v4.4.1-stable_win64.exe"
```

需要本机现有 Rider MSVC x64 / Ninja / Debug 缓存、已生成的 `godot-cpp` 静态库和游戏 Debug DLL。命令核对缓存的实际编译器与 Ninja 包含前缀，再在本工具 `.build` 中编译两个独立产物；不会运行主项目 CMake、重建游戏 DLL或写游戏资产。编译输入指纹相同会复用此次工具构建，GDScript 修复不触发 C++ 重编。

导出路径如下：

- 11 个搭建件、5 种建筑：独立 C++ 程序直接调用当前 `build_piece_mesh` / `build_building_mesh`。建筑读取当前房屋模板，以默认规则勘察零高程平地。山墙使用相邻真实瓦坡顶提供形状，只输出山墙自身。
- 5 个放置物、20 个散布原型与矿种等子类：工具专用 GDExtension 直接编译当前 `build_prop_model` / `ScatterMeshes` 源码，读取生成后的真实节点和表面数组。
- 8 个船模块：从现有游戏 DLL 按合法参考布局生成的真实模块节点截取。包含模块自身船体、甲板和陈设；全船共用船首、连接梯子不计入模块。

Godot 在本工具 `runtime` 项目无头执行，没有游戏自动加载单例、地图、世界数据或存档路径；子进程的用户数据目录也隔离到工具工作区。默认超时 60 秒，日志及退出码留在 `.build/godot-result.json`，不会新开可见游戏窗口。

产物使用米制 `mesh-json`：

```json
{
  "schemaVersion": 1,
  "units": "m",
  "winding": "ccw",
  "parts": [{
    "positions": [0, 0, 0],
    "normals": [0, 1, 0],
    "indices": [0, 1, 2],
    "color": [0.55, 0.39, 0.23]
  }]
}
```

示例仅说明字段；实际数组须具有完整三角面。节点变换已烘入顶点，法线使用逆转置并归一化，原 Godot 顺时针索引转换为 WebGL 逆时针。曲率着色器的原材质色、粗糙度与自发光参数保留；散布的平涂顶点色和确定性实例色合并到各材质分组。叶片顶点 alpha 是分类标记，不能当透明度。独立预览显示静态原始网格，不包含世界曲率、季节、积雪或风摆。

`manifest.json` 记录每个模型的用途、对应记录、网格 SHA-256 和完整源码/模板/着色器指纹。全部生成依赖在编译前读取，导出后逐一核对；游戏 DLL、项目配置和主构建缓存也核对不变。船模型来源是既有 DLL，附当前实现源码指纹用于定位，不声称本轮重新编译了该 DLL。

49 个模型不等于 49 个道具库存模型：采集源以 `usage: gather-source` 标明用途；粟、菽等 14 个没有本轮可用独立库存实体的道具列在 `missingItems`，不会补造几何。

定向检查：

```powershell
node --test tools/content-studio/test/model-export.test.mjs
```

检查真实目录覆盖、索引/法线面向、米制字段、资产及来源 SHA、采集源用途和明确的模型缺口。
