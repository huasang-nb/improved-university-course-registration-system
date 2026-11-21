// 学生端会话接口（Zhang Yikai，第 3 周）
//
// 把登录身份、学生查询与选退课持久化接到同一个 SystemContext 上：
//   - 登录只认"学生存在且账号启用"，比较规则复用 normalize_account_key；
//   - 查询走 StudentQueryService；
//   - 选退课走 PersistentRegistrationService（业务成功才保存，保存成功才发布）。

#include "student_interface.hpp"

#include <utility>

namespace course_registration {

StudentInterface::StudentInterface(SystemContext& context,
                                   StorageOptions options)
    : context_(context), options_(std::move(options)) {}

OperationResult StudentInterface::login(const std::string& account_number) {
    // 切换账号失败时回到未登录，避免继续沿用上一个学生的身份。
    logout();
    const Student* student = context_.find_student_by_number(account_number);
    if (student == nullptr) {
        return {ResultCode::RecordNotFound, "student account not found"};
    }
    if (!student->account_active) {
        return {ResultCode::PermissionDenied, "student account is disabled"};
    }
    // 只采信从存储/内存中查到的编号，不使用调用方传入的其他学生编号。
    identity_ = UserIdentity{student->student_id, student->student_number,
                             Role::Student};
    logged_in_ = true;
    return {ResultCode::Success,
            "logged in as " + student->student_number};
}

void StudentInterface::logout() {
    identity_ = UserIdentity{};
    logged_in_ = false;
}

OperationResult StudentInterface::require_login() const {
    if (!logged_in_) {
        return {ResultCode::PermissionDenied, "please log in first"};
    }
    // 登录后账号可能被停用，或学号被管理员改过：会话账号必须与当前学号
    // 按统一规则（去首尾空白 + 转小写）一致，否则读取和选退课都拒绝。
    const Student* student = context_.find_student(identity_.user_id);
    if (student == nullptr || !student->account_active ||
        normalize_account_key(identity_.account_number) !=
            normalize_account_key(student->student_number)) {
        return {ResultCode::PermissionDenied,
                "student account is no longer valid"};
    }
    return {ResultCode::Success, "ok"};
}

OperationResult StudentInterface::search(
    const std::string& keyword, std::vector<CourseView>& result) const {
    const OperationResult login = require_login();
    if (login.code != ResultCode::Success) {
        result.clear();
        return login;
    }
    StudentQueryService query(context_);
    return query.search_courses(keyword, CourseSearchFilter{}, result);
}

OperationResult StudentInterface::course_detail(int course_id,
                                                CourseView& result) const {
    const OperationResult login = require_login();
    if (login.code != ResultCode::Success) {
        result = CourseView{};
        return login;
    }
    StudentQueryService query(context_);
    return query.course_detail(course_id, result);
}

OperationResult StudentInterface::available_seats(int teaching_class_id,
                                                  SeatsView& result) const {
    const OperationResult login = require_login();
    if (login.code != ResultCode::Success) {
        result = SeatsView{};
        return login;
    }
    StudentQueryService query(context_);
    return query.view_available_seats(teaching_class_id, result);
}

OperationResult StudentInterface::timetable(
    std::vector<TimetableEntry>& result) const {
    const OperationResult login = require_login();
    if (login.code != ResultCode::Success) {
        result.clear();
        return login;
    }
    StudentQueryService query(context_);
    return query.view_timetable(identity_, result);
}

OperationResult StudentInterface::enroll(int teaching_class_id) {
    const OperationResult login = require_login();
    if (login.code != ResultCode::Success) {
        return login;
    }
    PersistentRegistrationService service(context_, options_);
    return service.enroll(identity_, teaching_class_id);
}

OperationResult StudentInterface::drop(int teaching_class_id) {
    const OperationResult login = require_login();
    if (login.code != ResultCode::Success) {
        return login;
    }
    PersistentRegistrationService service(context_, options_);
    return service.drop(identity_, teaching_class_id);
}

OperationResult StudentInterface::my_status(int& active, int& dropped) const {
    const OperationResult login = require_login();
    if (login.code != ResultCode::Success) {
        active = 0;
        dropped = 0;
        return login;
    }
    active = 0;
    dropped = 0;
    for (const Enrollment& record : context_.all_enrollments()) {
        if (record.student_id != identity_.user_id) {
            continue;
        }
        if (record.status == EnrollmentStatus::Active) {
            ++active;
        } else {
            ++dropped;
        }
    }
    return {ResultCode::Success, "ok"};
}

}  // namespace course_registration
