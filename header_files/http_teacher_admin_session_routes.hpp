#pragma once

// ============================================================================
// 教师 / 管理员路由接入公共会话骨架（Yuhua 的 session.hpp）的声明与映射规则
//
// 只有 COURSE_REGISTRATION_WITH_SESSION=ON 时参与构建。把 result_source_for()
// 单独导出，是为了让自测能直接核对“锁内失败到底返回 401 / 403 / 500”：
// 这段映射一旦写错，页面会把“请重新登录”显示在保存失败或内部异常上。
// ============================================================================

#include <httplib.h>

#include "http_support.hpp"
#include "session.hpp"

namespace course_registration {
namespace http {

// 把 with_business_lock() 的返回值映射成公共层的 ResultSource：
//   AuthenticationRequired        -> Session     (401)
//   PermissionDenied              -> Permission  (403)
//   Storage*                      -> Business    (500，保留实际 Storage* 码)
//   其他（无法识别的内部失败）      -> Internal    (500 InternalError)
// 业务错误（400/404/409）不会走到这里：它们在持锁回调里已经写入响应。
ResultSource result_source_for(ResultCode code);

// 注册教师与管理员的全部接口；身份、CSRF 与业务锁都来自公共会话层。
// 同一个 server 上只调用一次，且不要与 register_teacher_admin_routes() 同时使用。
void register_teacher_admin_routes_with_session(httplib::Server& server);

}  // namespace http
}  // namespace course_registration
