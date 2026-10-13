Write-Host ""
Write-Host "========================================"
Write-Host " STM32 VS Code Environment Setup"
Write-Host "========================================"
Write-Host ""

$ProjectRoot = Get-Location

Write-Host "[1/3] Configuring CMake..."
cmake --preset Debug

if ($LASTEXITCODE -ne 0)
{
    Write-Host "CMake configure failed." -ForegroundColor Red
    exit 1
}

$Target = Join-Path $ProjectRoot "build\Debug\compile_commands.json"
$Link   = Join-Path $ProjectRoot "compile_commands.json"

Write-Host ""
Write-Host "[2/3] Creating compile_commands.json link..."

if (Test-Path $Link)
{
    Remove-Item $Link -Force
}

New-Item `
    -ItemType SymbolicLink `
    -Path $Link `
    -Target $Target | Out-Null

Write-Host ""
Write-Host "[3/3] Building project..."

cmake --build --preset Debug

if ($LASTEXITCODE -ne 0)
{
    Write-Host "Build failed." -ForegroundColor Red
    exit 1
}

Write-Host ""
Write-Host "========================================"
Write-Host " STM32 VS Code setup completed."
Write-Host "========================================"
Write-Host ""
Write-Host "Ctrl+Shift+B : Build"
Write-Host "Ctrl+Alt+F   : Build + Flash"
Write-Host "F5           : Debug"
Write-Host ""