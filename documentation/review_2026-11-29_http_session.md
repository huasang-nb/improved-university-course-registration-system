# 教师 / 管理员模块复核回复（2026-11-29）

回应组长 2026-11-28 的复核意见。分支 `feature/zhu-http-teacher-admin`，
先合入公共骨架 `c7c5080`（合并提交 `cb51bce`），修复与该提交在同一分支上，尚未合入 `main`。

## 1. 分支与提交

| 内容 | 值 |
| --- | --- |
| 分支 | `feature/zhu-http-teacher-admin` |
| 合入公共骨架 | `cb51bce`（第二父提交 `c7c5080`，第一父提交 `58a7edd`） |
| 本轮修复 | 分支 HEAD，提交信息 `fix(http): address the review of the teacher and admin module` |
| 未合入 | 已推送到 `origin`（`58a7edd` → `b03bb71`），未合 `main`，等复核通过 |

合入时保留了本分支原有能力，没有被公共骨架覆盖：

- Dropped 选课记录的历史保护（有 Dropped 记录时禁止改 `course_id`，`d5853c6`）；
- 课程代码按“去首尾空白 + 转小写”查重（`normalize_account_key`）；
- 第 4 周失败路径验收套件 `admin_failure_acceptance_tests`（309 项）；
- `build_and_test.ps1` 的 `Optional` 跳过逻辑与 UTF-8 BOM。

## 2. 构建与测试命令

公共会话与 libsodium 同时打开（`session_service` 要求 `COURSE_REGISTRATION_USE_SODIUM=ON`）：

```powershell
$repo = "C:\Users\MSI_NB\Desktop\deepseek_harness_工作文件夹\improved-university-course-registration-system"
$cmake = "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
$ctest = "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe"

& $cmake -S $repo -B "$repo\build\cmake-team4" -G "Visual Studio 17 2022" -A x64 `
    -DCOURSE_REGISTRATION_USE_SODIUM=ON -DCOURSE_REGISTRATION_WITH_SESSION=ON `
    "-DSODIUM_ROOT=C:/Users/MSI_NB/Desktop/deepseek_harness_工作文件夹/_libsodium_probe/unzip/libsodium"

& $cmake --build "$repo\build\cmake-team4" --config Debug
& $ctest --test-dir "$repo\build\cmake-team4" -C Debug
& $cmake --build "$repo\build\cmake-team4" --config Release
& $ctest --test-dir "$repo\build\cmake-team4" -C Release
```

结果：

| 配置 | 构建 | `ctest` |
| --- | --- | --- |
| Debug | 成功，无 error | 19/19 通过 |
| Release | 成功，无 error | 17/17 有效（另 2 项 Skipped） |

Release 的说明：`course_registration_business_rules`、`student_query_and_timetable` 两套历史用例直接写 `assert`，Release（NDEBUG）下断言被编译器整段去掉，继续打印“通过”就是假通过。现在两套在 `NDEBUG` 下打印说明并返回 77，`CMakeLists.txt` 登记 `SKIP_RETURN_CODE 77`，`ctest` 显示 **Skipped**；这两套的断言只在 Debug 里真正执行。所以 Release 的有效口径是 **17/17**，另外两套由 Debug 覆盖，不再把“19/19”当作 Release 的有效结果。

编译警告只有既有的 `C4996 localtime`（`course_registration_business_rules.cpp:12`、
`teacher_and_administration_services.cpp:468`）；`/W4` 下本模块无警告。

各套件数量（Debug 实测）：

| 套件 | 数量 |
| --- | --- |
| `http_teacher_admin_tests`（handler 层） | 274 项 |
| `http_team4_integration_tests`（真实会话端到端，含 `/api/auth/me` 字段形状） | 166 项 |
| `admin_failure_acceptance_tests` | 309 项 |
| `teacher_and_administration_services_tests` | 505 项 |
| `persistent_administration_tests` | 134 项 |
| `session_tests` | 389 项 |
| `password_service_tests` | 67 项 |
| `web/tests/app_text_test.js` | 18 项 |

## 3. 是否打开正式会话选项

是。`COURSE_REGISTRATION_USE_SODIUM=ON` + `COURSE_REGISTRATION_WITH_SESSION=ON`，
上面的 Debug/Release 结果就是在这个组合下跑出来的，不是默认配置的结果。

