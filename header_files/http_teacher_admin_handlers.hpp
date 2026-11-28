#pragma once

// ============================================================================
// 教师 / 管理员 HTTP 接口的请求分发（Zhu Ziliang）
//
// dispatch() 与 HTTP 框架无关：输入是 http::Request，输出是 http::Response。
// 这样有三点好处：
//   1. 可以直接在单元测试里构造请求，验证越权、非法入参、CSRF 缺失等失败路径；
//   2. cpp-httplib 适配层只剩下"填 Request / 写 Response"两行；
//   3. 公共骨架更换框架时，本文件不用改。
//
// 路由与契约：documentation/decisions/0001-http-contract.md 第 21–25 条。
// ============================================================================

#include "http_support.hpp"

namespace course_registration {
namespace http {

// 分发一次请求。调用方必须已经持有共同业务锁：
// 会话骨架用 with_business_lock() 持锁后调用它，“身份核对 + 业务 + 保存”同处一个临界区。
// 未匹配到路由时返回 404 RecordNotFound。
Response dispatch(const Request& request, Services& services);

// 把可能含服务器内部路径的存储错误信息替换成页面可读的提示；错误码不变。
// 路由生成响应和正式适配层写回错误时都必须经过它，避免一处改了一处漏掉。
OperationResult public_result(const OperationResult& result);

// 自带加锁的便捷入口，只给独立服务器与本模块自测使用；
// 接入组长会话骨架后不要用它，否则会和 with_business_lock() 重复加锁。
Response dispatch_locked(const Request& request, Services& services);

}  // namespace http
}  // namespace course_registration
