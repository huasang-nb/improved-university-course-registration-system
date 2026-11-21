// 学生端控制台演示（Zhang Yikai，第 3 周）
//
// 用法：student_interface_demonstration --state PATH
// 先输入学号模拟登录，然后用菜单做搜索/详情/余量/课表/选课/退课。
// --state 必须显式指定测试用文件，演示不会写入默认的 data/state.json。

#include "student_interface.hpp"

#include <iostream>
#include <string>

using namespace course_registration;

namespace {

bool read_int(const std::string& prompt, int& value) {
    std::cout << prompt;
    std::string line;
    if (!std::getline(std::cin, line)) {
        return false;
    }
    try {
        std::size_t used = 0;
        const int parsed = std::stoi(line, &used);
        if (used == line.size()) {
            value = parsed;
            return true;
        }
    } catch (const std::exception&) {
    }
    return false;
}

void print_course(const CourseView& view) {
    std::cout << "  " << view.course.course_id << " " << view.course.course_code
              << " " << view.course.course_name << " (" << view.course.credits
              << " 学分)\n";
    for (const TeachingClassView& item : view.teaching_classes) {
        std::cout << "    班 " << item.teaching_class_id << " / "
                  << item.teacher_name << " / 容量 " << item.capacity
                  << " / 已选 " << item.active_enrollments << " / 余 "
                  << item.remaining_seats << "\n";
    }
}

}  // namespace

int main(int argc, char** argv) {
    // 必须显式指定测试用状态文件，避免演示时把选退课写进默认的 data/state.json。
    if (argc != 3 || std::string(argv[1]) != "--state") {
        std::cerr << "用法：student_interface_demonstration --state PATH\n"
                     "必须显式指定测试用状态文件（不会写入 data/state.json）。\n";
        return 2;
    }
    StorageOptions storage;
    storage.path = argv[2];

    SystemContext context = make_seed_context();
    const OperationResult initialized = initialize_storage(storage, context);
    if (initialized.code != ResultCode::Success) {
        std::cerr << initialized.message << "\n";
        return 1;
    }

    StudentInterface ui(context, storage);
    std::cout << "学生端控制台（模拟登录，数据文件 " << storage.path << "）\n";
    std::cout << "学号：";
    std::string account;
    if (!std::getline(std::cin, account)) {
        return 0;
    }
    const OperationResult login = ui.login(account);
    std::cout << login.message << "\n";
    if (login.code != ResultCode::Success) {
        return 1;
    }

    while (true) {
        std::cout << "\n1 搜索  2 详情  3 余量  4 我的课表  5 选课  6 退课  "
                     "7 我的状态  0 退出\n> ";
        std::string choice;
        if (!std::getline(std::cin, choice)) {
            break;
        }
        if (choice == "0") {
            break;
        }
        if (choice == "1") {
            std::cout << "关键词：";
            std::string keyword;
            std::getline(std::cin, keyword);
            std::vector<CourseView> courses;
            const OperationResult result = ui.search(keyword, courses);
            if (result.code != ResultCode::Success) {
                std::cout << result.message << "\n";
                continue;
            }
            if (courses.empty()) {
                std::cout << "没有匹配课程\n";
            }
            for (const CourseView& view : courses) {
                print_course(view);
            }
        } else if (choice == "2") {
            int course_id = 0;
            if (!read_int("课程编号：", course_id)) {
                std::cout << "编号必须是整数\n";
                continue;
            }
            CourseView view{};
            const OperationResult result = ui.course_detail(course_id, view);
            if (result.code != ResultCode::Success) {
                std::cout << result.message << "\n";
                continue;
            }
            print_course(view);
        } else if (choice == "3") {
            int class_id = 0;
            if (!read_int("教学班编号：", class_id)) {
                std::cout << "编号必须是整数\n";
                continue;
            }
            SeatsView view{};
            const OperationResult result = ui.available_seats(class_id, view);
            if (result.code != ResultCode::Success) {
                std::cout << result.message << "\n";
                continue;
            }
            std::cout << "班 " << view.teaching_class_id << " "
                      << view.course_code << " 容量 " << view.capacity
                      << " 已选 " << view.active_enrollments << " 余 "
                      << view.remaining_seats << "\n";
        } else if (choice == "4") {
            std::vector<TimetableEntry> table;
            const OperationResult result = ui.timetable(table);
            if (result.code != ResultCode::Success) {
                std::cout << result.message << "\n";
                continue;
            }
            if (table.empty()) {
                std::cout << "还没有选课\n";
            }
            for (const TimetableEntry& entry : table) {
                std::cout << "周" << entry.meeting.day_of_week << " "
                          << entry.meeting.start_minute << "-"
                          << entry.meeting.end_minute << " "
                          << entry.course_code << " " << entry.course_name
                          << " " << entry.meeting.room << "\n";
            }
        } else if (choice == "5" || choice == "6") {
            int class_id = 0;
            if (!read_int("教学班编号：", class_id)) {
                std::cout << "编号必须是整数\n";
                continue;
            }
            const OperationResult result =
                choice == "5" ? ui.enroll(class_id) : ui.drop(class_id);
            std::cout << result.message << "\n";
        } else if (choice == "7") {
            int active = 0;
            int dropped = 0;
            const OperationResult result = ui.my_status(active, dropped);
            if (result.code != ResultCode::Success) {
                std::cout << result.message << "\n";
                continue;
            }
            std::cout << "有效 " << active << " 门，已退 " << dropped
                      << " 门\n";
        } else {
            std::cout << "无效选项\n";
        }
    }
    return 0;
}
