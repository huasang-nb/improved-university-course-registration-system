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

// 分发一次请求。内部持有业务锁，覆盖身份核对、业务调用与保存。
// 未匹配到路由时返回 404 RecordNotFound。
Response dispatch(const Request& request, Services& services);

}  // namespace http
}  // namespace course_registration
