# 州渚（暂定名）

一款以浮空岛屿为舞台的经营 / 探索 / 社交游戏。世界正从宗法分封走向编户齐民：
你是边陲小邦的次子，靠一艘小空艇跑船为生，会看着自己出身的那套秩序被一套更有效率的秩序取代，
并且不得不承认，取代它的东西没那么坏。主题是「便利与失去是同一件事」，不作褒贬。
除浮石（它只解释为什么是岛、为什么飞）之外，没有任何超自然要素。

## 切片一：走上空艇，从一座岛飞到另一座

```
cmake -B build && cmake --build build --config Debug      # 扩展 → game/bin/
"D:\Program Files\Godot\Godot_v4.4.1-stable_win64.exe" --path game
```

步行：WASD 移动、鼠标转视角、空格跳、Shift 疾跑；走到舵旁按 E 掌舵。
掌舵：W/S 进退、A/D 转舵、空格升、Shift 降；离泊位 45 m 内按 E 自动靠泊并离舵。Esc 放开鼠标。

岛是运行时按种子生成的（`src/island_field.*`，算法移植自 skyisle-gen 的 `island/terrain.py`，缩到游戏尺度），
在 `game/main.tscn` 里改 `Island` 节点的属性即可换岛。

```
ctest --test-dir build -C Debug --output-on-failure        # 纯算法单测（噪声 / 岛屿高度场 / 飞行模型）
Godot --headless --path game --fixed-fps 60 res://tests/voyage_e2e.tscn    # 整条航程，只经输入动作驱动
Godot --headless --path game --fixed-fps 60 res://tests/respawn_e2e.tscn   # 坠入云海 → 回位 + 召回空艇
```
