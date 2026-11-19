// 管理员修改的持久化入口实现（Zhu Ziliang）

#include "persistent_administration_service.hpp"

#include <string>
#include <utility>

namespace course_registration {

PersistentAdministratorService::PersistentAdministratorService(
    SystemContext& official, const StorageOptions& options)
    : official_(official), options_(options) {}

OperationResult PersistentAdministratorService::check_actor(
    const UserIdentity& actor) const {
    if (actor.role != Role::Administrator) {
        return {ResultCode::PermissionDenied,
                "only an administrator can change administration data"};
    }
    // 账号必须能在当前管理员名单里找到，且账号、编号、启停状态都对得上，
    // 否则视为伪造或已撤销的会话。
    const std::string key = normalize_account_key(actor.account_number);
    for (const Administrator& administrator : official_.administrators()) {
        if (normalize_account_key(administrator.account_number) != key) {
            continue;
        }
        if (administrator.administrator_id != actor.user_id) {
            break;
        }
        if (!administrator.account_active) {
            return {ResultCode::PermissionDenied,
                    "administrator " + administrator.account_number +
                        " is not an active account"};
        }
        return {ResultCode::Success, "ok"};
    }
    return {ResultCode::PermissionDenied,
            "administrator account " + actor.account_number +
                " is not a known active administrator"};
}

#define PAS_FORWARD(method, ...)                                        \
    return commit(actor, [&](AdministratorService& service) {           \
        return service.method(actor, __VA_ARGS__);                      \
    })

#define PAS_FORWARD0(method)                                            \
    return commit(actor, [&](AdministratorService& service) {           \
        return service.method(actor);                                   \
    })

// ---- 课程与教学班 ----
OperationResult PersistentAdministratorService::create_course_with_teaching_class(
    const UserIdentity& actor, const Course& course,
    const TeachingClass& first_teaching_class) {
    PAS_FORWARD(create_course_with_teaching_class, course, first_teaching_class);
}

OperationResult PersistentAdministratorService::update_course(
    const UserIdentity& actor, const Course& course) {
    PAS_FORWARD(update_course, course);
}

OperationResult PersistentAdministratorService::remove_course(
    const UserIdentity& actor, int course_id) {
    PAS_FORWARD(remove_course, course_id);
}

OperationResult PersistentAdministratorService::create_teaching_class(
    const UserIdentity& actor, const TeachingClass& teaching_class) {
    PAS_FORWARD(create_teaching_class, teaching_class);
}

OperationResult PersistentAdministratorService::update_teaching_class(
    const UserIdentity& actor, const TeachingClass& teaching_class) {
    PAS_FORWARD(update_teaching_class, teaching_class);
}

OperationResult PersistentAdministratorService::remove_teaching_class(
    const UserIdentity& actor, int teaching_class_id) {
    PAS_FORWARD(remove_teaching_class, teaching_class_id);
}

OperationResult PersistentAdministratorService::set_teaching_class_capacity(
    const UserIdentity& actor, int teaching_class_id, int capacity) {
    PAS_FORWARD(set_teaching_class_capacity, teaching_class_id, capacity);
}

// ---- 选课规则 ----
OperationResult PersistentAdministratorService::set_registration_period(
    const UserIdentity& actor, const std::string& opens_at,
    const std::string& closes_at) {
    PAS_FORWARD(set_registration_period, opens_at, closes_at);
}

OperationResult PersistentAdministratorService::set_maximum_credits(
    const UserIdentity& actor, int maximum_credits) {
    PAS_FORWARD(set_maximum_credits, maximum_credits);
}

OperationResult PersistentAdministratorService::close_registration(
    const UserIdentity& actor) {
    PAS_FORWARD0(close_registration);
}

// ---- 人员资料 ----
OperationResult PersistentAdministratorService::create_student(
    const UserIdentity& actor, const Student& student) {
    PAS_FORWARD(create_student, student);
}

OperationResult PersistentAdministratorService::update_student(
    const UserIdentity& actor, const Student& student) {
    PAS_FORWARD(update_student, student);
}

OperationResult PersistentAdministratorService::remove_student(
    const UserIdentity& actor, int student_id) {
    PAS_FORWARD(remove_student, student_id);
}

OperationResult PersistentAdministratorService::create_teacher(
    const UserIdentity& actor, const Teacher& teacher) {
    PAS_FORWARD(create_teacher, teacher);
}

OperationResult PersistentAdministratorService::update_teacher(
    const UserIdentity& actor, const Teacher& teacher) {
    PAS_FORWARD(update_teacher, teacher);
}

OperationResult PersistentAdministratorService::remove_teacher(
    const UserIdentity& actor, int teacher_id) {
    PAS_FORWARD(remove_teacher, teacher_id);
}

#undef PAS_FORWARD0
#undef PAS_FORWARD

}  // namespace course_registration