正式服务器 `http_team4_web_server` 在同一进程里只建一个 `SessionRuntime`：

```cpp
SessionRuntime runtime(context, storage, session_options);
bind_session_runtime(runtime);
runtime.register_auth_routes(server);                       // 公共层注册 /api/auth/*
http::register_teacher_admin_routes_with_session(server);   // 本模块只注册教师/管理员路由
```

本模块的适配器在 `with_business_lock()` 回调里调用 `dispatch()`，不调用 `dispatch_locked()`；
`Services` 仍保留 `std::mutex& business_mutex` 成员（独立服务器与自测路径要用它），正式适配器传入一个不会被加锁的局部 mutex，真正的锁由公共层持有；`dispatch()` 在适配器路径上不再自己加锁，避免重复加锁。

## 4. 六项修复与测试证据

1. **改密弹窗打不开**（`web/shared/app.js`）：`backdrop.innerHTML` 的两段提示文字之间少一个
   `+`，实际只拼进前半段，保存/取消按钮没有生成，随后 `querySelector` 拿到 `null` 报错。
   已补齐；新增 `web/tests/app_text_test.js`，把模板表达式取出来求值，断言三个输入框
   （`#pw-current`/`#pw-new`/`#pw-confirm`）与保存/取消按钮都在，18 项全过。
   手动验证项：点击打开、三处输入、保存、取消、`Escape`、点背景关闭、规则错误提示、
   关闭后再次打开（`close()` 会 `remove()` 弹窗并解绑 `keydown`，所以不会重复绑定）。
2. **教室名被当成 HTML**：`meetingText()` 的输出契约改为“已转义、可直接放进 `innerHTML`”，
   `room` 统一过 `esc()`；`meetingsText()` 复用它。使用位置：管理员教学班列表、教师教学班列表、
   教师班级详情（都走这两个函数，没有第二套拼接）。测试用 `<b>TestRoom</b>`、`A&B "x" > y`
   断言输出不含裸标签、实体只转义一次（`esc()` 二次调用会再转义，调用方不要重复调用）。
3. **锁内身份失效返回 500**：`result_source_for()` 明确区分
   `AuthenticationRequired → Session(401)`、`PermissionDenied → Permission(403)`、
   `Storage* → Business(500，保留码)`、其它 → `Internal(500 InternalError)`。
   端到端测试注入“`require_session` 通过后账号被停用”的场景，断言 401。
