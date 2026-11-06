// 教师与管理员模块实现（Zhu Ziliang）
//
// 规则：任何人调用本模块都会先校验角色；校验失败的请求不改动任何数据。
// 名单、人数与余量一律由 RegistrationService 中的有效选课记录现算。

#include "teacher_and_administration_services.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <ctime>
#include <string>
#include <vector>

namespace course_registration {
namespace {

const char* const kIsoLengthError =
    "time must use the YYYY-MM-DDTHH:MM form";

bool is_digit(char character) { return character >= '0' && character <= '9'; }

bool all_digits(const std::string& text, std::size_t begin, std::size_t end) {
    for (std::size_t index = begin; index < end; ++index) {
        if (!is_digit(text[index])) {
            return false;
        }
    }
    return true;
}

int number_at(const std::string& text, std::size_t begin, std::size_t end) {
    int value = 0;
    for (std::size_t index = begin; index < end; ++index) {
        value = value * 10 + (text[index] - '0');
    }
    return value;
}

// 公历闰年：能被 4 整除且不能被 100 整除，或能被 400 整除。
bool is_leap_year(int year) {
    return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

int days_in_month(int year, int month) {
    static const int kDays[] = {31, 28, 31, 30, 31, 30,
                                31, 31, 30, 31, 30, 31};
    if (month < 1 || month > 12) {
        return 0;
    }
    if (month == 2 && is_leap_year(year)) {
        return 29;
    }
    return kDays[month - 1];
}

// "YYYY-MM-DDTHH:MM"：固定宽度，因此可以直接用字符串比较先后顺序。
bool is_valid_iso_minute(const std::string& text) {
    if (text.size() != 16) {
        return false;
    }
    if (text[4] != '-' || text[7] != '-' || text[10] != 'T' || text[13] != ':') {
        return false;
    }
    if (!all_digits(text, 0, 4) || !all_digits(text, 5, 7) ||
        !all_digits(text, 8, 10) || !all_digits(text, 11, 13) ||
        !all_digits(text, 14, 16)) {
        return false;
    }
    const int year = number_at(text, 0, 4);
    const int month = number_at(text, 5, 7);
    const int day = number_at(text, 8, 10);
    const int hour = number_at(text, 11, 13);
    const int minute = number_at(text, 14, 16);
    if (month < 1 || month > 12 || hour < 0 || hour > 23 ||
        minute < 0 || minute > 59) {
        return false;
    }
    // 缺陷-009 修复：按月份天数与闰年判断，
    // 避免 2026-02-30、2026-02-29（非闰年）或 2026-04-31 这类日期通过。
    return day >= 1 && day <= days_in_month(year, month);
}

bool is_blank(const std::string& text) {
    for (char character : text) {
        if (!std::isspace(static_cast<unsigned char>(character))) {
            return false;
        }
    }
    return true;
}

std::string to_lower(const std::string& text) {
    std::string result;
    result.reserve(text.size());
    for (char character : text) {
        result.push_back(static_cast<char>(
            std::tolower(static_cast<unsigned char>(character))));
    }
    return result;
}

std::string describe(const ClassMeeting& meeting) {
    return "day " + std::to_string(meeting.day_of_week) + " " +
           std::to_string(meeting.start_minute) + "-" +
           std::to_string(meeting.end_minute);
}

bool is_valid_meeting(const ClassMeeting& meeting) {
    return meeting.day_of_week >= 1 && meeting.day_of_week <= 7 &&
           meeting.start_minute >= 0 && meeting.end_minute <= 1440 &&
           meeting.start_minute < meeting.end_minute;
}

bool meetings_overlap(const ClassMeeting& left, const ClassMeeting& right) {
    return left.day_of_week == right.day_of_week &&
           left.start_minute < right.end_minute &&
           right.start_minute < left.end_minute;
}

bool teaching_classes_overlap(const TeachingClass& left,
                              const TeachingClass& right) {
    for (const ClassMeeting& left_meeting : left.meetings) {
        for (const ClassMeeting& right_meeting : right.meetings) {
            if (meetings_overlap(left_meeting, right_meeting)) {
                return true;
            }
        }
    }
    return false;
}

int count_active(const std::vector<Enrollment>& enrollments,
                 int teaching_class_id) {
    int count = 0;
    for (const Enrollment& enrollment : enrollments) {
        if (enrollment.teaching_class_id == teaching_class_id &&
            enrollment.status == EnrollmentStatus::Active) {
            ++count;
        }
    }
    return count;
}

// 覆盖写入：编号相同则替换，否则追加。
template <typename Record, typename Key>
void upsert(std::vector<Record>& records, const Record& record,
            Key key_of) {
    for (Record& existing : records) {
        if (key_of(existing) == key_of(record)) {
            existing = record;
            return;
        }
    }
    records.push_back(record);
}

template <typename Record, typename Key, typename KeyOf>
bool erase_by_key(std::vector<Record>& records, Key key, KeyOf key_of) {
    for (auto iterator = records.begin(); iterator != records.end(); ++iterator) {
        if (key_of(*iterator) == key) {
            records.erase(iterator);
            return true;
        }
    }
    return false;
}

OperationResult denied(const char* action) {
    return {ResultCode::PermissionDenied,
            std::string("Permission denied: ") + action +
                " requires a different role"};
}

}  // namespace

// ===========================================================================
// PermissionRules
// ===========================================================================

bool PermissionRules::is_administrator(const UserIdentity& user) {
    return user.role == Role::Administrator;
}

bool PermissionRules::is_teacher(const UserIdentity& user) {
    return user.role == Role::Teacher;
}

bool PermissionRules::is_student(const UserIdentity& user) {
    return user.role == Role::Student;
}

bool PermissionRules::can_manage_catalog(const UserIdentity& user) {
    return is_administrator(user);
}

bool PermissionRules::can_set_registration_period(const UserIdentity& user) {
    return is_administrator(user);
}

bool PermissionRules::can_manage_people(const UserIdentity& user) {
    return is_administrator(user);
}

bool PermissionRules::can_view_teaching_class(
    const UserIdentity& user, const TeachingClass& teaching_class) {
    if (is_administrator(user)) {
        return true;
    }
    if (is_teacher(user)) {
        return teaching_class.teacher_id == user.user_id;
    }
    return false;  // 学生不通过教师入口读取教学班
}

// ===========================================================================
// SystemContext
// ===========================================================================

SystemContext::SystemContext(std::vector<Student> students,
                             std::vector<Teacher> teachers,
                             std::vector<Administrator> administrators,
                             std::vector<Course> courses,
                             std::vector<TeachingClass> teaching_classes,
                             RegistrationRule rule)
    : students_(students),
      teachers_(teachers),
      administrators_(administrators),
      registration_(courses, teaching_classes, rule) {}

RegistrationService& SystemContext::registration() { return registration_; }

const RegistrationService& SystemContext::registration() const {
    return registration_;
}

std::vector<Course> SystemContext::courses() const {
    return registration_.course_snapshot();
}

std::vector<TeachingClass> SystemContext::teaching_classes() const {
    return registration_.teaching_class_snapshot();
}

RegistrationRule SystemContext::rule() const {
    return registration_.registration_rule();
}

const std::vector<Student>& SystemContext::students() const {
    return students_;
}

const std::vector<Teacher>& SystemContext::teachers() const {
    return teachers_;
}

const std::vector<Administrator>& SystemContext::administrators() const {
    return administrators_;
}

const Student* SystemContext::find_student(int student_id) const {
    for (const Student& student : students_) {
        if (student.student_id == student_id) {
            return &student;
        }
    }
    return nullptr;
}

const Teacher* SystemContext::find_teacher(int teacher_id) const {
    for (const Teacher& teacher : teachers_) {
        if (teacher.teacher_id == teacher_id) {
            return &teacher;
        }
    }
    return nullptr;
}

const Student* SystemContext::find_student_by_number(
    const std::string& number) const {
    for (const Student& student : students_) {
        if (to_lower(student.student_number) == to_lower(number)) {
            return &student;
        }
    }
    return nullptr;
}

const Teacher* SystemContext::find_teacher_by_number(
    const std::string& number) const {
    for (const Teacher& teacher : teachers_) {
        if (to_lower(teacher.teacher_number) == to_lower(number)) {
            return &teacher;
        }
    }
    return nullptr;
}

bool SystemContext::find_course(int course_id, Course& course) const {
    for (const Course& candidate : registration_.course_snapshot()) {
        if (candidate.course_id == course_id) {
            course = candidate;
            return true;
        }
    }
    return false;
}

bool SystemContext::find_teaching_class(
    int teaching_class_id, TeachingClass& teaching_class) const {
    for (const TeachingClass& candidate :
         registration_.teaching_class_snapshot()) {
        if (candidate.teaching_class_id == teaching_class_id) {
            teaching_class = candidate;
            return true;
        }
    }
    return false;
}

std::vector<Enrollment> SystemContext::all_enrollments() const {
    return registration_.all_enrollments();
}

std::vector<Enrollment> SystemContext::active_enrollments(
    int student_id) const {
    return registration_.active_enrollments(student_id);
}

int SystemContext::enrollment_count(int teaching_class_id) const {
    return registration_.enrollment_count(teaching_class_id);
}

int SystemContext::remaining_seats(int teaching_class_id) const {
    return registration_.remaining_seats(teaching_class_id);
}

void SystemContext::apply_course_change(const Course& course) {
    std::vector<Course> courses = registration_.course_snapshot();
    upsert(courses, course, [](const Course& record) { return record.course_id; });
    registration_.apply_catalog_change(courses,
                                       registration_.teaching_class_snapshot());
}

void SystemContext::remove_course_record(int course_id) {
    std::vector<Course> courses = registration_.course_snapshot();
    erase_by_key(courses, course_id,
                 [](const Course& record) { return record.course_id; });
    registration_.apply_catalog_change(courses,
                                       registration_.teaching_class_snapshot());
}

void SystemContext::apply_teaching_class_change(
    const TeachingClass& teaching_class) {
    std::vector<TeachingClass> classes =
        registration_.teaching_class_snapshot();
    upsert(classes, teaching_class, [](const TeachingClass& record) {
        return record.teaching_class_id;
    });
    registration_.apply_catalog_change(registration_.course_snapshot(), classes);
}

void SystemContext::remove_teaching_class_record(int teaching_class_id) {
    std::vector<TeachingClass> classes =
        registration_.teaching_class_snapshot();
    erase_by_key(classes, teaching_class_id, [](const TeachingClass& record) {
        return record.teaching_class_id;
    });
    registration_.apply_catalog_change(registration_.course_snapshot(), classes);
}

void SystemContext::apply_rule_change(const RegistrationRule& rule) {
    registration_.apply_rule_change(rule);
}

void SystemContext::apply_student_change(const Student& student) {
    upsert(students_, student,
           [](const Student& record) { return record.student_id; });
}

void SystemContext::remove_student_record(int student_id) {
    erase_by_key(students_, student_id,
                 [](const Student& record) { return record.student_id; });
}

void SystemContext::apply_teacher_change(const Teacher& teacher) {
    upsert(teachers_, teacher,
           [](const Teacher& record) { return record.teacher_id; });
}

void SystemContext::remove_teacher_record(int teacher_id) {
    erase_by_key(teachers_, teacher_id,
                 [](const Teacher& record) { return record.teacher_id; });
}

void SystemContext::apply_administrator_change(
    const Administrator& administrator) {
    upsert(administrators_, administrator,
           [](const Administrator& record) { return record.administrator_id; });
}

std::string SystemContext::now() const {
    if (!fixed_now_.empty()) {
        return fixed_now_;
    }
    std::time_t current = std::time(nullptr);
    std::tm* local_time = std::localtime(&current);
    if (local_time == nullptr) {
        return "1970-01-01T00:00";
    }
    char buffer[32] = {0};
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M", local_time);
    return std::string(buffer);
}

void SystemContext::set_now_for_tests(const std::string& iso_minute) {
    fixed_now_ = iso_minute;
    // 选课服务自己也要用同一个"现在"，否则时段判断会与管理员模块不一致。
    registration_.set_fixed_now_for_tests(iso_minute);
}

bool SystemContext::registration_window_open_at(
    const std::string& moment) const {
    // 只保留一处实现：交给选课服务判断，enroll()/drop() 用的是同一段逻辑。
    return registration_.registration_open_at(moment);
}

bool SystemContext::registration_window_open() const {
    return registration_window_open_at(now());
}

void SystemContext::refresh_registration_window() {
    // 仅把现算结果同步到 registration_open，供界面/演示显示当前状态。
    // enroll()/drop() 自己会现算，不再依赖这个布尔值，所以即使不调用本函数
    // 也不会出现"时段已结束仍能选课"的问题。
    RegistrationRule current = registration_.registration_rule();
    if (current.opens_at.empty() || current.closes_at.empty()) {
        return;  // 尚未设置时段，保留管理员的手动开关
    }
    current.registration_open = registration_window_open_at(now());
    registration_.apply_rule_change(current);
}

bool SystemContext::observe_registration_round() {
    if (round_opened_) {
        return true;
    }
    const RegistrationRule rule = registration_.registration_rule();
    // 1. 现在就开放：时段覆盖当前时间，或未设时段但手动开关为 true。
    if (registration_.registration_open_now()) {
        round_opened_ = true;
    }
    // 2. 时段的开始时刻已经过去：即使现在已关闭、已结束，也算"本轮开放过"。
    if (!rule.opens_at.empty() && !rule.closes_at.empty() &&
        now() >= rule.opens_at) {
        round_opened_ = true;
    }
    return round_opened_;
}

// ===========================================================================
// TeacherService
// ===========================================================================

namespace {

OperationResult build_summary(const SystemContext& context,
                              const TeachingClass& teaching_class,
                              TeachingClassSummary& summary) {
    Course course{};
    if (!context.find_course(teaching_class.course_id, course)) {
        return {ResultCode::RecordNotFound,
                "Teaching class " +
                    std::to_string(teaching_class.teaching_class_id) +
                    " refers to unknown course " +
                    std::to_string(teaching_class.course_id)};
    }
    const Teacher* teacher = context.find_teacher(teaching_class.teacher_id);
    summary = TeachingClassSummary{};
    summary.teaching_class_id = teaching_class.teaching_class_id;
    summary.course_id = course.course_id;
    summary.course_code = course.course_code;
    summary.course_name = course.course_name;
    summary.credits = course.credits;
    summary.teacher_id = teaching_class.teacher_id;
    summary.teacher_name = teacher == nullptr ? "(unregistered teacher)"
                                             : teacher->name;
    summary.capacity = teaching_class.capacity;
    summary.active_enrollments =
        context.enrollment_count(teaching_class.teaching_class_id);
    summary.remaining_seats = teaching_class.capacity - summary.active_enrollments;
    summary.meetings = teaching_class.meetings;
    return {ResultCode::Success, "ok"};
}

}  // namespace

TeacherService::TeacherService(const SystemContext& context)
    : context_(context) {}

OperationResult TeacherService::assigned_teaching_classes(
    const UserIdentity& user,
    std::vector<TeachingClassSummary>& result) const {
    result.clear();
    if (!PermissionRules::is_teacher(user) &&
        !PermissionRules::is_administrator(user)) {
        return denied("viewing assigned teaching classes");
    }

    const std::vector<TeachingClass> classes = context_.teaching_classes();
    for (const TeachingClass& teaching_class : classes) {
        if (!PermissionRules::can_view_teaching_class(user, teaching_class)) {
            continue;  // 教师跳过他人教学班，不暴露其存在
        }
        TeachingClassSummary summary{};
        const OperationResult built = build_summary(context_, teaching_class, summary);
        if (built.code != ResultCode::Success) {
            return built;
        }
        result.push_back(summary);
    }
    std::sort(result.begin(), result.end(),
              [](const TeachingClassSummary& left,
                 const TeachingClassSummary& right) {
                  return left.teaching_class_id < right.teaching_class_id;
              });
    return {ResultCode::Success,
            std::to_string(result.size()) + " teaching class(es) returned"};
}

OperationResult TeacherService::class_summary(
    const UserIdentity& user,
    int teaching_class_id,
    TeachingClassSummary& result) const {
    // 缺陷-007 修复：入口先清空输出，失败时不会残留上一次查询的班级。
    // 调用方仍然必须检查返回码。
    result = TeachingClassSummary{};
    TeachingClass teaching_class{};
    if (!context_.find_teaching_class(teaching_class_id, teaching_class)) {
        return {ResultCode::TeachingClassNotFound,
                "Teaching class " + std::to_string(teaching_class_id) +
                    " not found"};
    }
    if (!PermissionRules::can_view_teaching_class(user, teaching_class)) {
        return denied("reading another teacher's teaching class");
    }
    return build_summary(context_, teaching_class, result);
}

OperationResult TeacherService::class_roster(
    const UserIdentity& user,
    int teaching_class_id,
    ClassRoster& result) const {
    result = ClassRoster{};
    TeachingClass teaching_class{};
    if (!context_.find_teaching_class(teaching_class_id, teaching_class)) {
        return {ResultCode::TeachingClassNotFound,
                "Teaching class " + std::to_string(teaching_class_id) +
                    " not found"};
    }
    if (!PermissionRules::can_view_teaching_class(user, teaching_class)) {
        return denied("reading another teacher's student roster");
    }

    result.teaching_class_id = teaching_class_id;
    result.capacity = teaching_class.capacity;
    result.active_enrollments = context_.enrollment_count(teaching_class_id);
    result.remaining_seats = teaching_class.capacity - result.active_enrollments;

    for (const Enrollment& enrollment : context_.all_enrollments()) {
        if (enrollment.teaching_class_id != teaching_class_id ||
            enrollment.status != EnrollmentStatus::Active) {
            continue;  // 已退课记录不进名单
        }
        const Student* student = context_.find_student(enrollment.student_id);
        if (student == nullptr) {
            // 未登记学生的选课记录不进入名单，由 check_data_integrity 报告。
            continue;
        }
        result.students.push_back(
            {student->student_id, student->student_number, student->name});
    }
    std::sort(result.students.begin(), result.students.end(),
              [](const RosterEntry& left, const RosterEntry& right) {
                  return left.student_number < right.student_number;
              });

    return {ResultCode::Success,
            std::to_string(result.active_enrollments) +
                " active student(s) in class " + std::to_string(teaching_class_id)};
}

// ===========================================================================
// AdministratorService
// ===========================================================================

AdministratorService::AdministratorService(SystemContext& context)
    : context_(context) {}

OperationResult AdministratorService::create_course(const UserIdentity& user,
                                                    const Course& course) {
    if (!PermissionRules::can_manage_catalog(user)) {
        return denied("creating a course");
    }
    if (course.course_id <= 0) {
        return {ResultCode::InvalidInput, "course_id must be positive"};
    }
    if (is_blank(course.course_code)) {
        return {ResultCode::InvalidInput, "course_code must not be empty"};
    }
    if (is_blank(course.course_name)) {
        return {ResultCode::InvalidInput, "course_name must not be empty"};
    }
    if (course.credits <= 0) {
        return {ResultCode::InvalidInput, "credits must be positive"};
    }

    for (const Course& existing : context_.courses()) {
        if (existing.course_id == course.course_id) {
            return {ResultCode::DuplicateIdentifier,
                    "course_id " + std::to_string(course.course_id) +
                        " already exists"};
        }
        if (to_lower(existing.course_code) == to_lower(course.course_code)) {
            return {ResultCode::DuplicateIdentifier,
                    "course_code " + course.course_code + " already exists"};
        }
    }

    context_.apply_course_change(course);
    return {ResultCode::Success,
            "course " + course.course_code + " created"};
}

OperationResult AdministratorService::update_course(const UserIdentity& user,
                                                    const Course& course) {
    if (!PermissionRules::can_manage_catalog(user)) {
        return denied("editing a course");
    }
    Course existing{};
    if (!context_.find_course(course.course_id, existing)) {
        return {ResultCode::RecordNotFound,
                "course_id " + std::to_string(course.course_id) +
                    " not found"};
    }
    if (is_blank(course.course_code) || is_blank(course.course_name)) {
        return {ResultCode::InvalidInput,
                "course_code and course_name must not be empty"};
    }
    if (course.credits <= 0) {
        return {ResultCode::InvalidInput, "credits must be positive"};
    }
    for (const Course& other : context_.courses()) {
        if (other.course_id != course.course_id &&
            to_lower(other.course_code) == to_lower(course.course_code)) {
            return {ResultCode::DuplicateIdentifier,
                    "course_code " + course.course_code +
                        " is used by course_id " +
                        std::to_string(other.course_id)};
        }
    }

    // 改学分不能让任何在读学生超过学分上限，否则拒绝并指出学生。
    if (course.credits != existing.credits) {
        const RegistrationRule rule = context_.rule();
        for (const Student& student : context_.students()) {
            int total = 0;
            for (const Enrollment& enrollment :
                 context_.active_enrollments(student.student_id)) {
                TeachingClass enrolled_class{};
                if (!context_.find_teaching_class(enrollment.teaching_class_id,
                                                  enrolled_class)) {
                    continue;
                }
                Course enrolled_course{};
                if (!context_.find_course(enrolled_class.course_id,
                                          enrolled_course)) {
                    continue;
                }
                total += enrolled_course.course_id == course.course_id
                             ? course.credits
                             : enrolled_course.credits;
            }
            if (total > rule.maximum_credits) {
                return {ResultCode::CreditLimitExceeded,
                        "changing credits to " + std::to_string(course.credits) +
                            " makes student " + student.student_number +
                            " reach " + std::to_string(total) +
                            " credits, above the limit " +
                            std::to_string(rule.maximum_credits)};
            }
        }
    }

    context_.apply_course_change(course);
    return {ResultCode::Success,
            "course " + course.course_code + " updated"};
}

OperationResult AdministratorService::remove_course(const UserIdentity& user,
                                                    int course_id) {
    if (!PermissionRules::can_manage_catalog(user)) {
        return denied("removing a course");
    }
    Course existing{};
    if (!context_.find_course(course_id, existing)) {
        return {ResultCode::RecordNotFound,
                "course_id " + std::to_string(course_id) + " not found"};
    }
    int referencing_classes = 0;
    for (const TeachingClass& teaching_class : context_.teaching_classes()) {
        if (teaching_class.course_id == course_id) {
            ++referencing_classes;
        }
    }
    if (referencing_classes > 0) {
        return {ResultCode::CatalogInUse,
                "course " + existing.course_code + " still has " +
                    std::to_string(referencing_classes) +
                    " teaching class(es); remove them first"};
    }
    context_.remove_course_record(course_id);
    return {ResultCode::Success, "course " + existing.course_code + " removed"};
}

OperationResult AdministratorService::create_teaching_class(
    const UserIdentity& user, const TeachingClass& teaching_class) {
    if (!PermissionRules::can_manage_catalog(user)) {
        return denied("creating a teaching class");
    }
    if (teaching_class.teaching_class_id <= 0) {
        return {ResultCode::InvalidInput, "teaching_class_id must be positive"};
    }
    if (teaching_class.capacity <= 0) {
        return {ResultCode::InvalidInput, "capacity must be at least 1"};
    }
    if (teaching_class.meetings.empty()) {
        return {ResultCode::InvalidInput,
                "a teaching class needs at least one class meeting"};
    }
    for (const ClassMeeting& meeting : teaching_class.meetings) {
        if (!is_valid_meeting(meeting)) {
            return {ResultCode::InvalidInput,
                    "invalid class meeting: " + describe(meeting)};
        }
    }
    for (const TeachingClass& existing : context_.teaching_classes()) {
        if (existing.teaching_class_id == teaching_class.teaching_class_id) {
            return {ResultCode::DuplicateIdentifier,
                    "teaching_class_id " +
                        std::to_string(teaching_class.teaching_class_id) +
                        " already exists"};
        }
    }
    Course course{};
    if (!context_.find_course(teaching_class.course_id, course)) {
        return {ResultCode::RecordNotFound,
                "course_id " + std::to_string(teaching_class.course_id) +
                    " not found"};
    }
    const Teacher* teacher = context_.find_teacher(teaching_class.teacher_id);
    if (teacher == nullptr) {
        return {ResultCode::RecordNotFound,
                "teacher_id " + std::to_string(teaching_class.teacher_id) +
                    " not found"};
    }
    if (!teacher->account_active) {
        return {ResultCode::InvalidInput,
                "teacher " + teacher->teacher_number +
                    " is not an active account"};
    }
    // 同一教师不能在同一时段承担两个教学班。
    for (const TeachingClass& existing : context_.teaching_classes()) {
        if (existing.teacher_id == teaching_class.teacher_id &&
            teaching_classes_overlap(existing, teaching_class)) {
            return {ResultCode::TimeConflict,
                    "teacher " + teacher->teacher_number +
                        " already teaches teaching class " +
                        std::to_string(existing.teaching_class_id) +
                        " in the same time slot"};
        }
    }

    context_.apply_teaching_class_change(teaching_class);
    return {ResultCode::Success,
            "teaching class " +
                std::to_string(teaching_class.teaching_class_id) + " of " +
                course.course_code + " created"};
}

OperationResult AdministratorService::update_teaching_class(
    const UserIdentity& user, const TeachingClass& teaching_class) {
    if (!PermissionRules::can_manage_catalog(user)) {
        return denied("editing a teaching class");
    }
    TeachingClass existing{};
    if (!context_.find_teaching_class(teaching_class.teaching_class_id,
                                      existing)) {
        return {ResultCode::TeachingClassNotFound,
                "teaching class " +
                    std::to_string(teaching_class.teaching_class_id) +
                    " not found"};
    }
    if (teaching_class.capacity <= 0) {
        return {ResultCode::InvalidInput, "capacity must be at least 1"};
    }
    if (teaching_class.meetings.empty()) {
        return {ResultCode::InvalidInput,
                "a teaching class needs at least one class meeting"};
    }
    for (const ClassMeeting& meeting : teaching_class.meetings) {
        if (!is_valid_meeting(meeting)) {
            return {ResultCode::InvalidInput,
                    "invalid class meeting: " + describe(meeting)};
        }
    }
    Course course{};
    if (!context_.find_course(teaching_class.course_id, course)) {
        return {ResultCode::RecordNotFound,
                "course_id " + std::to_string(teaching_class.course_id) +
                    " not found"};
    }
    const Teacher* teacher = context_.find_teacher(teaching_class.teacher_id);
    if (teacher == nullptr) {
        return {ResultCode::RecordNotFound,
                "teacher_id " + std::to_string(teaching_class.teacher_id) +
                    " not found"};
    }

    const int active = context_.enrollment_count(
        teaching_class.teaching_class_id);

    // 缺陷-002 修复：教学班已有有效选课时，不允许改 course_id。
    // 否则已选学生可能变成同时选同一门课的两个教学班，或改成高学分课程后
    // 绕过学分上限；enroll() 的检查只在选课时做一次，管不到这种事后改动。
    // 容量与上课时间仍按各自的规则继续校验（下面分别处理）。
    if (teaching_class.course_id != existing.course_id && active > 0) {
        return {ResultCode::InvalidInput,
                "course_id of teaching class " +
                    std::to_string(teaching_class.teaching_class_id) +
                    " cannot be changed from " +
                    std::to_string(existing.course_id) + " to " +
                    std::to_string(teaching_class.course_id) + " while it has " +
                    std::to_string(active) +
                    " active enrollment(s); ask the students to drop first"};
    }

    if (teaching_class.capacity < active) {
        return {ResultCode::CapacityBelowEnrollment,
                "capacity " + std::to_string(teaching_class.capacity) +
                    " is below the " + std::to_string(active) +
                    " active enrollment(s) of teaching class " +
                    std::to_string(teaching_class.teaching_class_id)};
    }

    // 教师本人不能被安排到与其他教学班重叠的时段。
    for (const TeachingClass& other : context_.teaching_classes()) {
        if (other.teaching_class_id == teaching_class.teaching_class_id) {
            continue;
        }
        if (other.teacher_id == teaching_class.teacher_id &&
            teaching_classes_overlap(other, teaching_class)) {
            return {ResultCode::TimeConflict,
                    "teacher " + teacher->teacher_number +
                        " already teaches teaching class " +
                        std::to_string(other.teaching_class_id) +
                        " in the same time slot"};
        }
    }

    // 改时段后，已选课学生不能与其他已选教学班冲突。
    for (const Enrollment& enrollment : context_.all_enrollments()) {
        if (enrollment.teaching_class_id != teaching_class.teaching_class_id ||
            enrollment.status != EnrollmentStatus::Active) {
            continue;
        }
        for (const Enrollment& other :
             context_.active_enrollments(enrollment.student_id)) {
            if (other.teaching_class_id == teaching_class.teaching_class_id) {
                continue;
            }
            TeachingClass other_class{};
            if (!context_.find_teaching_class(other.teaching_class_id,
                                              other_class)) {
                continue;
            }
            if (teaching_classes_overlap(other_class, teaching_class)) {
                const Student* student =
                    context_.find_student(enrollment.student_id);
                return {ResultCode::TimeConflict,
                        "the new time slot conflicts with teaching class " +
                            std::to_string(other_class.teaching_class_id) +
                            " already taken by student " +
                            (student == nullptr ? std::to_string(
                                                      enrollment.student_id)
                                                : student->student_number)};
            }
        }
    }

    context_.apply_teaching_class_change(teaching_class);
    return {ResultCode::Success,
            "teaching class " +
                std::to_string(teaching_class.teaching_class_id) + " updated"};
}

OperationResult AdministratorService::remove_teaching_class(
    const UserIdentity& user, int teaching_class_id) {
    if (!PermissionRules::can_manage_catalog(user)) {
        return denied("removing a teaching class");
    }
    TeachingClass existing{};
    if (!context_.find_teaching_class(teaching_class_id, existing)) {
        return {ResultCode::TeachingClassNotFound,
                "teaching class " + std::to_string(teaching_class_id) +
                    " not found"};
    }
    int active = 0;
    int dropped = 0;
    for (const Enrollment& enrollment : context_.all_enrollments()) {
        if (enrollment.teaching_class_id != teaching_class_id) {
            continue;
        }
        if (enrollment.status == EnrollmentStatus::Active) {
            ++active;
        } else {
            ++dropped;
        }
    }
    if (active > 0) {
        return {ResultCode::CatalogInUse,
                "teaching class " + std::to_string(teaching_class_id) +
                    " still has " + std::to_string(active) +
                    " active enrollment(s); ask students to drop first"};
    }
    if (dropped > 0) {
        // 删除教学班会让已退课记录失去引用，当前版本不做归档，因此拒绝。
        return {ResultCode::CatalogInUse,
                "teaching class " + std::to_string(teaching_class_id) +
                    " has " + std::to_string(dropped) +
                    " dropped enrollment record(s) kept as history; "
                    "archiving is out of the current scope"};
    }

    context_.remove_teaching_class_record(teaching_class_id);
    return {ResultCode::Success,
            "teaching class " + std::to_string(teaching_class_id) +
                " removed"};
}

OperationResult AdministratorService::set_teaching_class_capacity(
    const UserIdentity& user, int teaching_class_id, int capacity) {
    if (!PermissionRules::can_manage_catalog(user)) {
        return denied("changing a teaching class capacity");
    }
    TeachingClass existing{};
    if (!context_.find_teaching_class(teaching_class_id, existing)) {
        return {ResultCode::TeachingClassNotFound,
                "teaching class " + std::to_string(teaching_class_id) +
                    " not found"};
    }
    if (capacity <= 0) {
        return {ResultCode::InvalidInput, "capacity must be at least 1"};
    }
    const int active = context_.enrollment_count(teaching_class_id);
    if (capacity < active) {
        return {ResultCode::CapacityBelowEnrollment,
                "capacity " + std::to_string(capacity) +
                    " is below the " + std::to_string(active) +
                    " active enrollment(s); no seat can be taken away from "
                    "an existing student"};
    }

    const int previous = existing.capacity;
    existing.capacity = capacity;
    context_.apply_teaching_class_change(existing);
    return {ResultCode::Success,
            "capacity of teaching class " + std::to_string(teaching_class_id) +
                " changed from " + std::to_string(previous) + " to " +
                std::to_string(capacity) + "; remaining seats " +
                std::to_string(context_.remaining_seats(teaching_class_id))};
}

OperationResult AdministratorService::set_registration_period(
    const UserIdentity& user,
    const std::string& opens_at,
    const std::string& closes_at) {
    if (!PermissionRules::can_set_registration_period(user)) {
        return denied("setting the registration period");
    }
    if (!is_valid_iso_minute(opens_at) || !is_valid_iso_minute(closes_at)) {
        return {ResultCode::InvalidInput, kIsoLengthError};
    }
    if (!(opens_at < closes_at)) {
        return {ResultCode::InvalidInput,
                "the registration period must start before it ends (" +
                    opens_at + " -> " + closes_at + ")"};
    }

    RegistrationRule rule = context_.rule();
    rule.opens_at = opens_at;
    rule.closes_at = closes_at;
    context_.apply_rule_change(rule);
    context_.refresh_registration_window();
    // 若这个时段此刻已经开放，本轮就算作已开放，学分上限随之锁住。
    context_.observe_registration_round();

    const RegistrationRule applied = context_.rule();
    return {ResultCode::Success,
            "registration period set to " + opens_at + " -> " + closes_at +
                "; registration is " +
                (applied.registration_open ? "open" : "closed") + " now at " +
                context_.now()};
}

OperationResult AdministratorService::set_maximum_credits(
    const UserIdentity& user, int maximum_credits) {
    if (!PermissionRules::can_set_registration_period(user)) {
        return denied("changing the maximum credits");
    }
    if (maximum_credits <= 0) {
        return {ResultCode::InvalidInput, "maximum_credits must be positive"};
    }
    // 缺陷-010 修复：学分上限必须在本轮选课开放前确定。
    // 只要本轮开放过（含暂停、已结束），就不再允许修改；
    // 仅有"当前是否开放"的判断会被"先关闭、改上限、再开放"绕过。
    if (context_.observe_registration_round()) {
        return {ResultCode::InvalidInput,
                "maximum credits must be fixed before this round of "
                "registration opens; the limit is still " +
                    std::to_string(context_.rule().maximum_credits) +
                    " and cannot be changed now"};
    }
    RegistrationRule rule = context_.rule();
    rule.maximum_credits = maximum_credits;
    context_.apply_rule_change(rule);
    return {ResultCode::Success,
            "maximum credits set to " + std::to_string(maximum_credits)};
}

OperationResult AdministratorService::close_registration(
    const UserIdentity& user) {
    if (!PermissionRules::can_set_registration_period(user)) {
        return denied("closing the registration period");
    }
    // 缺陷-010：关闭前先记下"本轮已经开放过"，否则清空时段后
    // 这个事实会丢失，"关闭→改上限→再开放"就能绕过限制。
    context_.observe_registration_round();
    // 清空起止时间：手动关闭作为覆盖状态，避免下一次按请求时刻现算时
    // 又被已经设好的时段重新打开。要恢复开放，管理员重新设置时段。
    RegistrationRule rule = context_.rule();
    rule.registration_open = false;
    rule.opens_at.clear();
    rule.closes_at.clear();
    context_.apply_rule_change(rule);
    return {ResultCode::Success,
            "registration closed by the administrator; set a new period to "
            "reopen"};
}

OperationResult AdministratorService::create_student(
    const UserIdentity& user, const Student& student) {
    if (!PermissionRules::can_manage_people(user)) {
        return denied("creating a student account");
    }
    if (student.student_id <= 0) {
        return {ResultCode::InvalidInput, "student_id must be positive"};
    }
    if (is_blank(student.student_number)) {
        return {ResultCode::InvalidInput, "student_number must not be empty"};
    }
    if (is_blank(student.name)) {
        return {ResultCode::InvalidInput, "name must not be empty"};
    }
    if (context_.find_student(student.student_id) != nullptr) {
        return {ResultCode::DuplicateIdentifier,
                "student_id " + std::to_string(student.student_id) +
                    " already exists"};
    }
    if (context_.find_student_by_number(student.student_number) != nullptr) {
        return {ResultCode::DuplicateIdentifier,
                "student_number " + student.student_number +
                    " already exists"};
    }
    context_.apply_student_change(student);
    return {ResultCode::Success,
            "student " + student.student_number + " created"};
}

OperationResult AdministratorService::update_student(
    const UserIdentity& user, const Student& student) {
    if (!PermissionRules::can_manage_people(user)) {
        return denied("editing a student account");
    }
    if (context_.find_student(student.student_id) == nullptr) {
        return {ResultCode::RecordNotFound,
                "student_id " + std::to_string(student.student_id) +
                    " not found"};
    }
    if (is_blank(student.student_number) || is_blank(student.name)) {
        return {ResultCode::InvalidInput,
                "student_number and name must not be empty"};
    }
    const Student* by_number = context_.find_student_by_number(
        student.student_number);
    if (by_number != nullptr && by_number->student_id != student.student_id) {
        return {ResultCode::DuplicateIdentifier,
                "student_number " + student.student_number +
                    " is used by student_id " +
                    std::to_string(by_number->student_id)};
    }
    context_.apply_student_change(student);
    return {ResultCode::Success,
            "student " + student.student_number + " updated"};
}

OperationResult AdministratorService::remove_student(const UserIdentity& user,
                                                     int student_id) {
    if (!PermissionRules::can_manage_people(user)) {
        return denied("removing a student account");
    }
    const Student* existing = context_.find_student(student_id);
    if (existing == nullptr) {
        return {ResultCode::RecordNotFound,
                "student_id " + std::to_string(student_id) + " not found"};
    }
    // 缺陷-006 修复：先复制学号。删除会 erase 掉 vector 元素，
    // 之后 existing 是悬空指针，读出来可能是另一个学生的编号。
    const std::string student_number = existing->student_number;
    const int active = static_cast<int>(
        context_.active_enrollments(student_id).size());
    if (active > 0) {
        return {ResultCode::CatalogInUse,
                "student " + student_number + " still has " +
                    std::to_string(active) +
                    " active enrollment(s); drop them first"};
    }
    context_.remove_student_record(student_id);
    return {ResultCode::Success, "student " + student_number + " removed"};
}

OperationResult AdministratorService::create_teacher(
    const UserIdentity& user, const Teacher& teacher) {
    if (!PermissionRules::can_manage_people(user)) {
        return denied("creating a teacher account");
    }
    if (teacher.teacher_id <= 0) {
        return {ResultCode::InvalidInput, "teacher_id must be positive"};
    }
    if (is_blank(teacher.teacher_number)) {
        return {ResultCode::InvalidInput, "teacher_number must not be empty"};
    }
    if (is_blank(teacher.name)) {
        return {ResultCode::InvalidInput, "name must not be empty"};
    }
    if (context_.find_teacher(teacher.teacher_id) != nullptr) {
        return {ResultCode::DuplicateIdentifier,
                "teacher_id " + std::to_string(teacher.teacher_id) +
                    " already exists"};
    }
    if (context_.find_teacher_by_number(teacher.teacher_number) != nullptr) {
        return {ResultCode::DuplicateIdentifier,
                "teacher_number " + teacher.teacher_number +
                    " already exists"};
    }
    context_.apply_teacher_change(teacher);
    return {ResultCode::Success,
            "teacher " + teacher.teacher_number + " created"};
}

OperationResult AdministratorService::update_teacher(
    const UserIdentity& user, const Teacher& teacher) {
    if (!PermissionRules::can_manage_people(user)) {
        return denied("editing a teacher account");
    }
    if (context_.find_teacher(teacher.teacher_id) == nullptr) {
        return {ResultCode::RecordNotFound,
                "teacher_id " + std::to_string(teacher.teacher_id) +
                    " not found"};
    }
    if (is_blank(teacher.teacher_number) || is_blank(teacher.name)) {
        return {ResultCode::InvalidInput,
                "teacher_number and name must not be empty"};
    }
    const Teacher* by_number = context_.find_teacher_by_number(
        teacher.teacher_number);
    if (by_number != nullptr && by_number->teacher_id != teacher.teacher_id) {
        return {ResultCode::DuplicateIdentifier,
                "teacher_number " + teacher.teacher_number +
                    " is used by teacher_id " +
                    std::to_string(by_number->teacher_id)};
    }
    context_.apply_teacher_change(teacher);
    return {ResultCode::Success,
            "teacher " + teacher.teacher_number + " updated"};
}

OperationResult AdministratorService::remove_teacher(const UserIdentity& user,
                                                     int teacher_id) {
    if (!PermissionRules::can_manage_people(user)) {
        return denied("removing a teacher account");
    }
    const Teacher* existing = context_.find_teacher(teacher_id);
    if (existing == nullptr) {
        return {ResultCode::RecordNotFound,
                "teacher_id " + std::to_string(teacher_id) + " not found"};
    }
    // 缺陷-006 修复：先复制工号，删除后不再通过 existing 指针读取。
    const std::string teacher_number = existing->teacher_number;
    int assigned = 0;
    for (const TeachingClass& teaching_class : context_.teaching_classes()) {
        if (teaching_class.teacher_id == teacher_id) {
            ++assigned;
        }
    }
    if (assigned > 0) {
        return {ResultCode::CatalogInUse,
                "teacher " + teacher_number + " still teaches " +
                    std::to_string(assigned) +
                    " teaching class(es); reassign them first"};
    }
    context_.remove_teacher_record(teacher_id);
    return {ResultCode::Success, "teacher " + teacher_number + " removed"};
}

OperationResult AdministratorService::check_data_integrity(
    const UserIdentity& user, std::vector<std::string>& problems) const {
    problems.clear();
    if (!PermissionRules::can_manage_catalog(user)) {
        return denied("running the data integrity check");
    }

    const std::vector<Course> courses = context_.courses();
    const std::vector<TeachingClass> classes = context_.teaching_classes();
    const std::vector<Enrollment> enrollments = context_.all_enrollments();

    for (const TeachingClass& teaching_class : classes) {
        bool course_found = false;
        for (const Course& course : courses) {
            if (course.course_id == teaching_class.course_id) {
                course_found = true;
                break;
            }
        }
        if (!course_found) {
            problems.push_back("teaching class " +
                               std::to_string(teaching_class.teaching_class_id) +
                               " refers to missing course_id " +
                               std::to_string(teaching_class.course_id));
        }
        if (context_.find_teacher(teaching_class.teacher_id) == nullptr) {
            problems.push_back("teaching class " +
                               std::to_string(teaching_class.teaching_class_id) +
                               " refers to missing teacher_id " +
                               std::to_string(teaching_class.teacher_id));
        }
        if (teaching_class.capacity <= 0) {
            problems.push_back("teaching class " +
                               std::to_string(teaching_class.teaching_class_id) +
                               " has non-positive capacity " +
                               std::to_string(teaching_class.capacity));
        }
        if (teaching_class.meetings.empty()) {
            problems.push_back("teaching class " +
                               std::to_string(teaching_class.teaching_class_id) +
                               " has no class meeting");
        }
        for (const ClassMeeting& meeting : teaching_class.meetings) {
            if (!is_valid_meeting(meeting)) {
                problems.push_back("teaching class " +
                                   std::to_string(
                                       teaching_class.teaching_class_id) +
                                   " has an invalid meeting " +
                                   describe(meeting));
            }
        }
        const int active = count_active(enrollments,
                                        teaching_class.teaching_class_id);
        if (active > teaching_class.capacity) {
            problems.push_back(
                "teaching class " +
                std::to_string(teaching_class.teaching_class_id) + " has " +
                std::to_string(active) + " active enrollments but capacity " +
                std::to_string(teaching_class.capacity));
        }
    }

    // 同一学生对同一教学班只应有一条有效记录。
    for (std::size_t left = 0; left < enrollments.size(); ++left) {
        if (enrollments[left].status != EnrollmentStatus::Active) {
            continue;
        }
        TeachingClass enrolled_class{};
        if (!context_.find_teaching_class(enrollments[left].teaching_class_id,
                                          enrolled_class)) {
            problems.push_back(
                "active enrollment of student " +
                std::to_string(enrollments[left].student_id) +
                " refers to missing teaching_class_id " +
                std::to_string(enrollments[left].teaching_class_id));
            continue;
        }
        if (context_.find_student(enrollments[left].student_id) == nullptr) {
            problems.push_back(
                "active enrollment refers to missing student_id " +
                std::to_string(enrollments[left].student_id) +
                " in teaching class " +
                std::to_string(enrollments[left].teaching_class_id));
        }
        for (std::size_t right = left + 1; right < enrollments.size(); ++right) {
            if (enrollments[right].status != EnrollmentStatus::Active) {
                continue;
            }
            if (enrollments[left].student_id == enrollments[right].student_id &&
                enrollments[left].teaching_class_id ==
                    enrollments[right].teaching_class_id) {
                problems.push_back(
                    "student " + std::to_string(enrollments[left].student_id) +
                    " has duplicate active enrollment in teaching class " +
                    std::to_string(enrollments[left].teaching_class_id));
            }
        }
    }

    // 学生不能同时读同一门 Course 的两个教学班，也不能有课表冲突。
    for (const Student& student : context_.students()) {
        std::vector<TeachingClass> taken;
        for (const Enrollment& enrollment :
             context_.active_enrollments(student.student_id)) {
            TeachingClass enrolled_class{};
            if (context_.find_teaching_class(enrollment.teaching_class_id,
                                              enrolled_class)) {
                taken.push_back(enrolled_class);
            }
        }
        for (std::size_t left = 0; left < taken.size(); ++left) {
            for (std::size_t right = left + 1; right < taken.size(); ++right) {
                if (taken[left].course_id == taken[right].course_id) {
                    problems.push_back(
                        "student " + student.student_number +
                        " is enrolled in two teaching classes of course_id " +
                        std::to_string(taken[left].course_id));
                }
                if (teaching_classes_overlap(taken[left], taken[right])) {
                    problems.push_back(
                        "student " + student.student_number +
                        " has a timetable conflict between teaching class " +
                        std::to_string(taken[left].teaching_class_id) +
                        " and " +
                        std::to_string(taken[right].teaching_class_id));
                }
            }
        }
    }

    if (problems.empty()) {
        return {ResultCode::Success, "no inconsistency found"};
    }
    return {ResultCode::InvalidInput,
            std::to_string(problems.size()) + " inconsistent item(s) found"};
}

}  // namespace course_registration
