param([string]$Port='COM7')
$ErrorActionPreference='Stop'
Push-Location (Split-Path $PSScriptRoot -Parent)
try {
    . (Join-Path $PSScriptRoot 'activate-idf541.ps1')
    $record=Get-Content IMAGE_RECORD.json -Raw | ConvertFrom-Json
    if((Get-FileHash restore-original.bin -Algorithm SHA256).Hash.ToLowerInvariant() -ne $record.original_app_sha256) {
        throw 'Restore image SHA256 mismatch'
    }
    python -m esptool --chip esp32s3 --port $Port --baud 460800 --after hard_reset write_flash 0x20000 restore-original.bin
    if($LASTEXITCODE -ne 0){throw 'Restore failed'}
} finally {Pop-Location}
