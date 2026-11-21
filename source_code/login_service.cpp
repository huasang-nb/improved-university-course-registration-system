#include "login_service.hpp"

#include <stdexcept>
#include <utility>

namespace course_registration {

LoginService::LoginService(const SystemContext& context,
                           PasswordVerifier verify_password)
    : context_(context), verify_password_(std::move(verify_password)) {
    // 缺少校验器是配置错误，构造服务时直接报告。
    if (!verify_password_) {
        throw std::invalid_argument("LoginService requires a password verifier");
    }
}

LoginResult LoginService::login(const std::string& account_number,
                                std::string_view password) const {
    const std::string requested_account_key = normalize_account_key(account_number);
    if (requested_account_key.empty() || password.empty()) {
        return {{ResultCode::InvalidInput, "account and password are required"},
                std::nullopt};
    }

    struct AccountMatch {
        UserIdentity identity;
        bool active;
    };
    std::optional<AccountMatch> matched_account;
    bool account_is_ambiguous = false;
    const auto consider_account = [&](Role role, int user_id,
                                      const std::string& stored_account,
                                      bool account_active) {
        if (normalize_account_key(stored_account) != requested_account_key) {
            return;
        }
        if (matched_account) {
            account_is_ambiguous = true;
            return;
        }
        matched_account = AccountMatch{
            UserIdentity{user_id, stored_account, role}, account_active};
    };

    // 停用账号仍占用账号名，所以匹配时也要查它。查完整个上下文，
    // 避免旧数据存在重名时按扫描顺序选中其中一条记录。
    for (const Student& student : context_.students()) {
        consider_account(Role::Student, student.student_id, student.student_number,
                         student.account_active);
    }
    for (const Teacher& teacher : context_.teachers()) {
        consider_account(Role::Teacher, teacher.teacher_id, teacher.teacher_number,
                         teacher.account_active);
    }
    for (const Administrator& administrator : context_.administrators()) {
        consider_account(Role::Administrator, administrator.administrator_id,
                         administrator.account_number, administrator.account_active);
    }

    if (account_is_ambiguous) {
        return {{ResultCode::DuplicateIdentifier, "login account is not unique"},
                std::nullopt};
    }
    // 先确认身份唯一且启用，再调用凭据校验器。普通登录失败使用统一提示。
    if (!matched_account || !matched_account->active ||
        !verify_password_(matched_account->identity, password)) {
        return {{ResultCode::PermissionDenied, "sign-in failed"}, std::nullopt};
    }
    return {{ResultCode::Success, "sign-in succeeded"},
            std::move(matched_account->identity)};
}

}  // namespace course_registration
