#pragma once

// ============================================================================
// 学生查询模块（Zhang Yikai）
//
// 覆盖三个用例：
//   UC02 搜索 / 查看课程  —— search_courses()、course_detail()
//   UC03 查看剩余名额      —— view_available_seats()
//   UC06 查看课表          —— view_timetable()
//
// 三条不写在别处的规矩：
//   1. 课程、教学班、选课记录都从 SystemContext 取，不另建一份数据。
//   2. 剩余名额 = capacity - 有效选课人数，每次现算，不存计数器。
//   3. 课表由 active 选课记录 + 教学班时间拼出来，不存第二份课表。
// 本模块只读，不创建、不修改、不删除 Enrollment，所以不会和选课检查打架。
// ============================================================================

#include <string>
#include <vector>

#include "teacher_and_administration_services.hpp"

namespace course_registration {

// 课程搜索的筛选条件。字符串留空、整数留 -1 表示这一项不参与筛选。
//
// 关键词和 teacher_id 同时给的时候，两者要由**同一个教学班**同时满足：
// 搜 "Wang" 再筛 Zhao，不会返回 Zhao 的班。课程代码或名称本身命中关键词时，
// 只要这门课确实有这位老师的班就算命中，返回的教学班仍只留这位老师的。
struct CourseSearchFilter {
    std::string course_code{};
    int credits{-1};
    int teacher_id{-1};
};

// 课程详情里的一条教学班。人数和余量都在读取时现算。
struct TeachingClassView {
    int teaching_class_id;
    int teacher_id;
    std::string teacher_name;
    int capacity;
    int active_enrollments;
    int remaining_seats;
    std::vector<ClassMeeting> meetings;
};

// 一门课程 + 它下面的教学班。
struct CourseView {
    Course course;
    std::vector<TeachingClassView> teaching_classes;
};

// UC03 的结果：容量、有效人数、剩余名额。
struct SeatsView {
    int teaching_class_id;
    int course_id;
    std::string course_code;
    std::string course_name;
    int capacity;
    int active_enrollments;
    int remaining_seats;
};

// 课表的一行：某个教学班的一次上课时段。
struct TimetableEntry {
    int teaching_class_id;
    std::string course_code;
    std::string course_name;
    std::string teacher_name;
    ClassMeeting meeting;
};

// 学生端只读查询。构造时传入共享上下文，之后不再持有任何副本。
//
// 四个接口的输出参数用的是同一条约定：**进来先清空，失败时留空**。
// 所以查不到、没权限、查无此人时，result 里不会残留上一次查询的数据；
// 但调用方仍然要先看 OperationResult，成功才展示——清空只是兜底，
// 不是“可以不看结果码”的许可。
class StudentQueryService {
public:
    explicit StudentQueryService(const SystemContext& context);

    // UC02 搜索。关键词按课程代码、课程名称、教师姓名三处做不区分大小写的
    // 包含匹配；没有匹配时返回 Success + 空列表，由界面给出“没有匹配课程”。
    // 关键词靠教师姓名命中时，只算命中那一位老师的教学班；再叠教师筛选，
    // 两者必须是同一个班，不同班的组合不会返回。
    OperationResult search_courses(const std::string& keyword,
                                   const CourseSearchFilter& filter,
                                   std::vector<CourseView>& result) const;

    // UC02 查看某一门课程的详情（含教学班、教师、时间、余量）。
    OperationResult course_detail(int course_id, CourseView& result) const;

    // UC03 查看某个教学班的剩余名额。教学班不存在返回 RecordNotFound。
    OperationResult view_available_seats(int teaching_class_id,
                                         SeatsView& result) const;

    // UC06 查看课表。只允许学生本人查看；按星期几、开始时间排好序返回。
    // 没有任何有效选课时返回 Success + 空列表（界面提示尚未选课）。
    OperationResult view_timetable(const UserIdentity& user,
                                   std::vector<TimetableEntry>& result) const;

private:
    const SystemContext& context_;

    // 把一门课程连同它的教学班组装成视图，搜索和详情共用这一处。
    CourseView build_course_view(const Course& course,
                                 int teacher_filter) const;
    bool course_matches(const Course& course,
                        const std::string& lowered_keyword,
                        const CourseSearchFilter& filter) const;
    bool any_teaching_class_matches(const Course& course, int teacher_id) const;
};

}  // namespace course_registration
