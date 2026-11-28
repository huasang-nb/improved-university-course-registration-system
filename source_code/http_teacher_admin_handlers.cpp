#include "http_teacher_admin_handlers.hpp"

// ============================================================================
// 教师 / 管理员 HTTP 接口的请求分发（Zhu Ziliang）
//
// 处理顺序（对齐团队总手册第二节与我的操作手册第一节）：
//   1. 检查方法、路径与路径编号；
//   2. 从会话身份确认角色，并重查账号"仍存在 + 仍启用 + 账号名未变"；
//   3. 写操作检查 CSRF；
//   4. 解析 JSON、拒绝未知字段、检查编号范围；
//   5. 在业务锁内调用现有服务（TeacherService / PersistentAdministratorService）；
//   6. 统一映射成 {code, message, data}。
// ============================================================================

#include <algorithm>
#include <climits>
#include <cstddef>
#include <map>
#include <mutex>
#include <regex>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

namespace course_registration {
namespace http {
namespace {

using json = nlohmann::json;

constexpr std::size_t kMaxBodyBytes = 64u * 1024u;
constexpr std::size_t kMaxTextLength = 200u;

// ---------------------------------------------------------------- JSON 视图

json meeting_json(const ClassMeeting& meeting) {
    return json{{"day_of_week", meeting.day_of_week},
                {"start_minute", meeting.start_minute},
                {"end_minute", meeting.end_minute},
                {"room", meeting.room}};
}

json meetings_json(const std::vector<ClassMeeting>& meetings) {
    json data = json::array();
    for (const ClassMeeting& meeting : meetings) {
        data.push_back(meeting_json(meeting));
    }
    return data;
}

json summary_json(const TeachingClassSummary& summary) {
    return json{{"teaching_class_id", summary.teaching_class_id},
                {"course_id", summary.course_id},
                {"course_code", summary.course_code},
                {"course_name", summary.course_name},
                {"credits", summary.credits},
                {"teacher_id", summary.teacher_id},
                {"teacher_name", summary.teacher_name},
                {"capacity", summary.capacity},
                {"active_enrollments", summary.active_enrollments},
                {"remaining_seats", summary.remaining_seats},
                {"meetings", meetings_json(summary.meetings)}};
}

json course_json(const Course& course) {
    return json{{"course_id", course.course_id},
                {"course_code", course.course_code},
                {"course_name", course.course_name},
                {"credits", course.credits}};
}

// 人员响应只输出页面需要的字段：绝不包含 password_hash。
json student_json(const Student& student) {
    return json{{"student_id", student.student_id},
                {"student_number", student.student_number},
                {"name", student.name},
                {"major", student.major},
                {"account_active", student.account_active}};
}

json teacher_json(const Teacher& teacher) {
    return json{{"teacher_id", teacher.teacher_id},
                {"teacher_number", teacher.teacher_number},
                {"name", teacher.name},
                {"department", teacher.department},
                {"account_active", teacher.account_active}};
}

// ---------------------------------------------------------------- 响应构造

Response success_json(json data, const std::string& message) {
    const json body{{"code", "Success"}, {"message", message}, {"data", std::move(data)}};
    return {200, body.dump()};
}

Response failure_json(int status, const std::string& code, const std::string& message) {
    const json body{{"code", code}, {"message", message}, {"data", nullptr}};
    return {status, body.dump()};
}

Response from_result(const OperationResult& result, json data = json()) {
    if (result.code == ResultCode::Success) {
        return success_json(std::move(data), result.message);
    }
    return failure_json(status_for(result.code), name_for(result.code), result.message);
}

// ---------------------------------------------------------------- 会话与权限

// 重查当前人员记录：会话可能因为停用或改账号名而失效。
bool account_still_valid(const SystemContext& context, const UserIdentity& identity) {
    switch (identity.role) {
        case Role::Student: {
            const Student* student = context.find_student(identity.user_id);
            if (student == nullptr || !student->account_active) {
                return false;
            }
            return normalize_account_key(student->student_number) ==
                   normalize_account_key(identity.account_number);
        }
        case Role::Teacher: {
            const Teacher* teacher = context.find_teacher(identity.user_id);
            if (teacher == nullptr || !teacher->account_active) {
                return false;
            }
            return normalize_account_key(teacher->teacher_number) ==
                   normalize_account_key(identity.account_number);
        }
        case Role::Administrator: {
            // SystemContext 没有 find_administrator()：管理员数量少，直接查列表。
            const std::vector<Administrator>& administrators = context.administrators();
            for (const Administrator& administrator : administrators) {
                if (administrator.administrator_id != identity.user_id) {
                    continue;
                }
                return administrator.account_active &&
                       normalize_account_key(administrator.account_number) ==
                           normalize_account_key(identity.account_number);
            }
            return false;
        }
    }
    return false;
}

struct SessionCheck {
    bool ok = false;
    UserIdentity identity{};
    Response error{};
};

SessionCheck require_role(const Request& request, const SystemContext& context, Role expected) {
    if (!request.identity.has_value()) {
        return {false, {}, failure_json(401, "AuthenticationRequired",
                                        "no active session; sign in first")};
    }
    if (request.identity->role != expected) {
        return {false, {}, failure_json(403, "PermissionDenied",
                                        "Permission denied: this endpoint requires a different role")};
    }
    if (!account_still_valid(context, *request.identity)) {
        return {false, {}, failure_json(401, "AuthenticationRequired",
                                        "the session is no longer valid; sign in again")};
    }
    return {true, *request.identity, {}};
}

Response csrf_rejected() {
    return failure_json(403, "CsrfRejected", "missing or invalid X-CSRF-Token");
}

struct AdminAction {
    bool ok = false;
    UserIdentity identity{};
    Response error{};
};

AdminAction require_admin_action(const Request& request, Services& services, bool write) {
    const SessionCheck session = require_role(request, services.context, Role::Administrator);
    if (!session.ok) {
        return {false, {}, session.error};
    }
    if (write && !request.csrf_ok) {
        return {false, {}, csrf_rejected()};
    }
    return {true, session.identity, {}};
}

// ---------------------------------------------------------------- 入参解析

bool body_to_object(const Request& request, json& out, Response& error) {
    if (request.body.size() > kMaxBodyBytes) {
        error = failure_json(400, "InvalidInput", "request body exceeds 64 KiB");
        return false;
    }
    json parsed;
    try {
        parsed = json::parse(request.body);
    } catch (const json::parse_error&) {
        error = failure_json(400, "InvalidInput", "request body is not valid JSON");
        return false;
    }
    if (!parsed.is_object()) {
        error = failure_json(400, "InvalidInput", "request body must be a JSON object");
        return false;
    }
    out = std::move(parsed);
    return true;
}

bool reject_unknown_fields(const json& object,
                           std::initializer_list<const char*> allowed,
                           Response& error) {
    for (auto it = object.begin(); it != object.end(); ++it) {
        bool known = false;
        for (const char* name : allowed) {
            if (it.key() == name) {
                known = true;
                break;
            }
        }
        if (!known) {
            error = failure_json(400, "InvalidInput",
                                 "unknown field \"" + it.key() + "\" in request body");
            return false;
        }
    }
    return true;
}

bool read_int(const json& object, const char* key, int& out, Response& error) {
    if (!object.contains(key)) {
        error = failure_json(400, "InvalidInput", std::string("missing field \"") + key + "\"");
        return false;
    }
    const json& value = object.at(key);
    if (!value.is_number_integer()) {
        error = failure_json(400, "InvalidInput",
                             std::string("\"") + key + "\" must be an integer");
        return false;
    }
    const long long number = value.get<long long>();
    if (number < static_cast<long long>(INT_MIN) || number > static_cast<long long>(INT_MAX)) {
        error = failure_json(400, "InvalidInput",
                             std::string("\"") + key + "\" is out of the 32-bit integer range");
        return false;
    }
    out = static_cast<int>(number);
    return true;
}

bool read_string(const json& object, const char* key, std::string& out, Response& error) {
    if (!object.contains(key)) {
        error = failure_json(400, "InvalidInput", std::string("missing field \"") + key + "\"");
        return false;
    }
    const json& value = object.at(key);
    if (!value.is_string()) {
        error = failure_json(400, "InvalidInput", std::string("\"") + key + "\" must be a string");
        return false;
    }
    out = value.get<std::string>();
    if (out.size() > kMaxTextLength) {
        error = failure_json(400, "InvalidInput",
                             std::string("\"") + key + "\" is longer than 200 characters");
        return false;
    }
    return true;
}

bool read_bool(const json& object, const char* key, bool& out, Response& error) {
    if (!object.contains(key)) {
        error = failure_json(400, "InvalidInput", std::string("missing field \"") + key + "\"");
        return false;
    }
    const json& value = object.at(key);
    if (!value.is_boolean()) {
        error = failure_json(400, "InvalidInput", std::string("\"") + key + "\" must be true or false");
        return false;
    }
    out = value.get<bool>();
    return true;
}

bool parse_meeting(const json& value, ClassMeeting& out, Response& error) {
    if (!value.is_object()) {
        error = failure_json(400, "InvalidInput", "each meeting must be a JSON object");
        return false;
    }
    if (!reject_unknown_fields(value, {"day_of_week", "start_minute", "end_minute", "room"}, error)) {
        return false;
    }
    return read_int(value, "day_of_week", out.day_of_week, error) &&
           read_int(value, "start_minute", out.start_minute, error) &&
           read_int(value, "end_minute", out.end_minute, error) &&
           read_string(value, "room", out.room, error);
}

bool parse_meetings(const json& value, std::vector<ClassMeeting>& out, Response& error) {
    if (!value.is_array()) {
        error = failure_json(400, "InvalidInput", "\"meetings\" must be an array");
        return false;
    }
    out.clear();
    for (const json& item : value) {
        ClassMeeting meeting{};
        if (!parse_meeting(item, meeting, error)) {
            return false;
        }
        out.push_back(meeting);
    }
    return true;
}

bool parse_course(const json& value, Course& out, Response& error) {
    if (!value.is_object()) {
        error = failure_json(400, "InvalidInput", "course must be a JSON object");
        return false;
    }
    if (!reject_unknown_fields(value, {"course_id", "course_code", "course_name", "credits"}, error)) {
        return false;
    }
    return read_int(value, "course_id", out.course_id, error) &&
           read_string(value, "course_code", out.course_code, error) &&
           read_string(value, "course_name", out.course_name, error) &&
           read_int(value, "credits", out.credits, error);
}

bool parse_teaching_class(const json& value, TeachingClass& out, Response& error) {
    if (!value.is_object()) {
        error = failure_json(400, "InvalidInput", "teaching class must be a JSON object");
        return false;
    }
    if (!reject_unknown_fields(value,
                               {"teaching_class_id", "course_id", "teacher_id", "capacity", "meetings"},
                               error)) {
        return false;
    }
    if (!read_int(value, "teaching_class_id", out.teaching_class_id, error) ||
        !read_int(value, "course_id", out.course_id, error) ||
        !read_int(value, "teacher_id", out.teacher_id, error) ||
        !read_int(value, "capacity", out.capacity, error)) {
        return false;
    }
    if (!value.contains("meetings")) {
        error = failure_json(400, "InvalidInput", "missing field \"meetings\"");
        return false;
    }
    return parse_meetings(value.at("meetings"), out.meetings, error);
}

bool parse_student(const json& value, Student& out, Response& error) {
    if (!value.is_object()) {
        error = failure_json(400, "InvalidInput", "student must be a JSON object");
        return false;
    }
    if (!reject_unknown_fields(value,
                               {"student_id", "student_number", "name", "major", "account_active"},
                               error)) {
        return false;
    }
    return read_int(value, "student_id", out.student_id, error) &&
           read_string(value, "student_number", out.student_number, error) &&
           read_string(value, "name", out.name, error) &&
           read_string(value, "major", out.major, error) &&
           read_bool(value, "account_active", out.account_active, error);
}

bool parse_teacher(const json& value, Teacher& out, Response& error) {
    if (!value.is_object()) {
        error = failure_json(400, "InvalidInput", "teacher must be a JSON object");
        return false;
    }
    if (!reject_unknown_fields(
            value, {"teacher_id", "teacher_number", "name", "department", "account_active"}, error)) {
        return false;
    }
    return read_int(value, "teacher_id", out.teacher_id, error) &&
           read_string(value, "teacher_number", out.teacher_number, error) &&
           read_string(value, "name", out.name, error) &&
           read_string(value, "department", out.department, error) &&
           read_bool(value, "account_active", out.account_active, error);
}

// 路径编号：只接受正则已确认的数字串，再检查正整数与 int 范围。
bool parse_path_id(const std::string& text, int& out, Response& error) {
    if (text.empty() || text.size() > 10) {
        error = failure_json(400, "InvalidInput", "path id must be a positive integer");
        return false;
    }
    long long number = 0;
    for (const char character : text) {
        if (character < '0' || character > '9') {
            error = failure_json(400, "InvalidInput", "path id must be a positive integer");
            return false;
        }
        number = number * 10 + (character - '0');
        if (number > static_cast<long long>(INT_MAX)) {
            error = failure_json(400, "InvalidInput", "path id is out of the 32-bit integer range");
            return false;
        }
    }
    if (number <= 0) {
        error = failure_json(400, "InvalidInput", "path id must be a positive integer");
        return false;
    }
    out = static_cast<int>(number);
    return true;
}

// ---------------------------------------------------------------- 路由定义

using Params = std::vector<std::string>;
using Handler = Response (*)(const Request&, Services&, const Params&);

// ---------------------------------------------------------------- 教师只读

Response handle_teacher_classes(const Request& request, Services& services, const Params&) {
    const SessionCheck session = require_role(request, services.context, Role::Teacher);
    if (!session.ok) {
        return session.error;
    }
    std::vector<TeachingClassSummary> classes;
    const OperationResult result = services.teacher.assigned_teaching_classes(session.identity, classes);
    if (result.code != ResultCode::Success) {
        return from_result(result);
    }
    json data = json::array();
    for (const TeachingClassSummary& summary : classes) {
        data.push_back(summary_json(summary));
    }
    return success_json(std::move(data), result.message);
}

Response handle_teacher_summary(const Request& request, Services& services, const Params& params) {
    const SessionCheck session = require_role(request, services.context, Role::Teacher);
    if (!session.ok) {
        return session.error;
    }
    int teaching_class_id = 0;
    Response error;
    if (!parse_path_id(params.at(0), teaching_class_id, error)) {
        return error;
    }
    TeachingClassSummary summary{};
    const OperationResult result =
        services.teacher.class_summary(session.identity, teaching_class_id, summary);
    if (result.code != ResultCode::Success) {
        return from_result(result);
    }
    return success_json(summary_json(summary), result.message);
}

Response handle_teacher_roster(const Request& request, Services& services, const Params& params) {
    const SessionCheck session = require_role(request, services.context, Role::Teacher);
    if (!session.ok) {
        return session.error;
    }
    int teaching_class_id = 0;
    Response error;
    if (!parse_path_id(params.at(0), teaching_class_id, error)) {
        return error;
    }
    ClassRoster roster{};
    const OperationResult result =
        services.teacher.class_roster(session.identity, teaching_class_id, roster);
    if (result.code != ResultCode::Success) {
        return from_result(result);
    }
    json students = json::array();
    for (const RosterEntry& entry : roster.students) {
        students.push_back(json{{"student_id", entry.student_id},
                                {"student_number", entry.student_number},
                                {"name", entry.name}});
    }
    const json data{{"teaching_class_id", roster.teaching_class_id},
                    {"capacity", roster.capacity},
                    {"active_enrollments", roster.active_enrollments},
                    {"remaining_seats", roster.remaining_seats},
                    {"students", std::move(students)}};
    return success_json(data, result.message);
}

// ---------------------------------------------------------------- 管理员查询

Response handle_admin_courses_read(const Request& request, Services& services, const Params&) {
    const AdminAction action = require_admin_action(request, services, false);
    if (!action.ok) {
        return action.error;
    }
    std::vector<TeachingClassSummary> classes;
    const OperationResult result = services.teacher.assigned_teaching_classes(action.identity, classes);
    if (result.code != ResultCode::Success) {
        return from_result(result);
    }
    std::map<int, json> classes_by_course;
    for (const TeachingClassSummary& summary : classes) {
        classes_by_course[summary.course_id].push_back(summary_json(summary));
    }
    json data = json::array();
    for (const Course& course : services.context.courses()) {
        json teaching_classes = json::array();
        const auto found = classes_by_course.find(course.course_id);
        if (found != classes_by_course.end()) {
            teaching_classes = found->second;
        }
        data.push_back(json{{"course", course_json(course)},
                            {"teaching_classes", std::move(teaching_classes)}});
    }
    return success_json(std::move(data), result.message);
}

Response handle_admin_classes_read(const Request& request, Services& services, const Params&) {
    const AdminAction action = require_admin_action(request, services, false);
    if (!action.ok) {
        return action.error;
    }
    std::vector<TeachingClassSummary> classes;
    const OperationResult result = services.teacher.assigned_teaching_classes(action.identity, classes);
    if (result.code != ResultCode::Success) {
        return from_result(result);
    }
    json data = json::array();
    for (const TeachingClassSummary& summary : classes) {
        data.push_back(summary_json(summary));
    }
    return success_json(std::move(data), result.message);
}

Response handle_admin_students_read(const Request& request, Services& services, const Params&) {
    const AdminAction action = require_admin_action(request, services, false);
    if (!action.ok) {
        return action.error;
    }
    json data = json::array();
    for (const Student& student : services.context.students()) {
        data.push_back(student_json(student));
    }
    return success_json(std::move(data), "students returned");
}

Response handle_admin_teachers_read(const Request& request, Services& services, const Params&) {
    const AdminAction action = require_admin_action(request, services, false);
    if (!action.ok) {
        return action.error;
    }
    json data = json::array();
    for (const Teacher& teacher : services.context.teachers()) {
        data.push_back(teacher_json(teacher));
    }
    return success_json(std::move(data), "teachers returned");
}

Response handle_admin_rule_read(const Request& request, Services& services, const Params&) {
    const AdminAction action = require_admin_action(request, services, false);
    if (!action.ok) {
        return action.error;
    }
    const RegistrationRule rule = services.context.rule();
    const json data{{"registration_open", rule.registration_open},
                    {"maximum_credits", rule.maximum_credits},
                    {"opens_at", rule.opens_at},
                    {"closes_at", rule.closes_at},
                    {"round_opened", services.context.round_opened()},
                    {"open_now", services.context.registration_window_open()}};
    return success_json(data, "registration rule returned");
}

// ---------------------------------------------------------------- 管理员写操作

Response handle_admin_courses_create(const Request& request, Services& services, const Params&) {
    const AdminAction action = require_admin_action(request, services, true);
    if (!action.ok) {
        return action.error;
    }
    json body;
    Response error;
    if (!body_to_object(request, body, error)) {
        return error;
    }
    if (!reject_unknown_fields(body, {"course", "first_teaching_class"}, error)) {
        return error;
    }
    if (!body.contains("course") || !body.contains("first_teaching_class")) {
        return failure_json(400, "InvalidInput",
                            "request body must contain \"course\" and \"first_teaching_class\"");
    }
    Course course{};
    TeachingClass first_class{};
    if (!parse_course(body.at("course"), course, error) ||
        !parse_teaching_class(body.at("first_teaching_class"), first_class, error)) {
        return error;
    }
    const OperationResult result =
        services.admin.create_course_with_teaching_class(action.identity, course, first_class);
    if (result.code != ResultCode::Success) {
        return from_result(result);
    }
    const json data{{"course_id", course.course_id},
                    {"teaching_class_id", first_class.teaching_class_id}};
    return success_json(data, result.message);
}

Response handle_admin_course_update(const Request& request, Services& services, const Params& params) {
    const AdminAction action = require_admin_action(request, services, true);
    if (!action.ok) {
        return action.error;
    }
    int course_id = 0;
    Response error;
    if (!parse_path_id(params.at(0), course_id, error)) {
        return error;
    }
    json body;
    if (!body_to_object(request, body, error)) {
        return error;
    }
    Course course{};
    if (!parse_course(body, course, error)) {
        return error;
    }
    if (course.course_id != course_id) {
        return failure_json(400, "InvalidInput",
                            "course_id in the body must match the course_id in the URL");
    }
    const OperationResult result = services.admin.update_course(action.identity, course);
    if (result.code != ResultCode::Success) {
        return from_result(result);
    }
    return success_json(course_json(course), result.message);
}

Response handle_admin_course_delete(const Request& request, Services& services, const Params& params) {
    const AdminAction action = require_admin_action(request, services, true);
    if (!action.ok) {
        return action.error;
    }
    int course_id = 0;
    Response error;
    if (!parse_path_id(params.at(0), course_id, error)) {
        return error;
    }
    return from_result(services.admin.remove_course(action.identity, course_id));
}

Response handle_admin_classes_create(const Request& request, Services& services, const Params&) {
    const AdminAction action = require_admin_action(request, services, true);
    if (!action.ok) {
        return action.error;
    }
    json body;
    Response error;
    if (!body_to_object(request, body, error)) {
        return error;
    }
    TeachingClass teaching_class{};
    if (!parse_teaching_class(body, teaching_class, error)) {
        return error;
    }
    const OperationResult result =
        services.admin.create_teaching_class(action.identity, teaching_class);
    if (result.code != ResultCode::Success) {
        return from_result(result);
    }
    return success_json(json{{"teaching_class_id", teaching_class.teaching_class_id}}, result.message);
}

Response handle_admin_class_update(const Request& request, Services& services, const Params& params) {
    const AdminAction action = require_admin_action(request, services, true);
    if (!action.ok) {
        return action.error;
    }
    int teaching_class_id = 0;
    Response error;
    if (!parse_path_id(params.at(0), teaching_class_id, error)) {
        return error;
    }
    json body;
    if (!body_to_object(request, body, error)) {
        return error;
    }
    TeachingClass teaching_class{};
    if (!parse_teaching_class(body, teaching_class, error)) {
        return error;
    }
    if (teaching_class.teaching_class_id != teaching_class_id) {
        return failure_json(400, "InvalidInput",
                            "teaching_class_id in the body must match the id in the URL");
    }
    const OperationResult result =
        services.admin.update_teaching_class(action.identity, teaching_class);
    if (result.code != ResultCode::Success) {
        return from_result(result);
    }
    return success_json(json{{"teaching_class_id", teaching_class.teaching_class_id}}, result.message);
}

Response handle_admin_class_delete(const Request& request, Services& services, const Params& params) {
    const AdminAction action = require_admin_action(request, services, true);
    if (!action.ok) {
        return action.error;
    }
    int teaching_class_id = 0;
    Response error;
    if (!parse_path_id(params.at(0), teaching_class_id, error)) {
        return error;
    }
    return from_result(services.admin.remove_teaching_class(action.identity, teaching_class_id));
}

Response handle_admin_class_capacity(const Request& request, Services& services, const Params& params) {
    const AdminAction action = require_admin_action(request, services, true);
    if (!action.ok) {
        return action.error;
    }
    int teaching_class_id = 0;
    Response error;
    if (!parse_path_id(params.at(0), teaching_class_id, error)) {
        return error;
    }
    json body;
    if (!body_to_object(request, body, error)) {
        return error;
    }
    if (!reject_unknown_fields(body, {"capacity"}, error)) {
        return error;
    }
    int capacity = 0;
    if (!read_int(body, "capacity", capacity, error)) {
        return error;
    }
    const OperationResult result =
        services.admin.set_teaching_class_capacity(action.identity, teaching_class_id, capacity);
    if (result.code != ResultCode::Success) {
        return from_result(result);
    }
    return success_json(json{{"teaching_class_id", teaching_class_id}, {"capacity", capacity}},
                        result.message);
}

Response handle_admin_students_create(const Request& request, Services& services, const Params&) {
    const AdminAction action = require_admin_action(request, services, true);
    if (!action.ok) {
        return action.error;
    }
    json body;
    Response error;
    if (!body_to_object(request, body, error)) {
        return error;
    }
    Student student{};
    if (!parse_student(body, student, error)) {
        return error;
    }
    const OperationResult result = services.admin.create_student(action.identity, student);
    if (result.code != ResultCode::Success) {
        return from_result(result);
    }
    return success_json(student_json(student), result.message);
}

Response handle_admin_student_update(const Request& request, Services& services, const Params& params) {
    const AdminAction action = require_admin_action(request, services, true);
    if (!action.ok) {
        return action.error;
    }
    int student_id = 0;
    Response error;
    if (!parse_path_id(params.at(0), student_id, error)) {
        return error;
    }
    json body;
    if (!body_to_object(request, body, error)) {
        return error;
    }
    Student student{};
    if (!parse_student(body, student, error)) {
        return error;
    }
    if (student.student_id != student_id) {
        return failure_json(400, "InvalidInput",
                            "student_id in the body must match the student_id in the URL");
    }
    const OperationResult result = services.admin.update_student(action.identity, student);
    if (result.code != ResultCode::Success) {
        return from_result(result);
    }
    return success_json(student_json(student), result.message);
}

Response handle_admin_student_delete(const Request& request, Services& services, const Params& params) {
    const AdminAction action = require_admin_action(request, services, true);
    if (!action.ok) {
        return action.error;
    }
    int student_id = 0;
    Response error;
    if (!parse_path_id(params.at(0), student_id, error)) {
        return error;
    }
    return from_result(services.admin.remove_student(action.identity, student_id));
}

Response handle_admin_teachers_create(const Request& request, Services& services, const Params&) {
    const AdminAction action = require_admin_action(request, services, true);
    if (!action.ok) {
        return action.error;
    }
    json body;
    Response error;
    if (!body_to_object(request, body, error)) {
        return error;
    }
    Teacher teacher{};
    if (!parse_teacher(body, teacher, error)) {
        return error;
    }
    const OperationResult result = services.admin.create_teacher(action.identity, teacher);
    if (result.code != ResultCode::Success) {
        return from_result(result);
    }
    return success_json(teacher_json(teacher), result.message);
}

Response handle_admin_teacher_update(const Request& request, Services& services, const Params& params) {
    const AdminAction action = require_admin_action(request, services, true);
    if (!action.ok) {
        return action.error;
    }
    int teacher_id = 0;
    Response error;
    if (!parse_path_id(params.at(0), teacher_id, error)) {
        return error;
    }
    json body;
    if (!body_to_object(request, body, error)) {
        return error;
    }
    Teacher teacher{};
    if (!parse_teacher(body, teacher, error)) {
        return error;
    }
    if (teacher.teacher_id != teacher_id) {
        return failure_json(400, "InvalidInput",
                            "teacher_id in the body must match the teacher_id in the URL");
    }
    const OperationResult result = services.admin.update_teacher(action.identity, teacher);
    if (result.code != ResultCode::Success) {
        return from_result(result);
    }
    return success_json(teacher_json(teacher), result.message);
}

Response handle_admin_teacher_delete(const Request& request, Services& services, const Params& params) {
    const AdminAction action = require_admin_action(request, services, true);
    if (!action.ok) {
        return action.error;
    }
    int teacher_id = 0;
    Response error;
    if (!parse_path_id(params.at(0), teacher_id, error)) {
        return error;
    }
    return from_result(services.admin.remove_teacher(action.identity, teacher_id));
}

Response handle_admin_period_update(const Request& request, Services& services, const Params&) {
    const AdminAction action = require_admin_action(request, services, true);
    if (!action.ok) {
        return action.error;
    }
    json body;
    Response error;
    if (!body_to_object(request, body, error)) {
        return error;
    }
    if (!reject_unknown_fields(body, {"opens_at", "closes_at"}, error)) {
        return error;
    }
    std::string opens_at;
    std::string closes_at;
    if (!read_string(body, "opens_at", opens_at, error) ||
        !read_string(body, "closes_at", closes_at, error)) {
        return error;
    }
    const OperationResult result =
        services.admin.set_registration_period(action.identity, opens_at, closes_at);
    if (result.code != ResultCode::Success) {
        return from_result(result);
    }
    const json data{{"opens_at", opens_at}, {"closes_at", closes_at}};
    return success_json(data, result.message);
}

Response handle_admin_credit_update(const Request& request, Services& services, const Params&) {
    const AdminAction action = require_admin_action(request, services, true);
    if (!action.ok) {
        return action.error;
    }
    json body;
    Response error;
    if (!body_to_object(request, body, error)) {
        return error;
    }
    if (!reject_unknown_fields(body, {"maximum_credits"}, error)) {
        return error;
    }
    int maximum_credits = 0;
    if (!read_int(body, "maximum_credits", maximum_credits, error)) {
        return error;
    }
    const OperationResult result =
        services.admin.set_maximum_credits(action.identity, maximum_credits);
    if (result.code != ResultCode::Success) {
        return from_result(result);
    }
    return success_json(json{{"maximum_credits", maximum_credits}}, result.message);
}

Response handle_admin_registration_close(const Request& request, Services& services, const Params&) {
    const AdminAction action = require_admin_action(request, services, true);
    if (!action.ok) {
        return action.error;
    }
    if (!request.body.empty()) {
        json body;
        Response error;
        if (!body_to_object(request, body, error)) {
            return error;
        }
        if (!body.empty()) {
            return failure_json(400, "InvalidInput",
                                "closing registration does not accept request fields");
        }
    }
    return from_result(services.admin.close_registration(action.identity));
}

// ---------------------------------------------------------------- 路由表

struct Route {
    const char* method;
    const char* pattern;
    Handler handler;
};

Response not_found(const Request& request) {
    return failure_json(404, "RecordNotFound",
                        "no route for " + request.method + " " + request.path);
}

}  // namespace

int status_for(ResultCode code) {
    switch (code) {
        case ResultCode::Success:
            return 200;
        case ResultCode::InvalidInput:
            return 400;
        case ResultCode::PermissionDenied:
            return 403;
        case ResultCode::RecordNotFound:
        case ResultCode::TeachingClassNotFound:
        case ResultCode::ActiveEnrollmentNotFound:
            return 404;
        case ResultCode::AlreadyEnrolled:
        case ResultCode::TimeConflict:
        case ResultCode::TeachingClassFull:
        case ResultCode::CapacityBelowEnrollment:
        case ResultCode::CatalogInUse:
        case ResultCode::DuplicateIdentifier:
        case ResultCode::CreditLimitExceeded:
        case ResultCode::RegistrationClosed:
            return 409;
        case ResultCode::StorageIoError:
        case ResultCode::StorageParseError:
        case ResultCode::StorageSchemaMismatch:
        case ResultCode::StorageIntegrityError:
        case ResultCode::StorageBackupMissing:
            return 500;
    }
    return 400;
}

const char* name_for(ResultCode code) {
    switch (code) {
        case ResultCode::Success:
            return "Success";
        case ResultCode::RegistrationClosed:
            return "RegistrationClosed";
        case ResultCode::AlreadyEnrolled:
            return "AlreadyEnrolled";
        case ResultCode::TimeConflict:
            return "TimeConflict";
        case ResultCode::CreditLimitExceeded:
            return "CreditLimitExceeded";
        case ResultCode::TeachingClassFull:
            return "TeachingClassFull";
        case ResultCode::TeachingClassNotFound:
            return "TeachingClassNotFound";
        case ResultCode::ActiveEnrollmentNotFound:
            return "ActiveEnrollmentNotFound";
        case ResultCode::PermissionDenied:
            return "PermissionDenied";
        case ResultCode::InvalidInput:
            return "InvalidInput";
        case ResultCode::RecordNotFound:
            return "RecordNotFound";
        case ResultCode::DuplicateIdentifier:
            return "DuplicateIdentifier";
        case ResultCode::CapacityBelowEnrollment:
            return "CapacityBelowEnrollment";
        case ResultCode::CatalogInUse:
            return "CatalogInUse";
        case ResultCode::StorageIoError:
            return "StorageIoError";
        case ResultCode::StorageParseError:
            return "StorageParseError";
        case ResultCode::StorageSchemaMismatch:
            return "StorageSchemaMismatch";
        case ResultCode::StorageIntegrityError:
            return "StorageIntegrityError";
        case ResultCode::StorageBackupMissing:
            return "StorageBackupMissing";
    }
    return "Unknown";
}

Response dispatch(const Request& request, Services& services) {
    // 一把业务锁覆盖身份核对、业务调用与保存（团队总手册第三节）。
    std::lock_guard<std::mutex> guard(services.business_mutex);

    static const std::vector<std::pair<Route, std::regex>> routes = {
        {{"GET", R"(^/api/teacher/classes$)", handle_teacher_classes},
         std::regex(R"(^/api/teacher/classes$)")},
        {{"GET", R"(^/api/teacher/classes/([0-9]+)/summary$)", handle_teacher_summary},
         std::regex(R"(^/api/teacher/classes/([0-9]+)/summary$)")},
        {{"GET", R"(^/api/teacher/classes/([0-9]+)/roster$)", handle_teacher_roster},
         std::regex(R"(^/api/teacher/classes/([0-9]+)/roster$)")},
        {{"GET", R"(^/api/admin/courses$)", handle_admin_courses_read},
         std::regex(R"(^/api/admin/courses$)")},
        {{"POST", R"(^/api/admin/courses$)", handle_admin_courses_create},
         std::regex(R"(^/api/admin/courses$)")},
        {{"PUT", R"(^/api/admin/courses/([0-9]+)$)", handle_admin_course_update},
         std::regex(R"(^/api/admin/courses/([0-9]+)$)")},
        {{"DELETE", R"(^/api/admin/courses/([0-9]+)$)", handle_admin_course_delete},
         std::regex(R"(^/api/admin/courses/([0-9]+)$)")},
        {{"GET", R"(^/api/admin/classes$)", handle_admin_classes_read},
         std::regex(R"(^/api/admin/classes$)")},
        {{"POST", R"(^/api/admin/classes$)", handle_admin_classes_create},
         std::regex(R"(^/api/admin/classes$)")},
        {{"PATCH", R"(^/api/admin/classes/([0-9]+)/capacity$)", handle_admin_class_capacity},
         std::regex(R"(^/api/admin/classes/([0-9]+)/capacity$)")},
        {{"PUT", R"(^/api/admin/classes/([0-9]+)$)", handle_admin_class_update},
         std::regex(R"(^/api/admin/classes/([0-9]+)$)")},
        {{"DELETE", R"(^/api/admin/classes/([0-9]+)$)", handle_admin_class_delete},
         std::regex(R"(^/api/admin/classes/([0-9]+)$)")},
        {{"GET", R"(^/api/admin/students$)", handle_admin_students_read},
         std::regex(R"(^/api/admin/students$)")},
        {{"POST", R"(^/api/admin/students$)", handle_admin_students_create},
         std::regex(R"(^/api/admin/students$)")},
        {{"PUT", R"(^/api/admin/students/([0-9]+)$)", handle_admin_student_update},
         std::regex(R"(^/api/admin/students/([0-9]+)$)")},
        {{"DELETE", R"(^/api/admin/students/([0-9]+)$)", handle_admin_student_delete},
         std::regex(R"(^/api/admin/students/([0-9]+)$)")},
        {{"GET", R"(^/api/admin/teachers$)", handle_admin_teachers_read},
         std::regex(R"(^/api/admin/teachers$)")},
        {{"POST", R"(^/api/admin/teachers$)", handle_admin_teachers_create},
         std::regex(R"(^/api/admin/teachers$)")},
        {{"PUT", R"(^/api/admin/teachers/([0-9]+)$)", handle_admin_teacher_update},
         std::regex(R"(^/api/admin/teachers/([0-9]+)$)")},
        {{"DELETE", R"(^/api/admin/teachers/([0-9]+)$)", handle_admin_teacher_delete},
         std::regex(R"(^/api/admin/teachers/([0-9]+)$)")},
        {{"GET", R"(^/api/admin/registration-rule$)", handle_admin_rule_read},
         std::regex(R"(^/api/admin/registration-rule$)")},
        {{"PUT", R"(^/api/admin/registration-period$)", handle_admin_period_update},
         std::regex(R"(^/api/admin/registration-period$)")},
        {{"PUT", R"(^/api/admin/credit-limit$)", handle_admin_credit_update},
         std::regex(R"(^/api/admin/credit-limit$)")},
        {{"POST", R"(^/api/admin/registration-close$)", handle_admin_registration_close},
         std::regex(R"(^/api/admin/registration-close$)")},
    };

    for (const auto& entry : routes) {
        const Route& route = entry.first;
        if (request.method != route.method) {
            continue;
        }
        std::smatch match;
        if (!std::regex_match(request.path, match, entry.second)) {
            continue;
        }
        Params params;
        for (std::size_t index = 1; index < match.size(); ++index) {
            params.push_back(match[index].str());
        }
        return route.handler(request, services, params);
    }
    return not_found(request);
}

}  // namespace http
}  // namespace course_registration
