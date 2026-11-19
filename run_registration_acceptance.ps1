# 选退课跨模块验收（Zhang Yuhua）。在项目根目录执行本脚本。
$ErrorActionPreference = 'Stop'
$compilerPath = if ($env:CXX) { $env:CXX } else { 'g++' }
$projectRoot = $PSScriptRoot
Push-Location -LiteralPath $projectRoot
try {
    & (Join-Path $projectRoot 'build_and_test.ps1')
    if ($LASTEXITCODE -ne 0) { throw 'Existing build or tests failed.' }

    $acceptanceExe = Join-Path $projectRoot 'build\registration_cross_module_acceptance.exe'
    $sources = @(
        'source_code/course_registration_business_rules.cpp',
        'source_code/teacher_and_administration_services.cpp',
        'source_code/student_query_and_timetable_services.cpp',
        'automated_tests/registration_cross_module_acceptance.cpp'
    )
    & $compilerPath -std=c++17 -Wall -Wextra -DNDEBUG -Iheader_files @sources -o $acceptanceExe
    if ($LASTEXITCODE -ne 0) { throw 'Acceptance build failed.' }
    & $acceptanceExe
    if ($LASTEXITCODE -ne 0) { throw 'Registration acceptance failed.' }
    Write-Host 'ALL REGISTRATION ACCEPTANCE CHECKS PASSED'
} finally {
    Pop-Location
}
