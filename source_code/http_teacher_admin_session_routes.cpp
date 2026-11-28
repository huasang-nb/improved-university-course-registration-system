// ============================================================================
// 教师 / 管理员路由接入组长的公共会话骨架（Yuhua 的 session.hpp）
//
// 这个文件只有在公共骨架落地后才参与构建：CMake 选项
// COURSE_REGISTRATION_WITH_SESSION=ON，且 session.hpp 在 include 路径上。
//
// 与 development server 的区别：
//   - 身份来自 require_session()，CSRF 来自 require_csrf()，锁来自
//     with_business_lock()；
//   - 本文件不自行加锁，因此调用 dispatch()（而不是 dispatch_locked()），
//     避免同一把锁被重复获取；
//   - 401 / 403 的区分来自 ResultSource：会话失败或锁内身份失效 401，角色不符 403，
//     存储失败 500 保留 Storage* 码，其余内部失败 500 InternalError。
//   - 本文件只注册 /api/teacher/* 与 /api/admin/*。统一认证路由（/api/auth/*）
//     由 SessionRuntime::register_auth_routes() 注册一次，这里不重复注册。
// ============================================================================

#include "session.hpp"

#include <httplib.h>

#include <mutex>

#include "http_teacher_admin_handlers.hpp"  // public_result()：过滤内部路径
#include "http_teacher_admin_routes.hpp"
#include "http_teacher_admin_session_routes.hpp"
#include "http_support.hpp"

namespace course_registration {
namespace http {
namespace {

Request to_request(const httplib::Request& raw) {
    Request request;
    request.method = raw.method;
    request.path = raw.path;
    for (const auto& item : raw.params) {
        request.query[item.first] = item.second;
    }
    request.body = raw.body;
    return request;
}

void write_response(const Response& source, httplib::Response& target) {
    target.status = source.status;
    target.set_content(source.body, "application/json; charset=utf-8");
}

Role role_for_path(const std::string& path) {
    // 教师路由只有 /api/teacher/*，其余落在本文件里的都是管理员路由。
    return path.rfind("/api/admin/", 0) == 0 ? Role::Administrator : Role::Teacher;
}

bool is_write_method(const std::string& method) {
    return method != "GET" && method != "HEAD" && method != "OPTIONS";
}

}  // namespace

ResultSource result_source_for(ResultCode code) {
    switch (code) {
        case ResultCode::AuthenticationRequired:
            return ResultSource::Session;
        case ResultCode::PermissionDenied:
            return ResultSource::Permission;
        case ResultCode::StorageIoError:
        case ResultCode::StorageParseError:
        case ResultCode::StorageSchemaMismatch:
        case ResultCode::StorageIntegrityError:
        case ResultCode::StorageBackupMissing:
            return ResultSource::Business;
        default:
            return ResultSource::Internal;
    }
}

void register_teacher_admin_routes_with_session(httplib::Server& server) {
    const auto handler = [](const httplib::Request& raw, httplib::Response& out) {
        // 1. 会话：无 Cookie、过期、账号停用/删除/不匹配一律 401。
        const SessionCheck session = require_session(raw);
        if (!session.identity) {
            write_result(out, session.result, ResultSource::Session);
            return;
        }

        // 2. CSRF：GET/HEAD/OPTIONS 直接通过；写操作必须通过。
        const bool write = is_write_method(raw.method);
        if (write) {
            const OperationResult csrf = require_csrf(raw);
            if (csrf.code != ResultCode::Success) {
                write_result(out, csrf, ResultSource::Csrf);
                return;
            }
        }

        Request request = to_request(raw);
        request.identity = session.identity;
        request.csrf_ok = true;

        // 3. 持锁后复核身份与角色，业务调用和保存在同一临界区。
        Response response;
        const OperationResult locked = with_business_lock(
            *session.identity, role_for_path(raw.path), [&](LockedServices& locked) {
                // dispatch() 只在锁内读取 context 并调用服务；写操作由服务负责保存。
                std::mutex unused_mutex;
                Services services{locked.context(), locked.admin_service(),
                                  locked.teacher_service(), unused_mutex};
                response = dispatch(request, services);
            });

        if (locked.code != ResultCode::Success) {
            // 锁内复核失败必须分开处理，不能只按“是不是 PermissionDenied”二元判断：
            //   AuthenticationRequired -> 401（会话在拿锁前还有效，进锁后账号被停用/删除/改号）
            //   PermissionDenied       -> 403（角色不符）
            //   Storage*               -> 500 且保留实际错误码（保存/读取失败）
            //   其他                    -> 500 InternalError（无法识别，不伪装成会话失效）
            // 业务本身的 400/404/409 不在这一步，它们在 callback 内已经写进 response。
            write_result(out, public_result(locked), result_source_for(locked.code));
            return;
        }

        write_response(response, out);
    };

    server.Get(R"(/api/(teacher|admin)/.*)", handler);
    server.Post(R"(/api/(teacher|admin)/.*)", handler);
    server.Put(R"(/api/(teacher|admin)/.*)", handler);
    server.Patch(R"(/api/(teacher|admin)/.*)", handler);
    server.Delete(R"(/api/(teacher|admin)/.*)", handler);
}

}  // namespace http
}  // namespace course_registration
