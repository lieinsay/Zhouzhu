---
name: zhouzhu-extend-seasons-weather
description: 拓展「时令与天气」（日历、日月、锋面+云团+海拔的天气、Atmosphere 天与光、季相着色器、村子作息歇工冬闲、庄稼按气温、风推船、F6 面板）2026-09-26 在 main 上 35d1bea..b09d58d，2026-09-27 已推送；约定与坑
metadata:
  node_type: memory
  type: project
  originSessionId: e854470c-c3d8-45a6-8052-1ce4de69478a
  modified: 2026-09-26T14:03:11.448Z
---

2026-09-26 在家里 Windows 上直接在 main 上做（[[work-on-main]]），提交 35d1bea..b09d58d（12 个），2026-09-27 随行星计划 P0 一起推送 origin 与 mepro。Mellos 页 `extend-seasons-weather`；fmap 事件 e50–e61、新链路 `seasons-weather`。

用户原话：「1.补充所有受时间影响的事务的功能；2.实现天气系统……积雪雪线其实是天气系统的事情；不同地区的天气可能不一样，你需要设计一下，天气过渡问题；3.实现一个开发者工具，能自由的调整时间天气；其中默认天气是这个时间地点该有的天气」。

- 设计：一游戏日 = 一历日（一年 336 天、一天 40 分钟，所以平常玩感觉不到季节，靠 F6 拨）——用户没拍板这个比例，是我定的默认，汇报里提过。
- 日历在 `WorldClock`：历日 = 钟的日 + 整日偏移（`calendar_start` 3 + `calendar_shift`）。拨日子只动偏移；往回拨钟点 = 偏移退一天 + 钟往前走，所以钟的秒只往前、村子账只往前结。`World.calendar_offset` 已删。
- 天气纯算法 `src/weather.*`：锋面（子夜前后按天抖、顺风 25 km/游戏钟头扫过、过渡 2.5 h）+ 云团（fbm 场随风漂，C++ 与 `game/shaders/weather.gdshaderinc` 逐式相同，单测对 numpy 参照）+ 海拔；慢变量按海拔分箱逐日一年。开发者覆盖：`WeatherKind::COUNT` = 只偏气温；下雨下雪的覆盖让雨区铺满。
- 全局着色器参数必须在 `project.godot` 的 `[shader_globals]` 里声明；天空着色器的内建是 `AT_CUBEMAP_PASS`（不是 AT_CUBEMAP）；无头模式不编着色器，查着色器错误要开窗口跑几秒看日志。
- 坑：DevTools 若每帧把自己的覆盖推给 Atmosphere，会冲掉测试 / 工具直接设的——现在只在设定变了或换了场景时推。
- 坑：截图工具把相机和 god 模式的人放在同一点，相机在人的模型里，地平线上方会出一排红黑短线——人放到相机下面几米。
- 坑：river_e2e 后半段人的位置本来就会 NaN（原有），NaN 进天气模型当下标会段错误——取样入口已挡 NaN。
- 坑：一天 40 分钟、人走 1.3 m/s（钟的秒），邑治大，走回家要几个游戏钟头；测「避雨」要查「外面的人都在往家走」，不是「全进门」。
- 验证：ctest（core 1109 + 体检）+ 18 条 e2e（新 weather_e2e）+ `game/tools/weather_shots.tscn -- --shots=<目录>` 目视；体检报一年的天气表。

**Why:** 下一步（推送、雪落房顶、村民打伞、多年天气、一年长短档位）要从这些约定接着做。
**How to apply:** 新的「随时间变」的东西读 `GameClock.get_calendar_day()` 与地图宿主的 `map_weather()` / `host_weather()`；村子要远近一致就走 `LocalSeason`。见 [[zhouzhu-slice7-world-sim]]、[[zhouzhu-extend-rivers]]、[[zhouzhu-refactor-stream-loading]]。