4. **保存失败泄露路径**：新增 `public_result()`，五个 `Storage*` 的 message 换成
   “storage failure: nothing was changed; check the state file and retry”，错误码不变；
   `from_result()` 与正式适配器写回都经过它，不存在“只改公共 `write_result()`、本模块照样发原文”的旁路。
   测试独占 `state.json` 后 `PATCH` 容量返回 500 `StorageIoError`，响应不含 `state.json`/`C:`/`:\`，
   内存容量仍为 2，解除占用后重试成功并落盘。
5. **重复 JSON 字段**：`body_to_object()` 用 `json::parse` 回调按对象层查重，
   `{"capacity":0,"capacity":2}` → 400 `InvalidInput`“request body contains a duplicated field”，
   不执行业务、不保存。清的是 `depth + 1` 层（键事件比对象开始深一层）：一开始清错层，
   把上一个对象的键留下，`course.course_id` 与 `first_teaching_class.course_id` 会被误判成重复，
   Release 测试暴露后修正。原有非法 JSON、未知字段、类型、编号范围检查保留。
6. **顶栏不假定 `name`**：`/api/auth/me` 的 user 只有 `role`/`user_id`/`account_number`，
   顶栏显示 `roleText(role) + 账号 + （用户编号 id）`；`roleText()` 已加入 `window.Team4`。

端到端测试还覆盖：无 Cookie 401；教师看他人班级 403；教师执行管理员操作 403；缺或错 CSRF 403；
合法管理员建课程成功且重新 `load_state` 一致；业务拒绝（删最后一个班）409 且文件不变；
内部异常 500 且不出现“请重新登录”。

## 5. 复核项与提交对应

本轮分支 `feature/zhu-http-teacher-admin`，HEAD `cd32606`（`b0ada2d` 之后只加了一个测试与文档提交）。
每一项都能单独指到提交和验证方式：

| 复核项 | 提交 | 验证 |
|---|---|---|
| 一、改密弹窗拼接 | `b0ada2d` | `web/tests/app_text_test.js` 18 项（求值模板表达式，断言三个输入框与保存/取消按钮存在）+ 手动打开/输入/保存/取消/Escape/点背景/重复打开 |
| 二、教室文本转义 | `b0ada2d` | 同上前端回归（`<b>TestRoom</b>`、引号、`&`、`>` 只显示文字）；管理员教学班列表、教师教学班列表、教师班级详情三处 |
| 三、锁内失败映射 | `b0ada2d` | `test_result_source_mapping`（7 个码逐个断言）+ 端到端 `test_deactivated_session`（锁内停用返回 401，不是 500 `InternalError`） |
| 四、保存失败脱敏 | `b0ada2d` | 端到端 `test_save_failure_is_sanitized`：占用主文件 → 500 `StorageIoError`，响应不含 `state.json`/`C:`/`:\\`，内存未被替换，解除后重试成功 |
| 五、重复 JSON 字段 | `b0ada2d` | 端到端 `test_duplicate_json_field`：`{"capacity":0,"capacity":2}` → 400 `InvalidInput`，容量仍为 2 |
| 六、正式会话联调 | `b0ada2d` | `http_team4_integration_tests` 166 项：401/403/CSRF/保存落盘/业务拒绝不改数据/内部异常 500 不显示为登录失败/改密与撤销旧会话 |
| 会话字段形状 | `cd32606` | `test_me_shape`：三种角色各查一次 `data.user.role`/`user_id`/`account_number`、`data.csrf_token`，并验证 Token 带与不带时的 403 `CsrfRejected` / 200 `Success` |

版本与验证结果：MSVC 2022，`COURSE_REGISTRATION_USE_SODIUM=ON` 与
`COURSE_REGISTRATION_WITH_SESSION=ON`，Debug **19/19**、Release **17/17（另 2 项 Skipped）**；本条记录对应 HEAD `cd32606`。

未验证事项（按约定留到整合阶段）：三端页面在浏览器里的完整流程（改密弹窗只做脚本级回归 + 手动验证），
以及接通共同服务器后的端到端走查。

## 6. 仍需你配合 / 未完成

- **`/api/auth/me` 的字段名（已定，2026-11-28）**：`data.user.role` 为
  `Student` / `Teacher` / `Administrator`，`data.user.user_id` 为整数，
  `data.user.account_number` 为当前账号，`data.csrf_token` 为当前会话 Token；
  user 在 `data` 内，页面不依赖 `name`。已加 `test_me_shape` 把它锁进端到端测试
  （三种角色各查一次，并用管理员 Token 走一次带/不带 CSRF 的写操作）。
- **锁内失败的细分（已定，2026-11-28）**：公共层 `with_business_lock` 返回
  `Success` / `AuthenticationRequired`（401）/ `PermissionDenied`（403）；
  `Storage*` 保留实际错误码返回 500；锁或回调中的内部异常由公共 HTTP 兜底为
  500 `InternalError`。`result_source_for()` 已按这张表实现，并有单测覆盖。
- **测试临时目录**：这台机器 `%TEMP%` 下不能新建子目录，持久化相关测试会报
  `create_directory: 拒绝访问`。跑 `ctest` 前需要把 `TEMP`/`TMP` 指到可写目录（我用
  `build\tmp-release`），否则是环境问题而不是代码失败。建议公共测试也改成相对工作目录。
- **学生端**：学生页面与选退课不在本模块；正式服务器上学生登录后提示“学生页面尚未接入这个服务器”。
- **页面测试（按约定留到整合阶段）**：改密弹窗的“打开/输入/保存/取消/Escape/点背景/
  重复打开”目前是脚本级回归（`web/tests/app_text_test.js` 直接求值模板表达式）加手动验证，
  没有引入浏览器自动化；三端页面与完整流程等接入共同服务器后统一检查。
- 首次登录强制改密、管理员在线重置他人密码，按约定本轮不扩展。
