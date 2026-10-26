param([Parameter(Mandatory=$true)][string]$Port)
$ErrorActionPreference='Stop'
Push-Location (Split-Path $PSScriptRoot -Parent)
try {
    $image=Join-Path $PWD 'touchdesk-0.8.6-usb-disconnect-fix.bin'
    if((Get-FileHash -LiteralPath $image -Algorithm SHA256).Hash.ToLowerInvariant() -ne 'dd3a67975456c71739c04c5fdffe26db98a76aa4493a3b61e28d3f39096bb014') { throw 'Previous firmware SHA256 mismatch' }
    $device=Get-PnpDevice -PresentOnly -Class Ports | Where-Object { $_.FriendlyName -match ('\('+[regex]::Escape($Port)+'\)$') -and $_.InstanceId -match 'VID_1A86' }
    if(!$device) { throw 'Selected port is not a connected CH340' }
    . (Join-Path $PSScriptRoot 'activate-idf541.ps1')
    python -m esptool --chip esp32s3 --port $Port --baud 460800 --after hard_reset write_flash 0x20000 $image
    if($LASTEXITCODE -ne 0){throw 'Previous firmware restore failed'}
} finally {Pop-Location}

