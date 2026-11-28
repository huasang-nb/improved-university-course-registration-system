#include "http_teacher_admin_routes.hpp"

// ============================================================================
// cpp-httplib 适配层（Zhu Ziliang）
//
// 这一层只做三件事：
//   1. 把 httplib::Request 转成与框架无关的 http::Request；
//   2. 调 http::dispatch()（路由表、权限、JSON 校验都在那里）；
//   3. 把 http::Response 写回 httplib::Response。
//
// 会话身份与 CSRF 结果由公共骨架提供（团队总手册第四节）：
// register_teacher_admin_routes(server, services, lookup) 的第三个参数就是
// 那个回调。没有公共骨架时可以用两参数重载做冒烟测试，此时所有请求都按
// "未登录"处理，正好验证 401 分支。
// ============================================================================

#include <functional>
#include <map>
#include <optional>
#include <string>

#include <httplib.h>
#include <nlohmann/json.hpp>

#include "http_teacher_admin_handlers.hpp"

namespace course_registration {
namespace http {
namespace {

Request to_request(const httplib::Request& source, const SessionView& session) {
    Request request;
    request.method = source.method;
    request.path = source.path;
    for (const auto& entry : source.params) {
        request.query.emplace(entry.first, entry.second);
    }
    request.body = source.body;
    request.identity = session.identity;
    request.csrf_ok = session.csrf_ok;
    return request;
}

void write_response(const Response& source, httplib::Response& target) {
    target.status = source.status;
    target.set_content(source.body, "application/json; charset=utf-8");
}

}  // namespace

void register_teacher_admin_routes(httplib::Server& server, Services& services,
                                  SessionLookup lookup) {
    const auto handler = [&services, lookup = std::move(lookup)](const httplib::Request& request,
                                                                 httplib::Response& response) {
        const SessionView session = lookup ? lookup(request) : SessionView{};
        const Response result = dispatch_locked(to_request(request, session), services);
        write_response(result, response);
    };

    server.Get(R"(/api/(teacher|admin)/.*)", handler);
    server.Post(R"(/api/(teacher|admin)/.*)", handler);
    server.Put(R"(/api/(teacher|admin)/.*)", handler);
    server.Patch(R"(/api/(teacher|admin)/.*)", handler);
    server.Delete(R"(/api/(teacher|admin)/.*)", handler);
}

void register_teacher_admin_routes(httplib::Server& server, Services& services) {
    register_teacher_admin_routes(server, services, SessionLookup{});
}

}  // namespace http
}  // namespace course_registration
