# 编译并运行全部测试（Zhu Ziliang）
# 用法：.\build_and_test.ps1
# 需要支持 C++17 的 g++。全部测试通过时脚本返回 0，任一测试失败返回 1。

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $root

$gxx = if ($env:CXX) { $env:CXX } else { "g++" }
$build = Join-Path $root "build"
New-Item -ItemType Directory -Force -Path $build | Out-Null

$common = @("-std=c++17", "-Wall", "-Wextra", "-Iheader_files")

$targets = @(
    @{
        Name = "course_registration_business_rules_tests"
        Sources = @("source_code/course_registration_business_rules.cpp",
                    "automated_tests/course_registration_business_rules_tests.cpp")
    },
    @{
        Name = "teacher_and_administration_services_tests"
        Sources = @("source_code/course_registration_business_rules.cpp",
                    "source_code/teacher_and_administration_services.cpp",
                    "automated_tests/teacher_and_administration_services_tests.cpp")
    },
    @{
        Name = "course_registration_console_demonstration"
        Sources = @("source_code/course_registration_business_rules.cpp",
                    "source_code/course_registration_console_demonstration.cpp")
    },
    @{
        Name = "teacher_and_administration_demonstration"
        Sources = @("source_code/course_registration_business_rules.cpp",
                    "source_code/teacher_and_administration_services.cpp",
                    "source_code/teacher_and_administration_demonstration.cpp")
    }
)

$failures = 0
foreach ($target in $targets) {
    $exe = Join-Path $build ($target.Name + ".exe")
    Write-Host ("`n=== build " + $target.Name + " ===") -ForegroundColor Cyan
    & $gxx @common @($target.Sources) -o $exe
    if ($LASTEXITCODE -ne 0) {
        Write-Host ("BUILD FAILED: " + $target.Name) -ForegroundColor Red
        $failures++
        continue
    }
    Write-Host ("OK: " + $exe)
}

$tests = @("course_registration_business_rules_tests",
           "teacher_and_administration_services_tests")
foreach ($test in $tests) {
    $exe = Join-Path $build ($test + ".exe")
    if (-not (Test-Path $exe)) { continue }
    Write-Host ("`n=== run " + $test + " ===") -ForegroundColor Cyan
    & $exe
    if ($LASTEXITCODE -ne 0) {
        Write-Host ("TEST FAILED: " + $test) -ForegroundColor Red
        $failures++
    }
}

Write-Host ""
if ($failures -eq 0) {
    Write-Host "ALL BUILD AND TESTS PASSED" -ForegroundColor Green
    exit 0
}
Write-Host ("FAILURES: " + $failures) -ForegroundColor Red
exit 1
