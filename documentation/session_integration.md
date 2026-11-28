# 公共会话、登录和本人改密接入

本阶段提供 `header_files/session.hpp`、`source_code/session.cpp` 和可运行的认证 API。
接口位于 `course_registration` 命名空间。这里说明代码怎样使用，不替代三份 PROJECT 记录中的正式业务决策。
会话配置默认空闲 30 分钟、最长 8 小时，均可通过 `SessionOptions` 调整。
学生、教师和管理员的完整页面与业务路由仍由各成员分支接入，不能把本阶段测试当成三端整合验收。

## 构建与测试

使用 Windows x64、MSVC 2022 和官方 libsodium 动态包。在仓库根目录运行：

```powershell
.\run_session_acceptance.ps1 -SodiumRoot 'C:/deps/libsodium' -Configuration Debug
.\run_session_acceptance.ps1 -SodiumRoot 'C:/deps/libsodium' -Configuration Release
```

脚本把两个选项都打开：`COURSE_REGISTRATION_WITH_SESSION=ON` 和 `COURSE_REGISTRATION_USE_SODIUM=ON`。
如果手动配置时只打开会话选项，CMake 会明确报告缺少真实密码依赖，不会退回演示密码。
第三方头文件和许可证位于 `third_party`，版本及校验值见其中的 README。
Debug 运行全部测试。Release 排除两套依赖 assert 的旧测试，其余测试仍会实际执行检查。
`build_and_test.ps1` 是原有 g++ 路径，本阶段的 libsodium 和 HTTP 验收使用上述 MSVC 路径。

## 最小可运行入口

先准备已有账号和状态文件，再用 `password_setup` 设置各自的初始密码。
启动 HTTP 服务器前必须停止密码工具；使用密码工具时也必须停止服务器。
同一份状态文件不能同时交给两个服务器进程使用。

```powershell
.\build\session\Debug\session_auth_server.exe --state .\build\demo\state.json --port 8080
```

这个最小入口只监听 `127.0.0.1`，只注册认证 API，不包含学生、教师、管理员页面。
文件不存在、读取失败或文件损坏时退出，不静默生成新文件覆盖数据。
允许的页面来源为 `http://127.0.0.1:8080`，端口随参数改变。

| 方法 | 路径 | 内容 |
| --- | --- | --- |
| POST | `/api/auth/login` | `account_number`、`password`；成功下发 Cookie 并返回 `user`、`csrf_token` |
| GET | `/api/auth/me` | 返回当前三角色身份之一及当前 CSRF Token |
| POST | `/api/auth/logout` | 检查 CSRF，撤销当前会话并清 Cookie |
| PUT | `/api/auth/password` | `current_password`、`new_password`；检查 CSRF，成功保存后撤销该账号全部旧会话 |

请求与响应都使用 JSON。正文上限 64 KiB；认证正文只接受表中要求的两个字符串字段，重复字段和未知字段都会拒绝。
登录请求需要同源 Origin；没有 Origin 时检查 Referer。两者都缺失、Origin 为 null 或来源不同，返回 403 CsrfRejected。
命令行测试也需要发送合法 Origin。已登录的写操作使用 `X-CSRF-Token`，GET/HEAD/OPTIONS 不需要 CSRF Token。

## 在共同服务器中创建一次运行对象

```cpp
#include "session.hpp"

using namespace course_registration;

// context 和 storage 已由启动代码加载。context 必须一直存在到服务器完全停止。
SessionOptions options;
options.allowed_origin = "http://127.0.0.1:8080";
SessionRuntime runtime(context, storage, options);
bind_session_runtime(runtime);

httplib::Server server;
runtime.register_auth_routes(server);

// 合入 Zhu 的模块后，在同一个 server 上调用：
// course_registration::http::register_teacher_admin_routes_with_session(server);
// Kai 的学生路由也注册在这个 server 上，不再注册另一套 /api/auth/*。
server.listen("127.0.0.1", 8080);
```

只绑定一个运行对象，运行期间不切换。先停止服务器并等待请求处理结束，再销毁 runtime。
`register_auth_routes` 同时设置了内部异常的兜底处理，其他模块不要再用另一个异常处理器覆盖它。
自己的可执行目标链接 `session_service`；使用现有 `course_registration_link_sodium(目标名)` 链接并复制对应配置的 DLL。

## 业务锁的使用

`require_session(request)` 返回检查结果和可选身份，只有身份有效才续期空闲时间。
静态页面请求不会自动续期。空闲时限从最近一次有效身份检查开始计算，最长时限始终从登录成功开始计算。
每次使用会话都会核对当前账号、角色、编号、启停状态和密码哈希；过期、停用、删除、改号或凭据变化后旧会话被拒绝。

`with_business_lock(actor, role, fn)` 的回调返回 void。业务结果或已经转换好的响应通过捕获变量带出来：

