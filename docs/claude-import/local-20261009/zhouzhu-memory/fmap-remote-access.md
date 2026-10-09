---
name: fmap-remote-access
description: FeatureMap 查看器在家里这台机器上同时监听 10.8.0.3（WG），靠用户环境变量 FMAP_HOST
metadata:
  node_type: memory
  type: project
  originSessionId: 5f8958ae-8e3c-470b-bf6b-fabcc2654c45
  modified: 2026-09-27T12:18:03.576Z
---

2026-09-27 用户要 FeatureMap 能从 10.8.0.3 访问。featuremap 仓库（家里 D:\Project\featuremap，远端 mepro /srv/git/featuremap.git）提交 02f3967、4c45414 加了 `--host` / `FMAP_HOST`：127.0.0.1 总在，另加列出的网卡；不接受 0.0.0.0。已设**用户环境变量 FMAP_HOST=10.8.0.3**。`fmap open` 在跑的服务缺了要的地址才停掉重起，只加不减。

**Why:** 用户要从 WG 上的别的机器看链路图；服务 6 小时没人访问会自己退出，下次 `fmap open` 重新拉起时要带上 10.8.0.3。
**How to apply:** 给用户链接时，把 `fmap open` 输出里「别的机器：http://10.8.0.3:4517/…」那行也一起给。如果 Claude Code 进程启动得比设这个变量早，环境里就没有 FMAP_HOST，这时冷启动的服务只听本机；输出里没有「别的机器」那行时，先 `$env:FMAP_HOST='10.8.0.3'` 再 `fmap open`。已从 ME Pro 用 curl 验过（防火墙放行）。两个提交都还没推送。
