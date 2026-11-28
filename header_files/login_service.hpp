#pragma once

#include "teacher_and_administration_services.hpp"

#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace course_registration {

struct LoginResult {
    OperationResult result;
    // 仅认证成功时持有身份。失败时调用方可直接检查 result.code。
    std::optional<UserIdentity> identity;
};

// 校验器接收从 SystemContext 查到的账号身份。密码按输入原样传递，
// 因为首尾空格和大小写可能就是密码的一部分。
using PasswordVerifier =
    std::function<bool(const UserIdentity& account, std::string_view password)>;

class LoginService {
public:
    LoginService(const SystemContext& context, PasswordVerifier verify_password);

    // 跨角色识别账号。匹配唯一、账号启用且密码通过校验后才发放身份。
    // 共享上下文的调用方负责协调认证读取和人员资料更新。
    LoginResult login(const std::string& account_number,
                      std::string_view password) const;

private:
    const SystemContext& context_;  // 与账号维护共用，始终读取当前记录。
    PasswordVerifier verify_password_;
};

}  // namespace course_registration
