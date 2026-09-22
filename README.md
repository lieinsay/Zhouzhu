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

## 切片二：在河边开垦、播种、收获

出发岛上有一条河（`Island.has_river`）：腹地的泉眼顺地势流到岸缘，跌进云海。岛缘是一圈陡坡，
所以码头后面修了一条土路通到河滩——下了船顺着土色的路走一分钟不到就是河。

河两岸压平的滩地能种地。面朝一格地（视线前方约 2 m，能干活的格子会描出亮边）**按住 E**：
荒地 → 开垦（1.5 s）→ 播种（0.6 s，耗手上那种种子 1 份）→ 等它长（约 1–3 分钟，离河越近墒情越好、熟得越快）→ 收获（0.8 s，收成进背包）。
收完的地还是垦好的，接着种。离河太远、坡太陡、河里、路上、停机坪上都垦不了，按 E 会告诉你为什么。

### 背包与种子

种子从背包里选：**Tab**（或 I）打开背包，W/S 挪光标，E 把那样种子拿在手上（再按一次放下），Tab 合上。
开着背包时人站住不动。手上没种子时对着垦好的地按 E 会提醒你去背包选。
开局背包里有粟 5 份、菽 3 份，都是「收成即种子」：粟一格得 4，菽长得慢一半、一格得 3。
左上角第二行是背包一览和手上拿着什么；收获时收成按地里种的作物进背包。

地块就是岛屿高度场的格子（2.5 m），规则在 `src/farm_model.*`（纯算法，地块记着种的作物），河与路在 `src/island_river.*`，
物品与作物表在 `src/crops.*`、背包在 `src/inventory.*`（都是纯算法）；
场景里是挂在 `Island` 下面的 `Farm` 节点（基准生长时间、引水距离是它的属性），
开局物品是 `Voyage` 的 `starting_pack` 属性。

## 测试

```
ctest --test-dir build -C Debug --output-on-failure        # 纯算法单测（噪声 / 岛屿高度场 / 河与路 / 背包 / 作物表 / 农田 / 飞行模型）
Godot --headless --path game --fixed-fps 60 res://tests/voyage_e2e.tscn    # 整条航程，只经输入动作驱动
Godot --headless --path game --fixed-fps 60 res://tests/respawn_e2e.tscn   # 坠入云海 → 回位 + 召回空艇
Godot --headless --path game --fixed-fps 60 res://tests/farm_e2e.tscn      # 沿路走到河滩 → 开垦 → 没种子被拒 → 背包里选菽 → 播种 → 成熟 → 收进背包
```
