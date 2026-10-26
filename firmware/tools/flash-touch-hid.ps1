param([Parameter(Mandatory=$true)][string]$Port)
$ErrorActionPreference='Stop'
Push-Location (Split-Path $PSScriptRoot -Parent)
try {
    $record=Get-Content TOUCH_HID_IMAGE_RECORD.json -Raw | ConvertFrom-Json
    $image=Join-Path $PWD $record.image_file
    if((Get-FileHash -LiteralPath $image -Algorithm SHA256).Hash.ToLowerInvariant() -ne $record.sha256) { throw 'Firmware SHA256 mismatch' }
    if((Get-Item -LiteralPath $image).Length -gt 0x400000) { throw 'Application exceeds ota_0' }
    $device=Get-PnpDevice -PresentOnly -Class Ports | Where-Object { $_.FriendlyName -match ('\('+[regex]::Escape($Port)+'\)$') -and $_.InstanceId -match 'VID_1A86' }
    if(!$device) { throw 'Selected port is not a connected CH340; inspect cabling and port first' }
    . (Join-Path $PSScriptRoot 'activate-idf541.ps1')
    python -m esptool --chip esp32s3 --port $Port --baud 460800 --after hard_reset write_flash 0x20000 $image
    if($LASTEXITCODE -ne 0) { throw 'Application flash failed' }
} finally { Pop-Location }

