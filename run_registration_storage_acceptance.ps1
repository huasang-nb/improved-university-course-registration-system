param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Debug',
    [string]$CMakePath = ''
)
$ErrorActionPreference = 'Stop'
$projectRoot = $PSScriptRoot
if (-not $CMakePath) {
    $available = Get-Command cmake -ErrorAction SilentlyContinue
    if ($available) { $CMakePath = $available.Source }
}
if (-not $CMakePath) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path -LiteralPath $vswhere) {
        $installation = & $vswhere -latest -products '*' -property installationPath
        if ($installation) {
            $candidate = Join-Path $installation 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
            if (Test-Path -LiteralPath $candidate) { $CMakePath = $candidate }
        }
    }
}
if (-not $CMakePath -or -not (Test-Path -LiteralPath $CMakePath)) {
    throw 'CMake was not found. Pass -CMakePath with the installed cmake.exe path.'
}
$ctestPath = Join-Path (Split-Path -Parent $CMakePath) 'ctest.exe'
if (-not (Test-Path -LiteralPath $ctestPath)) { throw 'ctest.exe was not found beside CMake.' }
$buildDirectory = Join-Path $projectRoot 'build\storage'
& $CMakePath -S $projectRoot -B $buildDirectory
if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed.' }
& $CMakePath --build $buildDirectory --config $Configuration --parallel 4
if ($LASTEXITCODE -ne 0) { throw 'Storage integration build failed.' }
if ($Configuration -eq 'Release') {
    # 两个旧测试依赖 assert；其有效验收使用 Debug，以下五套使用显式检查。
    & $ctestPath --test-dir $buildDirectory -C $Configuration --output-on-failure `
        -R '^(teacher_and_administration_services|persistence|registration_credit_limit|registration_cross_module_acceptance|registration_storage_integration)$'
} else {
    & $ctestPath --test-dir $buildDirectory -C $Configuration --output-on-failure
}
if ($LASTEXITCODE -ne 0) { throw 'Storage integration tests failed.' }
Write-Host "ALL REGISTRATION STORAGE CHECKS PASSED ($Configuration)" -ForegroundColor Green
