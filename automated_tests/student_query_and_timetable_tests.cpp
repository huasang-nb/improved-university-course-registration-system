#include "student_query_and_timetable_services.hpp"

#include <cassert>
#include <iostream>
#include <string>
#include <vector>

using namespace course_registration;

int main() {
    // 演示数据按 documentation/interfaces.md 第 4.1 节，和另外两人的测试一致。
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
    RegistrationRule rule{true, 18};

    SystemContext context(students, teachers, administrators, courses,
                          teaching_classes, rule);
    StudentQueryService query(context);

    std::vector<CourseView> found;
    CourseSearchFilter no_filter;

    // 1. 按课程名称搜索，大小写不影响结果。
    assert(query.search_courses("software", no_filter, found).code ==
           ResultCode::Success);
    assert(found.size() == 1);
    assert(found[0].course.course_code == "SE101");
    assert(found[0].teaching_classes.size() == 2);
    assert(query.search_courses("SOFT", no_filter, found).code ==
           ResultCode::Success);
    assert(found.size() == 1);

    // 2. 关键词也能命中教师姓名。
    assert(query.search_courses("wang", no_filter, found).code ==
           ResultCode::Success);
    assert(found.size() == 1);
    assert(query.search_courses("Zhao", no_filter, found).code ==
           ResultCode::Success);
    assert(found.size() == 2);

    // 3. 没有匹配时返回空列表而不是报错，界面按用例要求提示没有匹配。
    assert(query.search_courses("nothing here", no_filter, found).code ==
           ResultCode::Success);
    assert(found.empty());

    // 4. 课程代码、学分、教师三种筛选各自生效。
    CourseSearchFilter by_code;
    by_code.course_code = "ma101";
    assert(query.search_courses("", by_code, found).code == ResultCode::Success);
    assert(found.size() == 1);
    assert(found[0].course.course_id == 2);

    CourseSearchFilter by_credits;
    by_credits.credits = 5;
    assert(query.search_courses("", by_credits, found).code ==
           ResultCode::Success);
    assert(found.size() == 1);
    assert(found[0].course.course_code == "CS101");

    CourseSearchFilter by_teacher;
    by_teacher.teacher_id = 300;
    assert(query.search_courses("", by_teacher, found).code ==
           ResultCode::Success);
    assert(found.size() == 2);
    // 按教师筛选时，课程详情里只留这位教师的教学班。
    for (const CourseView& view : found) {
        assert(view.teaching_classes.size() == 1);
        assert(view.teaching_classes[0].teacher_id == 300);
    }

    // 5. 课程详情：教学班、教师姓名、容量都在；不存在的编号要报找不到。
    CourseView detail;
    assert(query.course_detail(1, detail).code == ResultCode::Success);
    assert(detail.teaching_classes.size() == 2);
    assert(detail.teaching_classes[0].teacher_name == "Dr. Wang");
    assert(detail.teaching_classes[1].teacher_name == "Dr. Zhao");
    assert(query.course_detail(99, detail).code == ResultCode::RecordNotFound);

    // 6. 剩余名额 = 容量减有效人数，选课和退课之后都跟着变。
    SeatsView seats;
    assert(query.view_available_seats(10, seats).code == ResultCode::Success);
    assert(seats.capacity == 2);
    assert(seats.active_enrollments == 0);
    assert(seats.remaining_seats == 2);

    assert(context.registration().enroll(1001, 10).code == ResultCode::Success);
    assert(query.view_available_seats(10, seats).code == ResultCode::Success);
    assert(seats.active_enrollments == 1);
    assert(seats.remaining_seats == 1);

    assert(context.registration().drop(1001, 10).code == ResultCode::Success);
    assert(query.view_available_seats(10, seats).code == ResultCode::Success);
    assert(seats.remaining_seats == 2);
    assert(query.view_available_seats(999, seats).code ==
           ResultCode::RecordNotFound);

    // 7. 课表只由 active 记录生成，并按星期、开始时间排序。
    UserIdentity alice{1001, "S2023001", Role::Student};
    UserIdentity bob{1002, "S2023002", Role::Student};
    UserIdentity teacher{100, "T2015007", Role::Teacher};

    std::vector<TimetableEntry> timetable;
    assert(query.view_timetable(alice, timetable).code == ResultCode::Success);
    assert(timetable.empty());  // 还没选课，课表为空，界面提示尚未选课

    assert(context.registration().enroll(1001, 10).code == ResultCode::Success);
    assert(context.registration().enroll(1001, 30).code == ResultCode::Success);
    assert(query.view_timetable(alice, timetable).code == ResultCode::Success);
    assert(timetable.size() == 2);
    assert(timetable[0].meeting.day_of_week == 1);  // 周一 09:00
    assert(timetable[0].course_code == "SE101");
    assert(timetable[1].meeting.day_of_week == 3);  // 周三 14:00
    assert(timetable[1].course_code == "CS101");

    // 退课之后课表里不再出现这门课。
    assert(context.registration().drop(1001, 30).code == ResultCode::Success);
    assert(query.view_timetable(alice, timetable).code == ResultCode::Success);
    assert(timetable.size() == 1);
    assert(timetable[0].course_code == "SE101");

    // 8. 权限：教师没有学生课表，编号对不上要报找不到。
    assert(query.view_timetable(teacher, timetable).code ==
           ResultCode::PermissionDenied);
    UserIdentity ghost{9999, "S9999999", Role::Student};
    assert(query.view_timetable(ghost, timetable).code ==
           ResultCode::RecordNotFound);
    assert(query.view_timetable(bob, timetable).code == ResultCode::Success);
    assert(timetable.empty());

    // 9. 查询失败后不能残留上一次的结果：先查存在的，再查不存在的。
    //    顺序故意反过来，就是为了抓“忘了清空”这类问题。
    assert(query.course_detail(1, detail).code == ResultCode::Success);
    assert(detail.course.course_code == "SE101");
    assert(query.course_detail(9999, detail).code == ResultCode::RecordNotFound);
    assert(detail.course.course_code.empty());      // 旧课程代码不能留下
    assert(detail.course.course_name.empty());
    assert(detail.teaching_classes.empty());        // 旧教学班列表也不能留下

    // 剩余名额同一个变量连查两次，第二次的教学班不存在。
    assert(query.view_available_seats(10, seats).code == ResultCode::Success);
    assert(seats.capacity == 2);
    assert(seats.remaining_seats == 2 - seats.active_enrollments);  // 按公式，不写死
    assert(query.view_available_seats(9999, seats).code ==
           ResultCode::RecordNotFound);
    assert(seats.teaching_class_id == 0);
    assert(seats.course_code.empty());
    assert(seats.capacity == 0);
    assert(seats.remaining_seats == 0);             // 不能把上一个班的余量留下

    // 课表：先取到 1 条，再用教师身份请求，被拒时也必须是空列表。
    assert(query.view_timetable(alice, timetable).code == ResultCode::Success);
    assert(timetable.size() == 1);
    assert(query.view_timetable(teacher, timetable).code ==
           ResultCode::PermissionDenied);
    assert(timetable.empty());

    std::cout << "All student query and timetable tests passed\n";
}
