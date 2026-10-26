$ErrorActionPreference = 'Stop'
$pythonw = 'C:\Users\libai\miniconda3\envs\pyqt\pythonw.exe'
if (!(Test-Path -LiteralPath $pythonw)) { throw '找不到 conda pyqt 环境，请修改 start.ps1 的 pythonw 路径。' }
Start-Process -FilePath $pythonw -ArgumentList @(('"' + (Join-Path $PSScriptRoot 'serial_host.py') + '"')) -WorkingDirectory $PSScriptRoot -WindowStyle Normal
