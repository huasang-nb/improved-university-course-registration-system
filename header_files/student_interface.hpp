#pragma once

#include "persistent_registration_service.hpp"
#include "student_query_and_timetable_services.hpp"

#include <string>
#include <vector>

namespace course_registration {

// 学生端会话接口（控制台/接口层）。登录账号由上层会话提供；
// 本类只校验"学生存在且账号启用"，并把后续读写限定在该学生本人。
// 当前为同步单进程原型，不实现密码验证或会话管理。
class StudentInterface {
public:
    StudentInterface(SystemContext& context, StorageOptions options);

    // 用学号登录。账号不存在返回 RecordNotFound；账号停用返回 PermissionDenied。
    // 比较规则与 find_student_by_number 一致（去首尾空白 + 转小写）。
    OperationResult login(const std::string& account_number);
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
