// 学生查询模块的命令行演示（Zhang Yikai）
// 用法：编译后直接运行，按 UC02 / UC03 / UC06 的顺序打印结果。
// 这里只用打印，不做断言；断言在 automated_tests 里。

#include "student_query_and_timetable_services.hpp"

#include <iostream>
#include <string>
#include <vector>

using namespace course_registration;

namespace {

const char* day_name(int day_of_week) {
    switch (day_of_week) {
        case 1: return "Monday";
        case 2: return "Tuesday";
        case 3: return "Wednesday";
        case 4: return "Thursday";
        case 5: return "Friday";
        case 6: return "Saturday";
        case 7: return "Sunday";
        default: return "Unknown";
    }
}

std::string clock_text(int minute) {
    const int hour = minute / 60;
    const int rest = minute % 60;
    std::string text = std::to_string(hour);
    text += ":";
    if (rest < 10) {
        text += "0";
    }
    text += std::to_string(rest);
    return text;
}

}  // namespace

int main() {
    // 演示数据与 documentation/interfaces.md 第 4.1 节一致。
    std::vector<Course> courses{
        {1, "SE101", "Software Engineering", 3},
        {2, "MA101", "Mathematics", 4},
        {3, "CS101", "Programming", 5}};
    std::vector<TeachingClass> teaching_classes{
        {10, 1, 100, 2, {{1, 540, 600, "A101"}}},
        {11, 1, 300, 2, {{2, 540, 600, "A102"}}},
        {20, 2, 200, 2, {{1, 570, 630, "B201"}}},
        {30, 3, 300, 2, {{3, 840, 900, "C301"}}},
    };
    std::vector<Student> students{
        {1001, "S2023001", "Alice Chen", "Computer Science", true},
        {1002, "S2023002", "Bob Liu", "Computer Science", true}};
    std::vector<Teacher> teachers{
        {100, "T2015007", "Dr. Wang", "Software Engineering", true},
        {200, "T2016011", "Dr. Li", "Mathematics", true},
        {300, "T2018003", "Dr. Zhao", "Computer Science", true}};
    std::vector<Administrator> administrators{
        {900, "A2020001", "Admin Sun", true}};

    SystemContext context(students, teachers, administrators, courses,
                          teaching_classes, RegistrationRule{true, 18});
    StudentQueryService query(context);

    std::cout << "=== UC02 search: keyword = software ===\n";
    std::vector<CourseView> found;
    query.search_courses("software", CourseSearchFilter{}, found);
    if (found.empty()) {
        std::cout << "No course matches the keyword.\n";
    }
    for (const CourseView& view : found) {
        std::cout << view.course.course_code << "  " << view.course.course_name
                  << "  (" << view.course.credits << " credits)\n";
        for (const TeachingClassView& item : view.teaching_classes) {
            std::cout << "   class " << item.teaching_class_id << "  "
                      << item.teacher_name << "  " << item.meetings[0].room
                      << "  seats left: " << item.remaining_seats << "\n";
        }
    }

    std::cout << "\n=== UC02 search: keyword = quantum (should be empty) ===\n";
    query.search_courses("quantum", CourseSearchFilter{}, found);
    if (found.empty()) {
        std::cout << "No course matches the keyword.\n";
    }

    std::cout << "\n=== UC02 course detail: course 1 ===\n";
    CourseView detail;
    if (query.course_detail(1, detail).code == ResultCode::Success) {
        std::cout << detail.course.course_code << "  " << detail.course.course_name
                  << "\n";
        for (const TeachingClassView& item : detail.teaching_classes) {
            std::cout << "   class " << item.teaching_class_id << "  teacher "
                      << item.teacher_name << "  capacity " << item.capacity
                      << "  active " << item.active_enrollments << "  left "
                      << item.remaining_seats << "\n";
        }
    } else {
        std::cout << "Course not found.\n";
    }

    std::cout << "\n=== UC03 seats: class 10, before and after enrolling ===\n";
    SeatsView seats;
    query.view_available_seats(10, seats);
    std::cout << "before  capacity=" << seats.capacity
              << " active=" << seats.active_enrollments
              << " remaining=" << seats.remaining_seats << "\n";
    context.registration().enroll(1001, 10);
    context.registration().enroll(1001, 30);
    query.view_available_seats(10, seats);
    std::cout << "after   capacity=" << seats.capacity
              << " active=" << seats.active_enrollments
              << " remaining=" << seats.remaining_seats << "\n";

    std::cout << "\n=== UC06 timetable: student 1001 ===\n";
    UserIdentity alice{1001, "S2023001", Role::Student};
    std::vector<TimetableEntry> timetable;
    query.view_timetable(alice, timetable);
    if (timetable.empty()) {
        std::cout << "You have not enrolled in any teaching class.\n";
    }
    for (const TimetableEntry& entry : timetable) {
        std::cout << day_name(entry.meeting.day_of_week) << "  "
                  << clock_text(entry.meeting.start_minute) << "-"
                  << clock_text(entry.meeting.end_minute) << "  "
                  << entry.course_code << "  " << entry.teacher_name << "  "
                  << entry.meeting.room << "\n";
    }

    std::cout << "\n=== UC06 timetable: student 1002 (nothing enrolled) ===\n";
    UserIdentity bob{1002, "S2023002", Role::Student};
    query.view_timetable(bob, timetable);
    if (timetable.empty()) {
        std::cout << "You have not enrolled in any teaching class.\n";
    }

    std::cout << "\n=== permission check: teacher asking for a student timetable ===\n";
    UserIdentity teacher{100, "T2015007", Role::Teacher};
    const OperationResult denied = query.view_timetable(teacher, timetable);
    std::cout << "result: " << denied.message << "\n";

    return 0;
}
