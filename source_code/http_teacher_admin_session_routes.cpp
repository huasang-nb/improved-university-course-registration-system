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
//   - 401 / 403 的区分来自 ResultSource：会话失败 401，角色不符 403。
// ============================================================================

#include "session.hpp"

#include <httplib.h>

#include <mutex>

#include "http_teacher_admin_handlers.hpp"
#include "http_teacher_admin_routes.hpp"
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
            // 复核失败不一律当 401（2026-11-28 对齐）：
            //   角色不符            -> 403 PermissionDenied
            //   存储/内部异常        -> 500（InternalError 或原 Storage* 码）
            // 会话本身在进入临界区前已由 require_session() 检查过；若锁内发现身份失效，
            // 由公共层给出可区分的错误码后再补一支 401，这里不猜测。
            ResultSource source = ResultSource::Internal;
            if (locked.code == ResultCode::PermissionDenied) {
                source = ResultSource::Permission;
            } else if (locked.code == ResultCode::StorageIoError ||
                       locked.code == ResultCode::StorageParseError ||
                       locked.code == ResultCode::StorageSchemaMismatch ||
                       locked.code == ResultCode::StorageIntegrityError ||
                       locked.code == ResultCode::StorageBackupMissing) {
                source = ResultSource::Business;  // 保留实际 Storage* 错误码
            }
            write_result(out, locked, source);
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
