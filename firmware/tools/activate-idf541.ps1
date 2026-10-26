param(
    [string]$IdfPath = 'C:\play\esp32s3touchscreen\toolchains\idf-5.4.1-cn',
    [string]$ToolsPath = 'C:\Espressif\tools',
    [string]$Python = 'C:\Espressif\tools\python\python.exe'
)
$env:IDF_PATH = $IdfPath
$env:IDF_TOOLS_PATH = $ToolsPath
Remove-Item Env:IDF_PYTHON_ENV_PATH -ErrorAction SilentlyContinue
$exports = & $Python (Join-Path $IdfPath 'tools\idf_tools.py') export --format key-value
if ($LASTEXITCODE -ne 0) { throw 'ESP-IDF 5.4 environment export failed' }
foreach ($line in $exports) {
    if ($line -match '^([A-Za-z_][A-Za-z_0-9]*)=(.*)$') {
        $name = $matches[1]
        $value = $matches[2]
        if ($name -eq 'PATH') { $value = $value.Replace('%PATH%', $env:PATH) }
        [Environment]::SetEnvironmentVariable($name, $value, 'Process')
    }
}
Write-Host "ESP-IDF: $env:IDF_PATH"
Write-Host "Python: $env:IDF_PYTHON_ENV_PATH"
