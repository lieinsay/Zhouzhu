---
name: fmap-viewer-on-wg
description: fmap 查看器挂在 WireGuard 地址 10.8.0.2:4517 上给用户在别的机器看；靠用户环境变量 FMAP_HOST
metadata:
  node_type: memory
  type: project
  originSessionId: 3bf338ea-5e68-4b13-ab02-f1421ab720e3
  modified: 2026-09-29T03:42:15.406Z
---

2026-09-29 用户要自己开 featuremap 并「放到 10.8.0.2」（这台 Windows 机器的 WireGuard 地址，网卡名 Work）。
FeatureMap 加了 `--host` / `FMAP_HOST`：总在 127.0.0.1，再额外挂这些地址。提交 4c275c3 在 FeatureMap 的
`serve-host` 分支上，未并 main、未推送；全局 `fmap` 是链到工作区的，所以当前就生效。
用户环境变量 `FMAP_HOST=10.8.0.2` 已写入（新开的进程才看得到）。

**Why:** 服务是单实例、6 小时没人访问会自己退；下次被一个没带 FMAP_HOST 的进程拉起来就只剩本机。

**How to apply:** `fmap open` 输出里没有「别的机器用这个」那行、或出现 ⚠ 没挂在 10.8.0.2 → `fmap stop` 再
`fmap open --host 10.8.0.2`。防火墙三个配置文件全关，不用加规则。ME Pro 上 `curl http://10.8.0.2:4517/api/ping` 可验。
相关：[[zhouzhu-mepro-dev]]