```cpp
OperationResult business{ResultCode::InvalidInput, "operation did not run"};
const auto checked = with_business_lock(actor, Role::Student,
    [&](LockedServices& services) {
        business = services.registration_service().enroll(actor, teaching_class_id);
    });

if (checked.code != ResultCode::Success) {
    write_result(response, checked, ResultSource::Business);
    return;
}
write_result(response, business, ResultSource::Business);
```

函数的返回值只表示身份和角色检查是否通过，不代替业务结果。满班等业务错误仍按 409 返回。
回调异常向外传播，由公共 HTTP 异常处理返回 500；不会被改成会话失效。异常发生时先自动释放锁。
会话表和所有业务读写使用同一把 mutex，因此没有两把锁取得顺序相反的问题。
密码校验和新哈希计算在锁外执行，发布前重新检查当前状态。

`LockedServices` 提供管理员、教师、学生持久化服务。`context()` 为兼容 Zhu 的 Services 提供真实引用，
仅在回调内使用；修改资料仍调用持久化服务。不要把引用保留到锁外。
`context_snapshot()` 返回服务器内部副本，供需要离开锁继续处理的场景使用。
HTTP 输出必须逐项挑选字段，不能把包含密码哈希的整个副本直接转成响应。
Zhu 的正式适配器调用 `dispatch()`，不能在共同锁内再次调用 `dispatch_locked()`。

## 密码服务和发布顺序

`change_password(context, storage, actor, current_password, new_password)` 内部完成保存和发布，不只返回哈希。
这个便捷函数供单线程或已由调用方协调访问的路径使用，服务自身不再加锁。
HTTP 路径使用 `prepare_password_change` 和 `apply_password_change` 拆开处理：
在锁内复制状态，在锁外校验并计算哈希，再取得同一把锁检查会话和旧哈希，复制最新状态进行保存。
这样不会把哈希计算期间其他业务已经保存的修改覆盖掉。
`PreparedPasswordChange` 只在服务器内部传递，不能由请求体构造，也不能返回给页面。

新密码至少 8 位，只允许 33 至 126 范围的可见 ASCII 字符，并同时包含大写、小写和数字；符号可选。
中文、中文标点和所有空白字符都会拒绝，不自动删除空格。旧密码仍按原哈希校验，不使用新规则拒绝旧账号登录。
本机设置工具和在线改密使用同一份新密码检查。

改密保存成功后，在锁内撤销该账号的全部会话。随后本次请求返回 200 Success，并在同一响应中清 Cookie。
页面收到成功后显示提示并回登录页，不需要再请求 /api/auth/me 来判断改密是否成功。
保存失败返回对应错误，原密码和原会话保留。其他账号的会话不受影响。

| 失败原因 | HTTP / code |
| --- | --- |
| 当前密码错误、新密码不合规则、入参非法 | 400 InvalidInput |
| 无会话、过期、账号或凭据失效 | 401 AuthenticationRequired |
| 角色或归属不符 | 403 PermissionDenied |
| CSRF 或登录来源不符 | 403 CsrfRejected |
| 业务冲突 | 409，保留实际业务 code |
| 保存失败 | 500，保留实际 Storage* code，但不返回服务器路径 |
| 内部异常 | 500 InternalError，不输出异常堆栈或内部消息 |

`ResultSource::Internal` 专门用于内部异常；不要把“非 PermissionDenied”统一判为 401。
`AuthenticationRequired` 是追加的结果枚举，不改变已有结果编号或 JSON 快照格式。

## 阶段边界

本阶段没有合并成员分支，没有修改教师停用政策，也没有增加强制首次改密、管理员在线重置他人密码或管理员账号管理页面。
会话只保存在进程内存中，服务器重启后需要重新登录。
三端页面接入、三人代码合并后的完整回归和正式项目记录更新仍属于后续阶段。

## 本轮验证记录（2026-11-28）

Windows x64、MSVC 2022，使用 CMake Ninja Multi-Config 构建：

| 配置 | Debug | Release |
| --- | --- | --- |
| 默认，关闭会话和 libsodium | 11/11 | 9/9 |
| 打开会话和官方动态 libsodium | 15/15 | 13/13 |

密码服务在 Debug、Release 各通过 67 项检查。真实本机 HTTP 会话测试在两种配置下各通过 389 项检查。
覆盖三角色登录和改密、错误来源映射、来源检查、跨会话 CSRF、改密失败不撤销会话、改密成功撤销同账号全部会话、
空闲续期与最长时限、静态页面不续期、停用与改号失效、重启失效，以及两个学生争抢最后一个名额。
另外验证旧版中文密码仍可登录并换成符合新规则的密码，计算哈希期间其他业务修改不会被密码发布覆盖。

Zhu 的 `http_teacher_admin_session_routes.cpp` 连同其公共头文件在独立检查副本中通过 Debug、Release 编译。
这是接口兼容性检查，没有合并分支，也不代表教师和管理员页面已完成真实联调。
本轮运行日志留在工作区根目录 `tmp/session-20261128/`，不进入源码提交。
