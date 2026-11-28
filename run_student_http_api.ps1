# 学生端 HTTP 接口冒烟检查（Zhang Yikai）
#
# 用法：powershell -ExecutionPolicy Bypass -File .\run_student_http_api.ps1
# 先运行 .\build_and_test.ps1 生成 build\course_registration_http_server.exe。
# 每次使用 build\student_http_demo 下独立的数据文件，并随机选一个本地端口。

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$exe = Join-Path $root 'build\course_registration_http_server.exe'
if (-not (Test-Path -LiteralPath $exe)) {
    throw "缺少 $exe，请先运行 .\build_and_test.ps1"
}

$dir = Join-Path $root 'build\student_http_demo'
if (Test-Path -LiteralPath $dir) {
    Remove-Item -LiteralPath $dir -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $dir | Out-Null
$state = Join-Path $dir 'state.json'
$webRoot = Join-Path $root 'web'
$port = Get-Random -Minimum 18000 -Maximum 19000
$base = "http://127.0.0.1:$port"

$script:failures = 0
function Check([bool]$ok, [string]$name) {
    if ($ok) {
        Write-Host "PASS: $name" -ForegroundColor Green
    } else {
        Write-Host "FAIL: $name" -ForegroundColor Red
        $script:failures++
    }
}

function Expect-HttpError([scriptblock]$action, [int]$expected) {
    try {
        & $action | Out-Null
        return $false
    } catch {
        return ($_.Exception.Response.StatusCode.value__ -eq $expected)
    }
}

$server = Start-Process -FilePath $exe -WindowStyle Hidden -PassThru -ArgumentList @(
    '--state', $state, '--port', "$port", '--web-root', $webRoot,
    '--demo-password', 'Demo2026!'
)
try {
    $ready = $false
    for ($i = 0; $i -lt 50; $i++) {
        try {
            Invoke-WebRequest "$base/student/index.html" -UseBasicParsing -TimeoutSec 2 | Out-Null
            $ready = $true
            break
        } catch {
            Start-Sleep -Milliseconds 200
        }
    }
    Check $ready 'HTTP 服务启动并可访问静态页面'
    if (-not $ready) { throw '服务没有在超时内启动' }

    Check (Expect-HttpError { Invoke-RestMethod "$base/api/courses?keyword=SE" } 401) `
        '未登录访问课程接口返回 401'
    Check (Expect-HttpError {
        Invoke-RestMethod "$base/api/auth/login" -Method Post -ContentType 'application/json' `
            -Body '{"account_number":"S2023001","password":"wrong"}'
    } 401) '错误密码返回 401'

    $login = Invoke-RestMethod "$base/api/auth/login" -Method Post `
        -ContentType 'application/json' `
        -Body '{"account_number":"S2023001","password":"Demo2026!"}' `
        -SessionVariable session
    $csrf = $login.data.csrf_token
    Check ($login.code -eq 'Success' -and $login.data.user.role -eq 'Student') `
        '正确密码登录成功'

    $me = Invoke-RestMethod "$base/api/auth/me" -WebSession $session
    Check ($me.code -eq 'Success' -and $me.data.user.account_number -eq 'S2023001') `
        '/api/auth/me 返回当前学生'

    $found = Invoke-RestMethod "$base/api/courses?keyword=SE" -WebSession $session
    Check ($found.code -eq 'Success' -and $found.data.Count -ge 1) `
        '搜索 SE 返回课程'

    $empty = Invoke-RestMethod "$base/api/courses?keyword=ZZZ_NOT_EXIST" -WebSession $session
    Check ($empty.code -eq 'Success' -and $empty.data.Count -eq 0) `
        '无匹配搜索返回空列表'

    $headers = @{ 'X-CSRF-Token' = $csrf }
    $enrolled = Invoke-RestMethod "$base/api/me/enrollments" -Method Post `
        -WebSession $session -Headers $headers -ContentType 'application/json' `
        -Body '{"teaching_class_id":10}'
    Check ($enrolled.code -eq 'Success') '选课成功'

    $timetable = Invoke-RestMethod "$base/api/me/timetable" -WebSession $session
    Check ($timetable.code -eq 'Success' -and $timetable.data.Count -eq 1 -and
           $timetable.data[0].teaching_class_id -eq 10) '课表出现班 10'

    $mine = Invoke-RestMethod "$base/api/me/enrollments" -WebSession $session
    Check ($mine.data.active -eq 1 -and $mine.data.dropped -eq 0) `
        '我的选课统计为 active 1 / dropped 0'

    Check (Expect-HttpError {
        Invoke-RestMethod "$base/api/me/enrollments" -Method Post -WebSession $session `
            -Headers $headers -ContentType 'application/json' `
            -Body '{"teaching_class_id":10}'
    } 409) '重复选课返回 409'

    # 用全新会话测试缺少 CSRF：PowerShell 的 -Headers 会留在会话里，
    # 复用刚才带过 X-CSRF-Token 的会话测不出问题。
    $freshSession = $null
    Invoke-RestMethod "$base/api/auth/login" -Method Post `
        -ContentType 'application/json' `
        -Body '{"account_number":"S2023001","password":"Demo2026!"}' `
        -SessionVariable freshSession | Out-Null
    $csrfStatus = 0
    try {
        Invoke-RestMethod "$base/api/me/enrollments" -Method Post -WebSession $freshSession `
            -ContentType 'application/json' -Body '{"teaching_class_id":10}' | Out-Null
    } catch {
        $csrfStatus = $_.Exception.Response.StatusCode.value__
    }
    Check ($csrfStatus -eq 403) "缺少 CSRF Token 的写请求返回 403（实际 $csrfStatus）"

    $dropped = Invoke-RestMethod "$base/api/me/enrollments/10" -Method Delete `
        -WebSession $session -Headers $headers
    Check ($dropped.code -eq 'Success') '退课成功'

    $timetableAfter = Invoke-RestMethod "$base/api/me/timetable" -WebSession $session
    Check ($timetableAfter.data.Count -eq 0) '退课后课表为空'

    $mineAfter = Invoke-RestMethod "$base/api/me/enrollments" -WebSession $session
    Check ($mineAfter.data.active -eq 0 -and $mineAfter.data.dropped -eq 1) `
        '退课后统计为 active 0 / dropped 1'

    $page = Invoke-WebRequest "$base/student/index.html" -UseBasicParsing
    Check ($page.StatusCode -eq 200 -and $page.Content.Contains('学生登录')) `
        '学生页面可访问'

    $logout = Invoke-RestMethod "$base/api/auth/logout" -Method Post `
        -WebSession $session -Headers $headers
    Check ($logout.code -eq 'Success') '退出登录成功'
    Check (Expect-HttpError {
        Invoke-RestMethod "$base/api/me/timetable" -WebSession $session
    } 401) '退出后旧会话被拒绝'
} finally {
    if ($server -and -not $server.HasExited) {
        Stop-Process -Id $server.Id -Force
    }
}

if ($script:failures -eq 0) {
    Write-Host 'ALL STUDENT HTTP CHECKS PASSED' -ForegroundColor Green
    exit 0
}
Write-Host ("FAILURES: " + $script:failures) -ForegroundColor Red
exit 1
