#pragma once

// ============================================================================
// 教师 / 管理员接口的 cpp-httplib 注册入口（Zhu Ziliang）
//
// 交给公共骨架（Yuhua）的接入点：
//   register_teacher_admin_routes(server, services, lookup);
// 其中 lookup 负责把请求变成"当前会话身份 + CSRF 是否通过"。骨架里已经有
// 会话表与 Cookie 解析，实现这个回调即可，不需要改 handler 层。
//
// 路由清单见 documentation/decisions/0001-http-contract.md 第 21–25 条。
// ============================================================================

#include <functional>
#include <optional>

#include <httplib.h>

#include "http_support.hpp"

namespace course_registration {
namespace http {

// 公共会话层给出的每请求视图。
struct SessionView {
    std::optional<UserIdentity> identity;
    bool csrf_ok = false;
};

// 由公共骨架实现：读取 Cookie -> 查会话 -> 得到身份；写操作校验 CSRF。
using SessionLookup = std::function<SessionView(const httplib::Request&)>;

// 注册教师与管理员的全部接口。
void register_teacher_admin_routes(httplib::Server& server, Services& services,
                                  SessionLookup lookup);

// 没有会话骨架时的重载：所有请求按未登录处理（冒烟测试用）。
void register_teacher_admin_routes(httplib::Server& server, Services& services);

}  // namespace http
}  // namespace course_registration
