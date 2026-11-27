#pragma once

#include "login_service.hpp"
#include "persistent_registration_service.hpp"
#include "student_query_and_timetable_services.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace course_registration {

// 学生端会话接口（控制台/接口层）。登录账号由上层会话提供；
// 本类只校验"学生存在且账号启用"，并把后续读写限定在该学生本人。
// 正式流程用 login(LoginService&, 账号, 密码)；无密码的模拟登录只留给演示。
class StudentInterface {
public:
    StudentInterface(SystemContext& context, StorageOptions options);

    // 用学号登录（模拟，不校验密码）。账号不存在返回 RecordNotFound；
    // 账号停用返回 PermissionDenied。比较规则与 find_student_by_number 一致。
    OperationResult login(const std::string& account_number);

    // 用统一认证入口登录：账号 + 密码交给 LoginService 校验，
    // 成功且角色为学生才建立会话；任何失败都回到未登录。
    OperationResult login(LoginService& login_service,
                          const std::string& account_number,
                          std::string_view password);

    void logout();
    bool logged_in() const { return logged_in_; }
    const UserIdentity& identity() const { return identity_; }

    OperationResult search(const std::string& keyword,
                           std::vector<CourseView>& result) const;
    OperationResult course_detail(int course_id, CourseView& result) const;
    OperationResult available_seats(int teaching_class_id,
                                    SeatsView& result) const;
    OperationResult timetable(std::vector<TimetableEntry>& result) const;
    OperationResult enroll(int teaching_class_id);
    OperationResult drop(int teaching_class_id);
    OperationResult my_status(int& active, int& dropped) const;

private:
    OperationResult require_login() const;

    SystemContext& context_;
    StorageOptions options_;
    UserIdentity identity_{};
    bool logged_in_ = false;
};

}  // namespace course_registration
