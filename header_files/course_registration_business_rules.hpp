#pragma once

#include <string>
#include <vector>

namespace course_registration {

// 一次上课的时间。分钟从当天 00:00 起计算，例如 09:00 是 540。
struct ClassMeeting {
    int day_of_week;
    int start_minute;
    int end_minute;
    std::string room;
};

struct Course {
    int course_id;
    std::string course_code;
    std::string course_name;
    int credits;
};

// 一个 Course 可以开多个 TeachingClass；容量属于具体教学班。
struct TeachingClass {
    int teaching_class_id;
    int course_id;
    int teacher_id;
    int capacity;
    std::vector<ClassMeeting> meetings;
};

enum class EnrollmentStatus { Active, Dropped };

struct Enrollment {
    int student_id;
    int teaching_class_id;
    EnrollmentStatus status;
};

// 目前用示例数据设置规则；之后再由管理员功能提供真实配置。
// [Zhu Ziliang 追加，待 Zhang Yuhua 复核] 增加选课开始/结束时间（UC15 需要）。
// 两个字段追加在末尾并带默认值，原来的 {true, 7} 形式初始化依然有效，
// 也不会在 -Wall -Wextra 下产生 missing-field-initializers 警告。
// 格式为 "YYYY-MM-DDTHH:MM"；空字符串表示该时间尚未设置，此时 registration_open
// 仍由管理员手动开关决定，不引入新的必填字段。
struct RegistrationRule {
    bool registration_open;
    int maximum_credits;
    std::string opens_at{};
    std::string closes_at{};
};

enum class ResultCode {
    Success,
    RegistrationClosed,
    AlreadyEnrolled,
    TimeConflict,
    CreditLimitExceeded,
    TeachingClassFull,
    TeachingClassNotFound,
    ActiveEnrollmentNotFound,
    // [Zhu Ziliang 追加，待 Zhang Yuhua 复核] 教师/管理员模块需要的失败原因。
    // 追加在末尾，上面已有编号的含义不变，现有调用方无需改动。
    PermissionDenied,       // 角色或归属不符，例如教师读取他人的教学班
    InvalidInput,           // 字段格式/取值为空、负数、非法时段等
    RecordNotFound,         // 课程、教师、学生等记录不存在
    DuplicateIdentifier,    // 编号或学号/工号重复
    CapacityBelowEnrollment,// 新容量小于当前有效选课人数
    CatalogInUse            // 记录仍被引用（课程仍有教学班、教学班仍有有效选课）
};

struct OperationResult {
    ResultCode code;
    std::string message;
};

// 第一周只实现内存中的选课业务规则，不负责登录、网页和数据库。
class RegistrationService {
public:
    RegistrationService(const std::vector<Course>& courses,
                        const std::vector<TeachingClass>& teaching_classes,
                        RegistrationRule rule);

    OperationResult enroll(int student_id, int teaching_class_id);
    OperationResult drop(int student_id, int teaching_class_id);
    int remaining_seats(int teaching_class_id) const;
    int enrollment_count(int teaching_class_id) const;
    std::vector<Enrollment> active_enrollments(int student_id) const;

    // ---- [Zhu Ziliang 追加，待 Zhang Yuhua 复核] 教师/管理员模块只读接口 ----
    // 目的：名单、人数、余量和管理员一致性检查都从同一份选课记录取值，
    // 不另存计数，避免教师端与选课端显示不同人数。
    std::vector<Enrollment> all_enrollments() const;
    std::vector<Course> course_snapshot() const;
    std::vector<TeachingClass> teaching_class_snapshot() const;
    RegistrationRule registration_rule() const;

    // ---- [Zhu Ziliang 追加，待 Zhang Yuhua 复核] 管理员校验通过后的受控写入 ----
    // 只替换课程目录/教学班/规则，不改动 enrollments_；enroll() 的检查顺序不变。
    // 调用方必须先经 AdministratorService 校验，不得直接调用。
    void apply_catalog_change(const std::vector<Course>& courses,
                              const std::vector<TeachingClass>& teaching_classes);
    void apply_rule_change(const RegistrationRule& rule);

private:
    const Course* find_course(int course_id) const;
    const TeachingClass* find_teaching_class(int teaching_class_id) const;

    std::vector<Course> courses_;
    std::vector<TeachingClass> teaching_classes_;
    std::vector<Enrollment> enrollments_;
    RegistrationRule rule_;
};

}  // namespace course_registration
