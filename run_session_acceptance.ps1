param(
    [Parameter(Mandatory = $true)]
    [string]$SodiumRoot,
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Debug',
    [string]$CMakePath = ''
)
$ErrorActionPreference = 'Stop'
if (-not $CMakePath) {
    $available = Get-Command cmake -ErrorAction SilentlyContinue
    if ($available) { $CMakePath = $available.Source }
}
if (-not $CMakePath) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path -LiteralPath $vswhere) {
        $installation = & $vswhere -latest -products '*' -property installationPath
        if ($installation) {
            $CMakePath = Join-Path $installation 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
        }
    }
}
if (-not $CMakePath -or -not (Test-Path -LiteralPath $CMakePath)) {
    throw 'CMake was not found. Pass -CMakePath with the installed cmake.exe path.'
}
$ctestPath = Join-Path (Split-Path -Parent $CMakePath) 'ctest.exe'
$buildDirectory = Join-Path $PSScriptRoot 'build\session'
& $CMakePath -S $PSScriptRoot -B $buildDirectory -G 'Visual Studio 17 2022' -A x64 `
    -DCOURSE_REGISTRATION_WITH_SESSION=ON -DCOURSE_REGISTRATION_USE_SODIUM=ON "-DSODIUM_ROOT=$SodiumRoot"
if ($LASTEXITCODE -ne 0) { throw 'Session configuration failed.' }
& $CMakePath --build $buildDirectory --config $Configuration --parallel 3
if ($LASTEXITCODE -ne 0) { throw 'Session build failed.' }
if ($Configuration -eq 'Release') {
    # The two legacy assert-based suites are verified in Debug.
    & $ctestPath --test-dir $buildDirectory -C Release --output-on-failure `
        -E '^(course_registration_business_rules|student_query_and_timetable)$'
} else {
    & $ctestPath --test-dir $buildDirectory -C Debug --output-on-failure
}
if ($LASTEXITCODE -ne 0) { throw 'Session acceptance failed.' }
Write-Host "ALL SESSION CHECKS PASSED ($Configuration)" -ForegroundColor Green
