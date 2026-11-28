# 学生流程演示回归检查（Zhang Yikai，第 5 周）
#
# 用法：powershell -ExecutionPolicy Bypass -File .\run_student_flow_demo_regression.ps1
# 先运行 .\build_and_test.ps1 生成 build\student_flow_demonstration.exe。
# 每个用例使用 build\student_flow_demo_regression 下的独立演示文件。

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$exe = Join-Path $root 'build\student_flow_demonstration.exe'
if (-not (Test-Path -LiteralPath $exe)) {
    throw "缺少 $exe，请先运行 .\build_and_test.ps1"
}

$dir = Join-Path $root 'build\student_flow_demo_regression'
if (Test-Path -LiteralPath $dir) {
    Remove-Item -LiteralPath $dir -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $dir | Out-Null

$utf8 = [Text.UTF8Encoding]::new($false)
function Read-State([string]$path) { [IO.File]::ReadAllText($path, $utf8) }
function Write-State([string]$path, [string]$text) {
    [IO.File]::WriteAllText($path, $text, $utf8)
}

$script:failures = 0
function Check([bool]$ok, [string]$name) {
    if ($ok) {
        Write-Host "PASS: $name" -ForegroundColor Green
    } else {
        Write-Host "FAIL: $name" -ForegroundColor Red
        $script:failures++
    }
}

function Run-Demo([string]$statePath) {
    # 演示失败时会往 stderr 写原因；这里不能让原生命令的 stderr 触发终止错误。
    $previous = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    & $exe --state $statePath *> $null
    $code = $LASTEXITCODE
    $ErrorActionPreference = $previous
    return $code
}

# 1. 新文件：登录 → 搜索 → 选课 → 课表 → 状态 → 退课
$fresh = Join-Path $dir 'fresh.json'
Check ((Run-Demo $fresh) -eq 0) '新文件完整流程退出码为 0'
$text = Read-State $fresh
Check ($text -match '"student_id": 1001, "teaching_class_id": 10, "status": "dropped"') `
    '新文件结束后记录为 Dropped'

# 2. 同一个已完成的演示文件再跑一次
Check ((Run-Demo $fresh) -eq 0) '同一文件再次运行仍成功'

# 3. 学生原本已选班 10：选课应失败，且保留原 Active 记录
$activeState = Join-Path $dir 'already_active.json'
$text = Read-State $fresh
$text = $text.Replace(
    '"student_id": 1001, "teaching_class_id": 10, "status": "dropped"',
    '"student_id": 1001, "teaching_class_id": 10, "status": "active"')
Write-State $activeState $text
$before = Read-State $activeState
Check ((Run-Demo $activeState) -ne 0) '已选班 10 时演示返回非 0'
$after = Read-State $activeState
Check ($after -eq $before) '已选班 10 时文件保持原样'
Check ($after -match '"student_id": 1001, "teaching_class_id": 10, "status": "active"') `
    '原有 Active 记录保留'

# 4. 选课关闭：选课应失败，记录不变
$closedState = Join-Path $dir 'closed.json'
$text = Read-State $fresh
$text = $text.Replace('"registration_open": true', '"registration_open": false')
Write-State $closedState $text
$before = Read-State $closedState
Check ((Run-Demo $closedState) -ne 0) '选课关闭时演示返回非 0'
$after = Read-State $closedState
Check ($after -eq $before) '选课关闭时文件保持原样'

# 5. 保存失败：父路径是文件
$blockedParent = Join-Path $dir 'blocked'
[IO.File]::WriteAllText($blockedParent, 'not a directory', $utf8)
$blockedState = Join-Path $blockedParent 'state.json'
Check ((Run-Demo $blockedState) -ne 0) '保存失败时演示返回非 0'

if ($script:failures -eq 0) {
    Write-Host 'ALL STUDENT FLOW DEMO CHECKS PASSED' -ForegroundColor Green
    exit 0
}
Write-Host ("FAILURES: " + $script:failures) -ForegroundColor Red
exit 1
