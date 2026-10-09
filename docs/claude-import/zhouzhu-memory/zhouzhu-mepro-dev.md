---
name: zhouzhu-mepro-dev
description: Zhouzhu 后端在家用服务器 ME Pro 上开发：连法、必须走代理、git hub 位置
metadata:
  type: project
---

Zhouzhu 的 C++ 后端在 ME Pro（零刻 i5-13500H / Debian 13 / 32G / 16 线程）上开发，2026-09-18 搭好。

- **连法**：先连 WireGuard，然后 `ssh liein@10.8.0.12`（免密）。`192.168.0.116` 只在家里局域网可达；`ssh mepro` 别名只在家里那台 Windows 上有，公司电脑没有。
- **必须走代理**：ME Pro 直连 `claude.ai` / `downloads.claude.ai` 返回「App unavailable in region」。已在 `~/.bashrc` 的 `ZHOUZHU-DEV-ENV` 段里配好 mihomo（`http://mepro:<密码>@127.0.0.1:7890`，密码从 `/srv/mihomo/.proxy-pass` 读，不写死）。mihomo 是规则代理，国内域名自动 DIRECT，所以全局设也不影响 apt/tuna。
- **仓库**：内网 hub 是 `/srv/git/zhouzhu.git`（裸库，走 WG 免认证）；Windows 侧 remote 名 `mepro`。GitHub `lieinsay/Zhouzhu` 仍是 `origin`，异地备份。ME Pro 工作树在 `~/dev/Zhouzhu`。
- ME Pro 上 GitHub 走 `ssh.github.com:443`（22 端口不通），密钥 `~/.ssh/id_ed25519_github`，公钥待贴到 GitHub 账号。
- 常驻 tmux 会话名 `zhouzhu`，断线不丢进度。

相关：[[zhouzhu-core-architecture]]、[[zhouzhu-console-utf8]]
