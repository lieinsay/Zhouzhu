param([string]$PythonExecutable)
# 管理员 PowerShell：仅为内容工坊放行用户已授权的 Work VPN 网段。
$ErrorActionPreference = 'Stop'
$studioPython = if ($PythonExecutable) { (Resolve-Path -LiteralPath $PythonExecutable).Path } else { (Get-Command python -CommandType Application | Select-Object -First 1).Source }
if (-not $studioPython) { throw '未找到 Python，请先配置 python 命令。' }
$studioRuleName = 'Zhouzhu-ContentStudio-Python4176-WorkVPN'
$studioExistingRule = Get-NetFirewallRule -Name $studioRuleName -ErrorAction SilentlyContinue
if ($studioExistingRule) {
    throw '同名规则已存在，请核对现有规则；脚本不会覆盖已有防火墙配置。'
}
New-NetFirewallRule -Name $studioRuleName -DisplayName 'Zhouzhu Content Studio Python 4176 Work VPN' -Direction Inbound -Action Allow -Protocol TCP -LocalPort 4176 -LocalAddress 10.8.0.2 -RemoteAddress 10.8.0.0/24 -Program $studioPython -Profile Any
