#include "course_registration_business_rules.hpp"

#include <ctime>

namespace course_registration {
namespace {

// 默认时钟：本机当前时间，格式 "YYYY-MM-DDTHH:MM"，与 opens_at/closes_at 同构，
// 固定宽度可以直接按字符串比较先后。
std::string system_now() {
    std::time_t current = std::time(nullptr);
    std::tm* local_time = std::localtime(&current);
    if (local_time == nullptr) {
        return "1970-01-01T00:00";
    }
    char buffer[32] = {0};
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M", local_time);
    return std::string(buffer);
}

}  // namespace

RegistrationService::RegistrationService(
    const std::vector<Course>& courses,
    const std::vector<TeachingClass>& teaching_classes,
    RegistrationRule rule)
    : courses_(courses),
      teaching_classes_(teaching_classes),
      rule_(rule) {}

void RegistrationService::set_fixed_now_for_tests(
    const std::string& iso_minute) {
    fixed_now_ = iso_minute;
}

bool RegistrationService::registration_open_at(
    const std::string& moment) const {
    if (rule_.opens_at.empty() || rule_.closes_at.empty()) {
        return rule_.registration_open;  // 未设置时段，用管理员的手动开关
    }
    const bool after_open = moment >= rule_.opens_at;
    const bool before_close = moment < rule_.closes_at;
    return after_open && before_close;
}

bool RegistrationService::registration_open_now() const {
    const std::string moment = fixed_now_.empty() ? system_now() : fixed_now_;
    return registration_open_at(moment);
}

const Course* RegistrationService::find_course(int course_id) const {
    for (const Course& course : courses_) {
        if (course.course_id == course_id) {
            return &course;
        }
    }
    return nullptr;
}

const TeachingClass* RegistrationService::find_teaching_class(
    int teaching_class_id) const {
    for (const TeachingClass& teaching_class : teaching_classes_) {
        if (teaching_class.teaching_class_id == teaching_class_id) {
            return &teaching_class;
        }
    }
    return nullptr;
}

int RegistrationService::enrollment_count(int teaching_class_id) const {
    int count = 0;
    for (const Enrollment& enrollment : enrollments_) {
        if (enrollment.teaching_class_id == teaching_class_id &&
            enrollment.status == EnrollmentStatus::Active) {
            ++count;
        }
    }
    return count;
}

int RegistrationService::remaining_seats(int teaching_class_id) const {
    const TeachingClass* teaching_class = find_teaching_class(teaching_class_id);
    if (teaching_class == nullptr) {
        return -1;  // 对外查询时，用 -1 表示教学班不存在。
    }
    return teaching_class->capacity - enrollment_count(teaching_class_id);
}

std::vector<Enrollment> RegistrationService::active_enrollments(
    int student_id) const {
    std::vector<Enrollment> result;
    for (const Enrollment& enrollment : enrollments_) {
        if (enrollment.student_id == student_id &&
            enrollment.status == EnrollmentStatus::Active) {
            result.push_back(enrollment);
        }
    }
    return result;
}

OperationResult RegistrationService::enroll(
    int student_id, int teaching_class_id) {
    // 1. 只在选课开放期处理请求。按当前时间现算，不依赖保存下来的旧开关。
    if (!registration_open_now()) {
        return {ResultCode::RegistrationClosed, "Registration period closed"};
    }

    // 2. 教学班编号来自请求，在入口查一次。课程引用由初始化数据保证有效。
    const TeachingClass* requested_class = find_teaching_class(teaching_class_id);
    if (requested_class == nullptr) {
        return {ResultCode::TeachingClassNotFound, "Teaching class not found"};
    }
    const Course* requested_course = find_course(requested_class->course_id);

    // 后面三个检查都使用同一批已选教学班。
    std::vector<const TeachingClass*> current_classes;
    for (const Enrollment& enrollment : active_enrollments(student_id)) {
        current_classes.push_back(
            find_teaching_class(enrollment.teaching_class_id));
    }

    // 3. 同一门课程只能选一个教学班。
    for (const TeachingClass* current_class : current_classes) {
        if (current_class->course_id == requested_class->course_id) {
            return {ResultCode::AlreadyEnrolled, "Already enrolled in this course"};
        }
    }

    // 4. 两节课同一天，且时间有交叉，才算冲突。
    for (const TeachingClass* current_class : current_classes) {
        for (const ClassMeeting& current : current_class->meetings) {
            for (const ClassMeeting& requested : requested_class->meetings) {
                bool same_day = current.day_of_week == requested.day_of_week;
                bool times_overlap =
                    current.start_minute < requested.end_minute &&
                    requested.start_minute < current.end_minute;
                if (same_day && times_overlap) {
                    return {ResultCode::TimeConflict, "Time conflict"};
                }
            }
        }
    }

    // 5. 总学分不能超过当前规则中的上限。
    int current_credits = 0;
    for (const TeachingClass* current_class : current_classes) {
        const Course* course = find_course(current_class->course_id);
        current_credits += course->credits;
    }
    if (current_credits + requested_course->credits > rule_.maximum_credits) {
        return {ResultCode::CreditLimitExceeded, "Credit limit exceeded"};
    }

    // 6. 剩余名额由容量减去有效选课人数得到，不另存一份数字。
    if (enrollment_count(teaching_class_id) >= requested_class->capacity) {
        return {ResultCode::TeachingClassFull, "Teaching class full"};
    }

    // 7. 所有检查通过后，才改动选课记录。
    for (Enrollment& enrollment : enrollments_) {
        if (enrollment.student_id == student_id &&
            enrollment.teaching_class_id == teaching_class_id &&
            enrollment.status == EnrollmentStatus::Dropped) {
            enrollment.status = EnrollmentStatus::Active;
            return {ResultCode::Success, "Enrollment successful"};
        }
    }

    enrollments_.push_back(
        {student_id, teaching_class_id, EnrollmentStatus::Active});
    return {ResultCode::Success, "Enrollment successful"};
}

OperationResult RegistrationService::drop(
    int student_id, int teaching_class_id) {
    // 与 enroll() 使用同一个判断入口，避免两边规则不一致。
    if (!registration_open_now()) {
        return {ResultCode::RegistrationClosed, "Registration period closed"};
    }

    for (Enrollment& enrollment : enrollments_) {
        if (enrollment.student_id == student_id &&
            enrollment.teaching_class_id == teaching_class_id &&
            enrollment.status == EnrollmentStatus::Active) {
            enrollment.status = EnrollmentStatus::Dropped;
            // remaining_seats() 会据此自动增加一席。
            return {ResultCode::Success, "Drop successful"};
        }
    }
    return {ResultCode::ActiveEnrollmentNotFound,
            "Active enrollment not found"};
}

// ============================================================================
// [Zhu Ziliang 追加，待 Zhang Yuhua 复核] 教师/管理员模块需要的只读接口与
// 受控写入。这里只读取或替换目录与规则，不触碰 enrollments_，因此 enroll()
// 的检查顺序、名额计算方式和已有选课记录都保持原样。
// ============================================================================

std::vector<Enrollment> RegistrationService::all_enrollments() const {
    return enrollments_;
}

std::vector<Course> RegistrationService::course_snapshot() const {
    return courses_;
}

std::vector<TeachingClass> RegistrationService::teaching_class_snapshot() const {
    return teaching_classes_;
}

RegistrationRule RegistrationService::registration_rule() const {
    return rule_;
}

void RegistrationService::apply_catalog_change(
    const std::vector<Course>& courses,
    const std::vector<TeachingClass>& teaching_classes) {
    courses_ = courses;
    teaching_classes_ = teaching_classes;
}

void RegistrationService::apply_rule_change(const RegistrationRule& rule) {
    rule_ = rule;
}

}  // namespace course_registration
