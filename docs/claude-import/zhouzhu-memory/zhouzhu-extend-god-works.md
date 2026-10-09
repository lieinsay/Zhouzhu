---
name: zhouzhu-extend-god-works
description: 拓展「开发者营造」（god 下 F8 直接挖地搭建）已落地的分支、用户拍板与坑
metadata:
  node_type: memory
  type: project
  originSessionId: 1dd1c52e-1314-4660-b82c-fbf922ad3bf9
  modified: 2026-09-28T03:05:06.822Z
---

2026-09-28 在分支 extend-god-works（从 main 9187ed0 切出）上 6 个提交 4bec321..27435a1（当时未推送；后来已并入 main，见文末）。

用户拍板：工具从开发者营造面板挑（不是 god 下换快捷栏）；能力只要「笔刷 + 瞬间挖」——**没要**远距离、无视限制、拉平 / 恢复原样（以后若提，是新的一轮）。

- F8 面板（god 才开）、笔刷 1/3/5/9（[ ] 换）、按一下改一级、按住隔 0.2 s；样式摆完接着拿在手上。
- `held_now()` 是挖垒 / 搭建看的「手上拿着的」；DigHand / PieceHand 不再自己读背包。
- DevTools 每帧把 Player 的 god 拨回它自己的状态：e2e 开 god 必须发 InputEventAction（Input.parse_input_event），Input.action_press 不管用。
- `ctest` 不会自己重建 zhouzhu_site_survey：拉了新代码先 `cmake --build build --target zhouzhu_site_survey`，否则 site_survey_* 三项报「读不到 terrain.bin」。
- earthworks_e2e 拆场景重开时偶发 signal 11（6 次 1 次，当时机器在编译），与这次改动无关的路径，没查。

相关：[[zhouzhu-slice1-voyage]]（e2e 配方）、[[commit-split-by-topic]]

后续（同日）：拓展「Esc 暂停菜单」在分支 extend-pause-menu（建在 extend-god-works 上）61bc198..8de6eab（当时未推送，见文末）。autoload GameMenu 排在 DeveloperTools 前（未处理输入先到后加的节点）；
开发者工具说明在 src/dev_manual.cpp，单测扫 input_actions.h 里「// 开发者：」注释的动作——**加新开发者工具要写那行注释并在 dev_manual 添一条**。
e2e 里要测暂停：测试节点 ALWAYS、场景显式 PAUSABLE（不然场景继承 ALWAYS，不停）。

2026-09-28 更新：两段都已快进并入 main（8de6eab）并推送 origin 与 mepro；本地所有旧分支（slice-*、extend-*、refactor-*）都已并入 main 后删除，别的记忆里提到的分支名已不存在，看 main 的历史。
之后的 refactor-refit-drag、extend-ship-cabins 也已并入 main（70be1f4）并推送，这两个本地分支还没删（2026-09-29 查）。
