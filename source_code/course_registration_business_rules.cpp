#include "course_registration_business_rules.hpp"

#include <ctime>

namespace course_registration {
namespace {

// 读取本机当前时间，转成与 opens_at、closes_at 相同的 "YYYY-MM-DDTHH:MM" 格式。
// 这种格式的年月日和时分位数固定，按字符串比较就能判断时间先后。
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
    // 如果请求同时违反多条规则，返回最先检查到的错误。
    // 下面所有检查都通过后，才会修改选课记录。
    // 1. 按当前时间判断选课是否开放，避免时间已过但旧开关仍显示开放。
    if (!registration_open_now()) {
        return {ResultCode::RegistrationClosed, "Registration period closed"};
    }

    // 2. 先查找请求中的教学班编号，找不到就返回错误。
    // 使用本类前，需要保证每个教学班对应的课程都存在。
    const TeachingClass* requested_class = find_teaching_class(teaching_class_id);
    if (requested_class == nullptr) {
        return {ResultCode::TeachingClassNotFound, "Teaching class not found"};
    }
    const Course* requested_course = find_course(requested_class->course_id);

    // 收集学生当前仍在修读的教学班，后面用它们检查重复课程、时间冲突和总学分。
    // 状态为 Dropped 的记录表示已经退课，不占用学分和上课时间。
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

    // 5. 用 long long 计算总学分，因为多个 int 学分相加可能超过 int 能表示的范围。
    // 如果直接用 int 相加，结果可能变成错误的数值，导致本应超学分的请求被允许。
    long long current_credits = 0;
    for (const TeachingClass* current_class : current_classes) {
        const Course* course = find_course(current_class->course_id);
        current_credits += static_cast<long long>(course->credits);
    }
    if (current_credits + static_cast<long long>(requested_course->credits) >
        static_cast<long long>(rule_.maximum_credits)) {
        return {ResultCode::CreditLimitExceeded, "Credit limit exceeded"};
    }

    // 6. 已选人数达到容量时，拒绝选课。人数只统计状态为 Active 的记录。
    if (enrollment_count(teaching_class_id) >= requested_class->capacity) {
        return {ResultCode::TeachingClassFull, "Teaching class full"};
    }

    // 7. 如果这个学生以前退过这个班，就把原来的 Dropped 记录改回 Active。
    // 这样重新选课时就不用新增一条记录。名额仍要按上面的步骤检查，因为可能已被别人选走。
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
    // 选课和退课都调用 registration_open_now，保证两种操作使用相同的开放时间规则。
    if (!registration_open_now()) {
        return {ResultCode::RegistrationClosed, "Registration period closed"};
    }

    for (Enrollment& enrollment : enrollments_) {
        if (enrollment.student_id == student_id &&
            enrollment.teaching_class_id == teaching_class_id &&
            enrollment.status == EnrollmentStatus::Active) {
            enrollment.status = EnrollmentStatus::Dropped;
            // 人数和课表只统计 Active 记录。改为 Dropped 后会空出一个名额，课表也会移除该班。
            // 记录本身保留下来，便于以后查看退课历史或重新选课。
            return {ResultCode::Success, "Drop successful"};
        }
    }
    return {ResultCode::ActiveEnrollmentNotFound,
            "Active enrollment not found"};
}

// ============================================================================
// [Zhu Ziliang 追加，待 Zhang Yuhua 复核] 下面的方法供教师和管理员模块使用。
// 查询方法返回当前保存的数据。修改方法由管理员检查通过后调用，用于替换课程、教学班或规则。
// 这些修改方法保留已有的选退课记录，选课时仍然按上面的检查顺序判断是否允许操作。
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

// [Zhang Yikai 追加，待 Zhang Yuhua 复核] 读取文件时，把保存的选课记录放回内存。
// 这里直接替换记录，由存储代码检查整份数据。避免因当前已过选课时间而无法恢复历史记录。
void RegistrationService::restore_enrollments(
    std::vector<Enrollment> enrollments) {
    enrollments_ = std::move(enrollments);
}

}  // namespace course_registration
