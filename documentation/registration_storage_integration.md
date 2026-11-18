# 选退课与持久化接入（Zhang Yuhua，2026-11-18）

## 范围和版本

本分支整合 Yuhua 的 `bf689e8` 验收、Kai 的 `88f8d99` 持久化和 Zhu 的
`c954b67` 管理员修复，保留各人的原提交历史。账号比较冲突统一复用
`normalize_account_key()`。本说明是当前代码的运行说明，不替代待确认的正式项目记录。

新增 `PersistentRegistrationService`，把选退课接到完整 JSON 保存接口。
选课核心的学分累计使用 `long long`，拒绝 `INT_MAX + 5` 等溢出绕过，同时保留
恰好等于上限允许的行为。没有新增固定课程学分上限。

## 接口及身份边界

```cpp
PersistentRegistrationService registration(context, storage_options);
OperationResult result = registration.enroll(session_identity, teaching_class_id);
OperationResult dropped = registration.drop(session_identity, teaching_class_id);
```

`session_identity` 必须来自可信登录会话。该入口每次检查学生角色、学生存在、
账号与编号匹配，以及当前启停状态；账号比较去首尾空白并转小写。
不接收另一个目标学生编号，操作对象始终是会话学生。
本次没有实现密码验证、会话管理或 Web 页面。

身份通过后复制临时状态，调用原有 enroll/drop。业务失败立即返回原错误，不写文件。
业务成功后保存临时状态，保存失败返回存储错误，正式共享状态保持原样。
保存成功后用不抛异常的移动赋值发布，再返回业务成功。保留原业务检查顺序。
已有查询服务继续引用同一个 SystemContext，发布后可读取新结果。

这是同步单进程原型。后续 Web 并发必须让所有读取和修改共享上下文的入口共同协调，
本类没有提供覆盖管理员操作的全局锁，不宣称已完成并发或断电耐久性验收。

## 运行验收

在项目根目录执行：

```powershell
powershell -ExecutionPolicy Bypass -File .\run_registration_storage_acceptance.ps1
powershell -ExecutionPolicy Bypass -File .\run_registration_storage_acceptance.ps1 -Configuration Release
```

脚本优先查找 PATH 中的 CMake，其次查找 Visual Studio 附带版本；也可传
`-CMakePath '完整的cmake.exe路径'`。本机支持 Visual Studio 2022/MSVC。
GCC 使用 Kai 文档中的受支持版本，MinGW 8.1 的 filesystem 不受支持。
构建文件集中在 `build/storage`，不修改系统执行策略。

Debug 执行全部七套测试。Release 执行五套显式检查测试，跳过两个依赖 assert
的旧测试；旧测试以 Debug 结果为准。新增检查失败返回非零退出码。

`registration_credit_limit_tests` 验证极大学分累计、恰好上限与失败保持数据。
`registration_storage_integration_tests` 验证选退课保存、全字段恢复、历史复用、
共享查询、身份与停用检查、存储失败不发布、窗口边界和重启后学分锁定。
Windows 额外模拟主文件被占用，验证保存失败及解除占用后的选课、退课重试。
测试使用独立临时目录，不写默认运行数据；失败时输出保留的测试目录。

## 独立进程演示

这是本地联调示例，以种子数据中的学生账号模拟会话，不是登录页面。
每条命令启动一个新进程，使用同一保存路径：

```powershell
$demo = '.\build\storage\Debug\registration_storage_demonstration.exe'
& $demo --state '.\build\storage\demo\state.json' enroll S2023001 10
& $demo --state '.\build\storage\demo\state.json' status S2023001
& $demo --state '.\build\storage\demo\state.json' drop S2023001 10
& $demo --state '.\build\storage\demo\state.json' status S2023001
```

依次可见 Active=1、再次启动仍为1、退课后 Active=0/Dropped=1，再启动后
历史仍保留、教学班余量恢复。文件已存在时读取原状态，不要为重跑演示删除真实数据。
该目录属于构建产物，不进入提交。

## 后续仍待其他模块完成

Zhu 的 `update_course()` 学分累计仍是 int，需由其分支修复后再整合。
课程与首班聚合创建、管理员修改保存，以及 Dropped 引用完整性检查的旧缺口，
仍按原分工对接。此处只调用已复核的持久化接口，不代替这些管理员功能。
运行时默认 state.json、.tmp 和 .bak 已忽略，不提交 exe 或构建目录。
