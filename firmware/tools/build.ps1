param([string]$BuildDirectory = 'build-touch-hid')
$ErrorActionPreference = 'Stop'
Push-Location (Split-Path $PSScriptRoot -Parent)
try {
    . (Join-Path $PSScriptRoot 'activate-idf541.ps1')
    python "$env:IDF_PATH\tools\idf.py" -B $BuildDirectory build size
    if ($LASTEXITCODE -ne 0) { throw 'TouchDesk build failed' }
} finally { Pop-Location }
