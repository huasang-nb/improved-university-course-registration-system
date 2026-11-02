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
struct RegistrationRule {
    bool registration_open;
    int maximum_credits;
};

enum class ResultCode {
    Success,
    RegistrationClosed,
    AlreadyEnrolled,
    TimeConflict,
    CreditLimitExceeded,
    TeachingClassFull,
    TeachingClassNotFound,
    ActiveEnrollmentNotFound
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

private:
    const Course* find_course(int course_id) const;
    const TeachingClass* find_teaching_class(int teaching_class_id) const;

    std::vector<Course> courses_;
    std::vector<TeachingClass> teaching_classes_;
    std::vector<Enrollment> enrollments_;
    RegistrationRule rule_;
};

}  // namespace course_registration
