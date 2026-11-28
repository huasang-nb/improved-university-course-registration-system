#pragma once

// ============================================================================
// HTTP 接入的公共入口（Zhu Ziliang 起草，待团队确认后由公共骨架实现）
//
// 本文件只定义"教师/管理员接口需要从公共骨架拿到什么"，不包含任何 HTTP
// 框架或会话实现。这样两件事可以并行：
//   1. Yuhua 写公共骨架（会话、CSRF、静态页面、启动参数）；
//   2. 我在 handler 层写路由与页面，用本文件的 Request/Services 直接单测，
//      不依赖真实服务器。
// 骨架落地后，把本文件替换成他的头文件（或让他按这些签名实现），
// handler 代码不需要改。
//
// 契约来源：documentation/decisions/0001-http-contract.md
//   - 会话身份由公共层解析 Cookie 得到，handler 只信任 identity；
//   - 写操作的 CSRF 由公共层校验，结果放进 csrf_ok；
//   - 业务读写共用一把 mutex，覆盖"身份核对 + 业务调用 + 保存"；
//     这把锁由会话骨架的 with_business_lock() 持有，dispatch() 本身不再加锁；
//   - 响应外壳 {code, message, data} 与状态码映射见 status_for()。
// ============================================================================

#include <map>
#include <mutex>
#include <optional>
#include <string>

#include "persistent_administration_service.hpp"
#include "teacher_and_administration_services.hpp"

namespace course_registration {
namespace http {

// 一次请求中与业务有关的部分。公共层负责把 HTTP 细节转换成它。
struct Request {
    std::string method;  // "GET" / "POST" / "PUT" / "PATCH" / "DELETE"
    std::string path;    // 例如 "/api/admin/classes/10/capacity"
    std::map<std::string, std::string> query;
    std::string body;  // 原始 JSON 文本；GET 时为空
    // 公共会话层解析出的身份；没有有效会话时为空。
    std::optional<UserIdentity> identity;
    // 写操作是否通过 CSRF 校验；GET 请求忽略此字段。
    bool csrf_ok = false;
};

struct Response {
    int status = 200;
    std::string body;  // 完整 JSON 外壳
};

// handler 层依赖的共享对象。三者由公共骨架在启动时构造一次，
// 生命周期覆盖整个服务运行期。
// context 只读：handler 不直接改共享状态，写操作一律走 admin/teacher 服务，
// 这样“保存成功才生效”的网关不会被绕过。
struct Services {
    const SystemContext& context;
    PersistentAdministratorService& admin;
    TeacherService& teacher;
    std::mutex& business_mutex;
};

// ResultCode -> HTTP 状态码（团队总手册表 4）。
// 200 成功；400 InvalidInput；403 PermissionDenied；404 记录不存在；
// 409 状态冲突；500 存储失败。认证与 CSRF 由 handler 层直接给出
// 401 AuthenticationRequired / 403 CsrfRejected。
int status_for(ResultCode code);

// ResultCode -> 错误名（与枚举同名，页面按名字判断，不解析中文消息）。
const char* name_for(ResultCode code);

}  // namespace http
}  // namespace course_registration
