#include "student_query_and_timetable_services.hpp"

#include <algorithm>
#include <cctype>

namespace course_registration {
namespace {

// 统一转小写再比较，这样 "SE101" 和 "se101"、"Wang" 和 "wang" 都能匹配到。
std::string to_lower(std::string text) {
    for (char& ch : text) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return text;
}

bool contains_text(const std::string& haystack, const std::string& needle) {
    if (needle.empty()) {
        return true;
    }
    return haystack.find(needle) != std::string::npos;
}

// 教学班按编号排序，保证同样的数据每次露出同样的顺序。
bool by_teaching_class_id(const TeachingClassView& left,
                          const TeachingClassView& right) {
    return left.teaching_class_id < right.teaching_class_id;
}

}  // namespace

StudentQueryService::StudentQueryService(const SystemContext& context)
    : context_(context) {}

bool StudentQueryService::any_teaching_class_matches(const Course& course,
                                                     int teacher_id) const {
    for (const TeachingClass& teaching_class : context_.teaching_classes()) {
        if (teaching_class.course_id == course.course_id &&
            teaching_class.teacher_id == teacher_id) {
            return true;
        }
    }
    return false;
}

bool StudentQueryService::course_matches(
    const Course& course,
    const std::string& lowered_keyword,
    const CourseSearchFilter& filter) const {
    // 先看筛选条件；只要有一项对不上，直接排除，不再算关键词。
    if (!filter.course_code.empty() &&
        to_lower(course.course_code) != to_lower(filter.course_code)) {
        return false;
    }
    if (filter.credits >= 0 && course.credits != filter.credits) {
        return false;
    }
    // 关键词命中课程代码或课程名称，说明这门课本身就对得上。
    const bool course_level_match =
        contains_text(to_lower(course.course_code), lowered_keyword) ||
        contains_text(to_lower(course.course_name), lowered_keyword);

    // 没指定教师：课程级命中就算过，否则再看有没有哪位老师的名字带关键词。
    if (filter.teacher_id < 0) {
        if (course_level_match) {
            return true;
        }
        for (const TeachingClass& teaching_class : context_.teaching_classes()) {
            if (teaching_class.course_id != course.course_id) {
                continue;
            }
            const Teacher* teacher = context_.find_teacher(teaching_class.teacher_id);
            if (teacher != nullptr &&
                contains_text(to_lower(teacher->name), lowered_keyword)) {
                return true;
            }
        }
        return false;
    }

    // 指定了教师：课程级命中时，只要这门课确实有这位老师的班就行。
    if (course_level_match) {
        return any_teaching_class_matches(course, filter.teacher_id);
    }

    // 关键词只能靠老师名字命中时，关键词和筛选必须落在同一个教学班。
    // 否则搜 "Wang" 又筛 Zhao，会拿 Wang 的班凑关键词、拿 Zhao 的班凑筛选，
    // 返回一堆学生根本没在找的班。
    for (const TeachingClass& teaching_class : context_.teaching_classes()) {
        if (teaching_class.course_id != course.course_id ||
            teaching_class.teacher_id != filter.teacher_id) {
            continue;
        }
        const Teacher* teacher = context_.find_teacher(teaching_class.teacher_id);
        if (teacher != nullptr &&
            contains_text(to_lower(teacher->name), lowered_keyword)) {
            return true;
        }
    }
    return false;
}

CourseView StudentQueryService::build_course_view(const Course& course,
                                                  int teacher_filter) const {
    CourseView view;
    view.course = course;
    for (const TeachingClass& teaching_class : context_.teaching_classes()) {
        if (teaching_class.course_id != course.course_id) {
            continue;
        }
        if (teacher_filter >= 0 && teaching_class.teacher_id != teacher_filter) {
            continue;
        }
        TeachingClassView class_view;
        class_view.teaching_class_id = teaching_class.teaching_class_id;
        class_view.teacher_id = teaching_class.teacher_id;
        const Teacher* teacher = context_.find_teacher(teaching_class.teacher_id);
        class_view.teacher_name = teacher == nullptr ? std::string() : teacher->name;
        class_view.capacity = teaching_class.capacity;
        // 人数和余量都现算，和教师端、管理员端看到的是同一批记录。
        class_view.active_enrollments =
            context_.enrollment_count(teaching_class.teaching_class_id);
        class_view.remaining_seats =
            teaching_class.capacity - class_view.active_enrollments;
        class_view.meetings = teaching_class.meetings;
        view.teaching_classes.push_back(class_view);
    }
    std::sort(view.teaching_classes.begin(), view.teaching_classes.end(),
              by_teaching_class_id);
    return view;
}

OperationResult StudentQueryService::search_courses(
    const std::string& keyword,
    const CourseSearchFilter& filter,
    std::vector<CourseView>& result) const {
    result.clear();
    const std::string lowered_keyword = to_lower(keyword);

    for (const Course& course : context_.courses()) {
        if (!course_matches(course, lowered_keyword, filter)) {
            continue;
        }
        result.push_back(build_course_view(course, filter.teacher_id));
    }
    std::sort(result.begin(), result.end(),
              [](const CourseView& left, const CourseView& right) {
                  return left.course.course_code < right.course.course_code;
              });

    // 没有匹配不是错误：界面按用例要求提示“没有课程匹配这个关键词”。
    return {ResultCode::Success, "Search completed"};
}

OperationResult StudentQueryService::course_detail(int course_id,
                                                   CourseView& result) const {
    // 进来先清空：查不到时要留下空结果，而不是上一次查询的内容。
    // 网页那边就算忘了看结果码，也只会看到空课程，不会把旧课程当新课程显示。
    result = CourseView{};
    Course course{};
    if (!context_.find_course(course_id, course)) {
        return {ResultCode::RecordNotFound, "Course not found"};
    }
    result = build_course_view(course, -1);
    return {ResultCode::Success, "Course found"};
}

OperationResult StudentQueryService::view_available_seats(
    int teaching_class_id, SeatsView& result) const {
    // 同样是先清空，教学班不存在时返回全 0，不会残留上一个班的容量和余量。
    result = SeatsView{};
    TeachingClass teaching_class{};
    if (!context_.find_teaching_class(teaching_class_id, teaching_class)) {
        return {ResultCode::RecordNotFound, "Teaching class not found"};
    }
    Course course{};
    context_.find_course(teaching_class.course_id, course);

    result.teaching_class_id = teaching_class.teaching_class_id;
    result.course_id = teaching_class.course_id;
    result.course_code = course.course_code;
    result.course_name = course.course_name;
    result.capacity = teaching_class.capacity;
    result.active_enrollments = context_.enrollment_count(teaching_class_id);
    result.remaining_seats = teaching_class.capacity - result.active_enrollments;
    return {ResultCode::Success, "Seats available"};
}

OperationResult StudentQueryService::view_timetable(
    const UserIdentity& user, std::vector<TimetableEntry>& result) const {
    result.clear();
    // 课表属于学生本人，教师和管理员走各自的入口，不从这里读。
    if (user.role != Role::Student) {
        return {ResultCode::PermissionDenied, "Only students have a timetable"};
    }
    const Student* student = context_.find_student(user.user_id);
    if (student == nullptr) {
        return {ResultCode::RecordNotFound, "Student not found"};
    }

    // 只取有效选课；已退课的记录留在库里，但不再出现在课表上。
    for (const Enrollment& enrollment :
         context_.active_enrollments(student->student_id)) {
        TeachingClass teaching_class{};
        if (!context_.find_teaching_class(enrollment.teaching_class_id,
                                          teaching_class)) {
            // 目录里查不到这条教学班，说明数据已经不一致；跳过这一条，
            // 不让一条坏记录把整张课表打不开，问题由管理员一致性检查去报。
            continue;
        }
        Course course{};
        context_.find_course(teaching_class.course_id, course);
        const Teacher* teacher = context_.find_teacher(teaching_class.teacher_id);

        for (const ClassMeeting& meeting : teaching_class.meetings) {
            TimetableEntry entry;
            entry.teaching_class_id = teaching_class.teaching_class_id;
            entry.course_code = course.course_code;
            entry.course_name = course.course_name;
            entry.teacher_name = teacher == nullptr ? std::string() : teacher->name;
            entry.meeting = meeting;
            result.push_back(entry);
        }
    }

    // 周一到周日、同一天按开始时间排，界面直接按顺序显示即可。
    std::sort(result.begin(), result.end(),
              [](const TimetableEntry& left, const TimetableEntry& right) {
                  if (left.meeting.day_of_week != right.meeting.day_of_week) {
                      return left.meeting.day_of_week < right.meeting.day_of_week;
                  }
                  if (left.meeting.start_minute != right.meeting.start_minute) {
                      return left.meeting.start_minute < right.meeting.start_minute;
                  }
                  return left.teaching_class_id < right.teaching_class_id;
              });

    return {ResultCode::Success, "Timetable generated"};
}

}  // namespace course_registration
