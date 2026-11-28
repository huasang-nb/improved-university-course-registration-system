﻿# 编译并运行全部测试（Zhu Ziliang）
# 用法：.\build_and_test.ps1
# 需要支持 C++17 的 g++（GCC 8.1+；持久化目标会自动追加 -lstdc++fs）。
# 全部测试通过时脚本返回 0，任一测试失败返回 1。

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
    },
    @{
        Name = "student_query_and_timetable_tests"
        Sources = @("source_code/course_registration_business_rules.cpp",
                    "source_code/teacher_and_administration_services.cpp",
                    "source_code/student_query_and_timetable_services.cpp",
                    "automated_tests/student_query_and_timetable_tests.cpp")
    },
    @{
        Name = "student_query_demonstration"
        Sources = @("source_code/course_registration_business_rules.cpp",
                    "source_code/teacher_and_administration_services.cpp",
                    "source_code/student_query_and_timetable_services.cpp",
                    "source_code/student_query_demonstration.cpp")
    },
    @{
        Name = "persistence_tests"
        Sources = @("source_code/course_registration_business_rules.cpp",
                    "source_code/teacher_and_administration_services.cpp",
                    "source_code/student_query_and_timetable_services.cpp",
                    "source_code/persistence_service.cpp",
                    "automated_tests/persistence_tests.cpp")
        # GCC 8 的 std::filesystem 需要显式链接 stdc++fs；GCC 9+/MSVC 下这个库为空，可忽略。
        Extra = @("-lstdc++fs")
        # 该目标依赖 <filesystem>：MinGW GCC 8 在 Windows 上编译不过，此时跳过而不是判失败。
        Optional = $true
    },
    @{
        Name = "persistent_administration_tests"
        Sources = @("source_code/course_registration_business_rules.cpp",
                    "source_code/teacher_and_administration_services.cpp",
                    "source_code/persistence_service.cpp",
                    "source_code/persistent_administration_service.cpp",
                    "automated_tests/persistent_administration_tests.cpp")
        Extra = @("-lstdc++fs")
        # 与 persistence_tests 相同：没有可用的 <filesystem> 时跳过。
        Optional = $true
    },
    @{
        Name = "admin_failure_acceptance_tests"
        Sources = @("source_code/course_registration_business_rules.cpp",
                    "source_code/teacher_and_administration_services.cpp",
                    "automated_tests/admin_failure_acceptance_tests.cpp")
    },
    @{
        Name = "student_interface_demonstration"
        Sources = @("source_code/course_registration_business_rules.cpp",
                    "source_code/teacher_and_administration_services.cpp",
                    "source_code/student_query_and_timetable_services.cpp",
                    "source_code/persistence_service.cpp",
                    "source_code/persistent_registration_service.cpp",
                    "source_code/student_interface.cpp",
                    "source_code/student_interface_demonstration.cpp")
        Extra = @("-lstdc++fs")
    },
    @{
        Name = "student_interface_tests"
        Sources = @("source_code/course_registration_business_rules.cpp",
                    "source_code/teacher_and_administration_services.cpp",
                    "source_code/student_query_and_timetable_services.cpp",
                    "source_code/persistence_service.cpp",
                    "source_code/persistent_registration_service.cpp",
                    "source_code/student_interface.cpp",
                    "automated_tests/student_interface_tests.cpp")
        Extra = @("-lstdc++fs")
    }
)

$failures = 0
$built = @{}
foreach ($target in $targets) {
    $exe = Join-Path $build ($target.Name + ".exe")
    Write-Host ("`n=== build " + $target.Name + " ===") -ForegroundColor Cyan
    $extra = @()
    if ($target.ContainsKey("Extra")) { $extra = $target.Extra }
    & $gxx @common @($target.Sources) -o $exe @extra
    if ($LASTEXITCODE -ne 0) {
        $optional = $target.ContainsKey("Optional") -and $target.Optional
        if ($optional) {
            Write-Host ("SKIPPED: " + $target.Name + " (this compiler cannot build <filesystem>; use MSVC 2022 / CMake)") -ForegroundColor Yellow
            if (Test-Path $exe) { Remove-Item -LiteralPath $exe -Force }
            continue
        }
        Write-Host ("BUILD FAILED: " + $target.Name) -ForegroundColor Red
        $failures++
        continue
    }
    $built[$target.Name] = $true
    Write-Host ("OK: " + $exe)
}

$tests = @("course_registration_business_rules_tests",
           "teacher_and_administration_services_tests",
           "student_query_and_timetable_tests",
           "persistence_tests",
           "persistent_administration_tests",
           "admin_failure_acceptance_tests"),
           "student_interface_tests")
foreach ($test in $tests) {
    if (-not $built.ContainsKey($test)) {
        Write-Host ("`n=== skip " + $test + " (not built) ===") -ForegroundColor Yellow
        continue
    }
    $exe = Join-Path $build ($test + ".exe")
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
