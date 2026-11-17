// ============================================================================
// 持久化实现（Zhang Yikai）
//
// 设计：一个 state.json 快照 + .tmp / .bak 两份辅助文件。
//   - 写：序列化 -> 写 .tmp -> 备份旧文件为 .bak -> 原子替换（Windows 用
//     MoveFileEx(REPLACE_EXISTING)，其他平台用 filesystem::rename）。
//   - 读：解析 -> 校验（schema / 引用 / 唯一性）-> 才替换 out_context。
//   - 失败：不碰内存、不删原文件，返回带原因的 OperationResult。
//
// 为了不引入第三方依赖，这里带一个只够本文件使用的 JSON 读写子集：
// 对象、数组、字符串、整数、布尔、null；字符串支持 \" \\ \n \r \t \b \f \uXXXX。
// ============================================================================

#include "persistence_service.hpp"

#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace course_registration {
namespace {

constexpr int kSchemaVersion = 1;

// ---------------------------------------------------------------------------
// JSON 值
// ---------------------------------------------------------------------------

struct JsonValue {
    enum class Type { Null, Bool, Int, String, Array, Object };

    Type type = Type::Null;
    bool boolean = false;
    long long integer = 0;
    std::string text;
    std::vector<JsonValue> items;
    std::vector<std::pair<std::string, JsonValue>> members;

    const JsonValue* find(const std::string& key) const {
        for (const auto& member : members) {
            if (member.first == key) {
                return &member.second;
            }
        }
        return nullptr;
    }
};

// ---------------------------------------------------------------------------
// JSON 解析
// ---------------------------------------------------------------------------

int hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

void append_utf8(std::string& out, unsigned code) {
    if (code <= 0x7F) {
        out.push_back(static_cast<char>(code));
    } else if (code <= 0x7FF) {
        out.push_back(static_cast<char>(0xC0 | (code >> 6)));
        out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xE0 | (code >> 12)));
        out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
    }
}

class JsonParser {
public:
    explicit JsonParser(const std::string& text) : text_(text) {}

    bool parse(JsonValue& out, std::string& error) {
        skip_ws();
        if (!parse_value(out)) {
            error = error_;
            return false;
        }
        skip_ws();
        if (pos_ != text_.size()) {
            error = "trailing characters at offset " + std::to_string(pos_);
            return false;
        }
        return true;
    }

private:
    const std::string& text_;
    size_t pos_ = 0;
    std::string error_;

    bool fail(const std::string& what) {
        if (error_.empty()) {
            error_ = what + " at offset " + std::to_string(pos_);
        }
        return false;
    }

    void skip_ws() {
        while (pos_ < text_.size()) {
            const char c = text_[pos_];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                ++pos_;
            } else {
                break;
            }
        }
    }

    bool parse_value(JsonValue& out) {
        skip_ws();
        if (pos_ >= text_.size()) {
            return fail("unexpected end of input");
        }
        const char c = text_[pos_];
        if (c == '{') return parse_object(out);
        if (c == '[') return parse_array(out);
        if (c == '"') {
            out = JsonValue{};
            out.type = JsonValue::Type::String;
            return parse_string(out.text);
        }
        if (c == 't') {
            if (text_.compare(pos_, 4, "true") == 0) {
                pos_ += 4;
                out = JsonValue{};
                out.type = JsonValue::Type::Bool;
                out.boolean = true;
                return true;
            }
            return fail("invalid literal");
        }
        if (c == 'f') {
            if (text_.compare(pos_, 5, "false") == 0) {
                pos_ += 5;
                out = JsonValue{};
                out.type = JsonValue::Type::Bool;
                out.boolean = false;
                return true;
            }
            return fail("invalid literal");
        }
        if (c == 'n') {
            if (text_.compare(pos_, 4, "null") == 0) {
                pos_ += 4;
                out = JsonValue{};
                out.type = JsonValue::Type::Null;
                return true;
            }
            return fail("invalid literal");
        }
        if (c == '-' || (c >= '0' && c <= '9')) {
            return parse_number(out);
        }
        return fail("unexpected character");
    }

    bool parse_number(JsonValue& out) {
        const size_t start = pos_;
        if (pos_ < text_.size() && text_[pos_] == '-') {
            ++pos_;
        }
        while (pos_ < text_.size() &&
               std::isdigit(static_cast<unsigned char>(text_[pos_]))) {
            ++pos_;
        }
        if (pos_ == start || (pos_ == start + 1 && text_[start] == '-')) {
            return fail("invalid number");
        }
        const std::string token = text_.substr(start, pos_ - start);
        try {
            out = JsonValue{};
            out.type = JsonValue::Type::Int;
            out.integer = std::stoll(token);
        } catch (...) {
            return fail("number out of range");
        }
        return true;
    }

    bool parse_string(std::string& out) {
        if (pos_ >= text_.size() || text_[pos_] != '"') {
            return fail("expected string");
        }
        ++pos_;
        out.clear();
        while (pos_ < text_.size()) {
            const char c = text_[pos_];
            if (c == '"') {
                ++pos_;
                return true;
            }
            if (c == '\\') {
                ++pos_;
                if (pos_ >= text_.size()) {
                    return fail("unterminated escape");
                }
                const char e = text_[pos_];
                switch (e) {
                    case '"': out.push_back('"'); break;
                    case '\\': out.push_back('\\'); break;
                    case '/': out.push_back('/'); break;
                    case 'b': out.push_back('\b'); break;
                    case 'f': out.push_back('\f'); break;
                    case 'n': out.push_back('\n'); break;
                    case 'r': out.push_back('\r'); break;
                    case 't': out.push_back('\t'); break;
                    case 'u': {
                        if (pos_ + 4 >= text_.size()) {
                            return fail("short \\u escape");
                        }
                        unsigned code = 0;
                        for (int k = 1; k <= 4; ++k) {
                            const int digit = hex_digit(text_[pos_ + k]);
                            if (digit < 0) {
                                return fail("invalid \\u escape");
                            }
                            code = code * 16u + static_cast<unsigned>(digit);
                        }
                        append_utf8(out, code);
                        pos_ += 4;
                        break;
                    }
                    default:
                        return fail("unknown escape");
                }
                ++pos_;
            } else {
                out.push_back(c);
                ++pos_;
            }
        }
        return fail("unterminated string");
    }

    bool parse_array(JsonValue& out) {
        out = JsonValue{};
        out.type = JsonValue::Type::Array;
        ++pos_;  // '['
        skip_ws();
        if (pos_ < text_.size() && text_[pos_] == ']') {
            ++pos_;
            return true;
        }
        while (true) {
            JsonValue item;
            if (!parse_value(item)) {
                return false;
            }
            out.items.push_back(std::move(item));
            skip_ws();
            if (pos_ >= text_.size()) {
                return fail("unterminated array");
            }
            if (text_[pos_] == ',') {
                ++pos_;
                continue;
            }
            if (text_[pos_] == ']') {
                ++pos_;
                return true;
            }
            return fail("expected , or ]");
        }
    }

    bool parse_object(JsonValue& out) {
        out = JsonValue{};
        out.type = JsonValue::Type::Object;
        ++pos_;  // '{'
        skip_ws();
        if (pos_ < text_.size() && text_[pos_] == '}') {
            ++pos_;
            return true;
        }
        while (true) {
            skip_ws();
            if (pos_ >= text_.size() || text_[pos_] != '"') {
                return fail("expected key string");
            }
            std::string key;
            if (!parse_string(key)) {
                return false;
            }
            skip_ws();
            if (pos_ >= text_.size() || text_[pos_] != ':') {
                return fail("expected :");
            }
            ++pos_;
            JsonValue value;
            if (!parse_value(value)) {
                return false;
            }
            out.members.emplace_back(std::move(key), std::move(value));
            skip_ws();
            if (pos_ >= text_.size()) {
                return fail("unterminated object");
            }
            if (text_[pos_] == ',') {
                ++pos_;
                continue;
            }
            if (text_[pos_] == '}') {
                ++pos_;
                return true;
            }
            return fail("expected , or }");
        }
    }
};

// ---------------------------------------------------------------------------
// JSON 写出
// ---------------------------------------------------------------------------

void write_json_string(std::ostream& out, const std::string& value) {
    out << '"';
    for (const char raw : value) {
        const unsigned char c = static_cast<unsigned char>(raw);
        switch (c) {
            case '"': out << "\\\""; break;
            case '\\': out << "\\\\"; break;
            case '\n': out << "\\n"; break;
            case '\r': out << "\\r"; break;
            case '\t': out << "\\t"; break;
            case '\b': out << "\\b"; break;
            case '\f': out << "\\f"; break;
            default:
                if (c < 0x20) {
                    char buffer[8] = {0};
                    std::snprintf(buffer, sizeof(buffer), "\\u%04x",
                                  static_cast<unsigned>(c));
                    out << buffer;
                } else {
                    out << static_cast<char>(c);
                }
                break;
        }
    }
    out << '"';
}

std::string build_state_json(const SystemContext& context) {
    std::ostringstream out;
    out << "{\n";
    out << "  \"schema_version\": " << kSchemaVersion << ",\n";

    const RegistrationRule rule = context.rule();
    out << "  \"registration_rule\": {\n";
    out << "    \"registration_open\": "
        << (rule.registration_open ? "true" : "false") << ",\n";
    out << "    \"maximum_credits\": " << rule.maximum_credits << ",\n";
    out << "    \"opens_at\": ";
    write_json_string(out, rule.opens_at);
    out << ",\n";
    out << "    \"closes_at\": ";
    write_json_string(out, rule.closes_at);
    out << "\n  },\n";

    out << "  \"round_opened\": " << (context.round_opened() ? "true" : "false")
        << ",\n";

    const std::vector<Course> courses = context.courses();
    out << "  \"courses\": [";
    if (!courses.empty()) {
        out << "\n";
    }
    for (size_t i = 0; i < courses.size(); ++i) {
        out << "    {\"course_id\": " << courses[i].course_id
            << ", \"course_code\": ";
        write_json_string(out, courses[i].course_code);
        out << ", \"course_name\": ";
        write_json_string(out, courses[i].course_name);
        out << ", \"credits\": " << courses[i].credits << "}";
        if (i + 1 < courses.size()) {
            out << ",";
        }
        out << "\n";
    }
    out << "  ],\n";

    const std::vector<TeachingClass> classes = context.teaching_classes();
    out << "  \"teaching_classes\": [";
    if (!classes.empty()) {
        out << "\n";
    }
    for (size_t i = 0; i < classes.size(); ++i) {
        const TeachingClass& item = classes[i];
        out << "    {\"teaching_class_id\": " << item.teaching_class_id
            << ", \"course_id\": " << item.course_id
            << ", \"teacher_id\": " << item.teacher_id
            << ", \"capacity\": " << item.capacity << ", \"meetings\": [";
        for (size_t m = 0; m < item.meetings.size(); ++m) {
            const ClassMeeting& meeting = item.meetings[m];
            if (m > 0) {
                out << ", ";
            }
            out << "{\"day_of_week\": " << meeting.day_of_week
                << ", \"start_minute\": " << meeting.start_minute
                << ", \"end_minute\": " << meeting.end_minute
                << ", \"room\": ";
            write_json_string(out, meeting.room);
            out << "}";
        }
        out << "]}";
        if (i + 1 < classes.size()) {
            out << ",";
        }
        out << "\n";
    }
    out << "  ],\n";

    const std::vector<Student>& students = context.students();
    out << "  \"students\": [";
    if (!students.empty()) {
        out << "\n";
    }
    for (size_t i = 0; i < students.size(); ++i) {
        const Student& item = students[i];
        out << "    {\"student_id\": " << item.student_id
            << ", \"student_number\": ";
        write_json_string(out, item.student_number);
        out << ", \"name\": ";
        write_json_string(out, item.name);
        out << ", \"major\": ";
        write_json_string(out, item.major);
        out << ", \"account_active\": "
            << (item.account_active ? "true" : "false") << "}";
        if (i + 1 < students.size()) {
            out << ",";
        }
        out << "\n";
    }
    out << "  ],\n";

    const std::vector<Teacher>& teachers = context.teachers();
    out << "  \"teachers\": [";
    if (!teachers.empty()) {
        out << "\n";
    }
    for (size_t i = 0; i < teachers.size(); ++i) {
        const Teacher& item = teachers[i];
        out << "    {\"teacher_id\": " << item.teacher_id
            << ", \"teacher_number\": ";
        write_json_string(out, item.teacher_number);
        out << ", \"name\": ";
        write_json_string(out, item.name);
        out << ", \"department\": ";
        write_json_string(out, item.department);
        out << ", \"account_active\": "
            << (item.account_active ? "true" : "false") << "}";
        if (i + 1 < teachers.size()) {
            out << ",";
        }
        out << "\n";
    }
    out << "  ],\n";

    const std::vector<Administrator>& administrators = context.administrators();
    out << "  \"administrators\": [";
    if (!administrators.empty()) {
        out << "\n";
    }
    for (size_t i = 0; i < administrators.size(); ++i) {
        const Administrator& item = administrators[i];
        out << "    {\"administrator_id\": " << item.administrator_id
            << ", \"account_number\": ";
        write_json_string(out, item.account_number);
        out << ", \"name\": ";
        write_json_string(out, item.name);
        out << ", \"account_active\": "
            << (item.account_active ? "true" : "false") << "}";
        if (i + 1 < administrators.size()) {
            out << ",";
        }
        out << "\n";
    }
    out << "  ],\n";

    const std::vector<Enrollment> enrollments = context.all_enrollments();
    out << "  \"enrollments\": [";
    if (!enrollments.empty()) {
        out << "\n";
    }
    for (size_t i = 0; i < enrollments.size(); ++i) {
        const Enrollment& item = enrollments[i];
        out << "    {\"student_id\": " << item.student_id
            << ", \"teaching_class_id\": " << item.teaching_class_id
            << ", \"status\": ";
        write_json_string(
            out, item.status == EnrollmentStatus::Active ? "active" : "dropped");
        out << "}";
        if (i + 1 < enrollments.size()) {
            out << ",";
        }
        out << "\n";
    }
    out << "  ]\n";
    out << "}\n";
    return out.str();
}

// ---------------------------------------------------------------------------
// 字段读取与校验
// ---------------------------------------------------------------------------

bool as_int(const JsonValue* value, int& out, const std::string& field,
            std::string& error) {
    if (value == nullptr) {
        error = "missing field " + field;
        return false;
    }
    if (value->type != JsonValue::Type::Int) {
        error = "field " + field + " must be an integer";
        return false;
    }
    out = static_cast<int>(value->integer);
    return true;
}

bool as_string(const JsonValue* value, std::string& out,
               const std::string& field, std::string& error) {
    if (value == nullptr) {
        error = "missing field " + field;
        return false;
    }
    if (value->type != JsonValue::Type::String) {
        error = "field " + field + " must be a string";
        return false;
    }
    out = value->text;
    return true;
}

bool as_bool(const JsonValue* value, bool& out, const std::string& field,
             std::string& error) {
    if (value == nullptr) {
        error = "missing field " + field;
        return false;
    }
    if (value->type != JsonValue::Type::Bool) {
        error = "field " + field + " must be a boolean";
        return false;
    }
    out = value->boolean;
    return true;
}

std::string lower_copy(const std::string& value) {
    std::string out = value;
    for (char& c : out) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

bool contains_key(const std::vector<std::string>& keys, const std::string& key) {
    for (const std::string& item : keys) {
        if (item == key) {
            return true;
        }
    }
    return false;
}

// 把根节点解析成 SystemContext；只在全部校验通过后才写 out。
bool build_context_from_json(const JsonValue& root, SystemContext& out,
                             std::string& error) {
    if (root.type != JsonValue::Type::Object) {
        error = "state root must be an object";
        return false;
    }

    const JsonValue* version = root.find("schema_version");
    if (version == nullptr || version->type != JsonValue::Type::Int) {
        error = "missing schema_version";
        return false;
    }
    if (version->integer != kSchemaVersion) {
        error = "unsupported schema_version " +
                std::to_string(version->integer);
        return false;
    }

    // ---- 规则 ----
    const JsonValue* rule_value = root.find("registration_rule");
    if (rule_value == nullptr || rule_value->type != JsonValue::Type::Object) {
        error = "missing registration_rule";
        return false;
    }
    RegistrationRule rule{false, 0, "", ""};
    if (!as_bool(rule_value->find("registration_open"), rule.registration_open,
                 "registration_open", error)) {
        return false;
    }
    if (!as_int(rule_value->find("maximum_credits"), rule.maximum_credits,
                "maximum_credits", error)) {
        return false;
    }
    if (!as_string(rule_value->find("opens_at"), rule.opens_at, "opens_at",
                   error)) {
        return false;
    }
    if (!as_string(rule_value->find("closes_at"), rule.closes_at, "closes_at",
                   error)) {
        return false;
    }

    bool round_opened = false;
    if (!as_bool(root.find("round_opened"), round_opened, "round_opened",
                 error)) {
        return false;
    }

    // ---- 课程 ----
    std::vector<Course> courses;
    const JsonValue* courses_value = root.find("courses");
    if (courses_value == nullptr ||
        courses_value->type != JsonValue::Type::Array) {
        error = "missing courses";
        return false;
    }
    for (const JsonValue& item : courses_value->items) {
        if (item.type != JsonValue::Type::Object) {
            error = "course entry must be an object";
            return false;
        }
        Course course{};
        if (!as_int(item.find("course_id"), course.course_id, "course_id",
                    error) ||
            !as_string(item.find("course_code"), course.course_code,
                       "course_code", error) ||
            !as_string(item.find("course_name"), course.course_name,
                       "course_name", error) ||
            !as_int(item.find("credits"), course.credits, "credits", error)) {
            return false;
        }
        courses.push_back(std::move(course));
    }

    // ---- 教学班 ----
    std::vector<TeachingClass> classes;
    const JsonValue* classes_value = root.find("teaching_classes");
    if (classes_value == nullptr ||
        classes_value->type != JsonValue::Type::Array) {
        error = "missing teaching_classes";
        return false;
    }
    for (const JsonValue& item : classes_value->items) {
        if (item.type != JsonValue::Type::Object) {
            error = "teaching_class entry must be an object";
            return false;
        }
        TeachingClass entry{};
        if (!as_int(item.find("teaching_class_id"), entry.teaching_class_id,
                    "teaching_class_id", error) ||
            !as_int(item.find("course_id"), entry.course_id, "course_id",
                    error) ||
            !as_int(item.find("teacher_id"), entry.teacher_id, "teacher_id",
                    error) ||
            !as_int(item.find("capacity"), entry.capacity, "capacity",
                    error)) {
            return false;
        }
        const JsonValue* meetings = item.find("meetings");
        if (meetings != nullptr && meetings->type == JsonValue::Type::Array) {
            for (const JsonValue& meeting_value : meetings->items) {
                if (meeting_value.type != JsonValue::Type::Object) {
                    error = "meeting entry must be an object";
                    return false;
                }
                ClassMeeting meeting{};
                if (!as_int(meeting_value.find("day_of_week"),
                            meeting.day_of_week, "day_of_week", error) ||
                    !as_int(meeting_value.find("start_minute"),
                            meeting.start_minute, "start_minute", error) ||
                    !as_int(meeting_value.find("end_minute"),
                            meeting.end_minute, "end_minute", error) ||
                    !as_string(meeting_value.find("room"), meeting.room, "room",
                               error)) {
                    return false;
                }
                if (meeting.day_of_week < 1 || meeting.day_of_week > 7 ||
                    meeting.start_minute < 0 ||
                    meeting.end_minute > 24 * 60 ||
                    meeting.start_minute >= meeting.end_minute) {
                    error = "invalid meeting time in teaching class " +
                            std::to_string(entry.teaching_class_id);
                    return false;
                }
                entry.meetings.push_back(std::move(meeting));
            }
        }
        if (entry.capacity < 0) {
            error = "negative capacity in teaching class " +
                    std::to_string(entry.teaching_class_id);
            return false;
        }
        classes.push_back(std::move(entry));
    }

    // ---- 三类人员 ----
    std::vector<Student> students;
    const JsonValue* students_value = root.find("students");
    if (students_value == nullptr ||
        students_value->type != JsonValue::Type::Array) {
        error = "missing students";
        return false;
    }
    for (const JsonValue& item : students_value->items) {
        Student student{};
        if (!as_int(item.find("student_id"), student.student_id,
                    "student_id", error) ||
            !as_string(item.find("student_number"), student.student_number,
                       "student_number", error) ||
            !as_string(item.find("name"), student.name, "name", error) ||
            !as_string(item.find("major"), student.major, "major", error) ||
            !as_bool(item.find("account_active"), student.account_active,
                     "account_active", error)) {
            return false;
        }
        students.push_back(std::move(student));
    }

    std::vector<Teacher> teachers;
    const JsonValue* teachers_value = root.find("teachers");
    if (teachers_value == nullptr ||
        teachers_value->type != JsonValue::Type::Array) {
        error = "missing teachers";
        return false;
    }
    for (const JsonValue& item : teachers_value->items) {
        Teacher teacher{};
        if (!as_int(item.find("teacher_id"), teacher.teacher_id,
                    "teacher_id", error) ||
            !as_string(item.find("teacher_number"), teacher.teacher_number,
                       "teacher_number", error) ||
            !as_string(item.find("name"), teacher.name, "name", error) ||
            !as_string(item.find("department"), teacher.department,
                       "department", error) ||
            !as_bool(item.find("account_active"), teacher.account_active,
                     "account_active", error)) {
            return false;
        }
        teachers.push_back(std::move(teacher));
    }

    std::vector<Administrator> administrators;
    const JsonValue* admins_value = root.find("administrators");
    if (admins_value == nullptr ||
        admins_value->type != JsonValue::Type::Array) {
        error = "missing administrators";
        return false;
    }
    for (const JsonValue& item : admins_value->items) {
        Administrator admin{};
        if (!as_int(item.find("administrator_id"), admin.administrator_id,
                    "administrator_id", error) ||
            !as_string(item.find("account_number"), admin.account_number,
                       "account_number", error) ||
            !as_string(item.find("name"), admin.name, "name", error) ||
            !as_bool(item.find("account_active"), admin.account_active,
                     "account_active", error)) {
            return false;
        }
        administrators.push_back(std::move(admin));
    }

    // ---- 选退课记录 ----
    std::vector<Enrollment> enrollments;
    const JsonValue* enrollments_value = root.find("enrollments");
    if (enrollments_value == nullptr ||
        enrollments_value->type != JsonValue::Type::Array) {
        error = "missing enrollments";
        return false;
    }
    for (const JsonValue& item : enrollments_value->items) {
        Enrollment enrollment{};
        std::string status;
        if (!as_int(item.find("student_id"), enrollment.student_id,
                    "student_id", error) ||
            !as_int(item.find("teaching_class_id"),
                    enrollment.teaching_class_id, "teaching_class_id", error) ||
            !as_string(item.find("status"), status, "status", error)) {
            return false;
        }
        if (status == "active") {
            enrollment.status = EnrollmentStatus::Active;
        } else if (status == "dropped") {
            enrollment.status = EnrollmentStatus::Dropped;
        } else {
            error = "enrollment status must be active or dropped";
            return false;
        }
        enrollments.push_back(std::move(enrollment));
    }

    // ---- 唯一性 ----
    std::vector<std::string> course_ids;
    for (const Course& course : courses) {
        const std::string key = std::to_string(course.course_id);
        if (contains_key(course_ids, key)) {
            error = "duplicate course_id " + key;
            return false;
        }
        course_ids.push_back(key);
    }
    std::vector<std::string> class_ids;
    for (const TeachingClass& item : classes) {
        const std::string key = std::to_string(item.teaching_class_id);
        if (contains_key(class_ids, key)) {
            error = "duplicate teaching_class_id " + key;
            return false;
        }
        class_ids.push_back(key);
    }
    std::vector<std::string> student_ids;
    std::vector<std::string> student_numbers;
    for (const Student& item : students) {
        const std::string id = std::to_string(item.student_id);
        const std::string number = lower_copy(item.student_number);
        if (contains_key(student_ids, id)) {
            error = "duplicate student_id " + id;
            return false;
        }
        if (contains_key(student_numbers, number)) {
            error = "duplicate student_number " + item.student_number;
            return false;
        }
        student_ids.push_back(id);
        student_numbers.push_back(number);
    }
    std::vector<std::string> teacher_ids;
    std::vector<std::string> teacher_numbers;
    for (const Teacher& item : teachers) {
        const std::string id = std::to_string(item.teacher_id);
        const std::string number = lower_copy(item.teacher_number);
        if (contains_key(teacher_ids, id)) {
            error = "duplicate teacher_id " + id;
            return false;
        }
        if (contains_key(teacher_numbers, number)) {
            error = "duplicate teacher_number " + item.teacher_number;
            return false;
        }
        teacher_ids.push_back(id);
        teacher_numbers.push_back(number);
    }
    std::vector<std::string> admin_ids;
    std::vector<std::string> admin_numbers;
    for (const Administrator& item : administrators) {
        const std::string id = std::to_string(item.administrator_id);
        const std::string number = lower_copy(item.account_number);
        if (contains_key(admin_ids, id)) {
            error = "duplicate administrator_id " + id;
            return false;
        }
        if (contains_key(admin_numbers, number)) {
            error = "duplicate account_number " + item.account_number;
            return false;
        }
        admin_ids.push_back(id);
        admin_numbers.push_back(number);
    }

    // ---- 引用完整性 ----
    for (const TeachingClass& item : classes) {
        if (!contains_key(course_ids, std::to_string(item.course_id))) {
            error = "teaching class " +
                    std::to_string(item.teaching_class_id) +
                    " refers to unknown course " +
                    std::to_string(item.course_id);
            return false;
        }
        if (!contains_key(teacher_ids, std::to_string(item.teacher_id))) {
            error = "teaching class " +
                    std::to_string(item.teaching_class_id) +
                    " refers to unknown teacher " +
                    std::to_string(item.teacher_id);
            return false;
        }
    }
    for (const Enrollment& item : enrollments) {
        if (!contains_key(student_ids, std::to_string(item.student_id))) {
            error = "enrollment refers to unknown student " +
                    std::to_string(item.student_id);
            return false;
        }
        if (!contains_key(class_ids, std::to_string(item.teaching_class_id))) {
            error = "enrollment refers to unknown teaching class " +
                    std::to_string(item.teaching_class_id);
            return false;
        }
    }

    // 全部通过后才构造并替换。
    SystemContext parsed(students, teachers, administrators, courses, classes,
                         rule);
    parsed.registration().restore_enrollments(std::move(enrollments));
    parsed.restore_round_opened(round_opened);
    out = std::move(parsed);
    return true;
}

// ---------------------------------------------------------------------------
// 文件操作
// ---------------------------------------------------------------------------

std::string parent_directory(const std::string& path) {
    return std::filesystem::path(path).parent_path().string();
}

bool ensure_directory(const std::string& path, bool create_dirs,
                      std::string& error) {
    if (!create_dirs) {
        return true;
    }
    const std::string parent = parent_directory(path);
    if (parent.empty()) {
        return true;
    }
    std::error_code ec;
    if (std::filesystem::exists(parent, ec)) {
        return true;
    }
    std::filesystem::create_directories(parent, ec);
    if (ec) {
        error = "cannot create directory " + parent + ": " + ec.message();
        return false;
    }
    return true;
}

bool read_file(const std::string& path, std::string& content,
               std::string& error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        error = "cannot open " + path + " for reading";
        return false;
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    content = buffer.str();
    return true;
}

bool write_file_atomic(const std::string& path, const std::string& content,
                       std::string& error) {
    const std::string tmp = path + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) {
            error = "cannot open " + tmp + " for writing";
            return false;
        }
        out.write(content.data(), static_cast<std::streamsize>(content.size()));
        out.flush();
        if (!out.good()) {
            error = "write failed for " + tmp;
            out.close();
            std::error_code ignored;
            std::filesystem::remove(tmp, ignored);
            return false;
        }
    }

    // 备份上一版；备份失败就不继续，保证原文件还在。
    // 先删旧备份再复制：MinGW 的 copy_file(overwrite_existing) 在目标已存在时
    // 会报 "File exists"，这里用 remove + copy 保证跨平台一致。
    std::error_code ec;
    if (std::filesystem::exists(path, ec)) {
        const std::string backup = path + ".bak";
        std::error_code remove_ec;
        std::filesystem::remove(backup, remove_ec);
        std::error_code copy_ec;
        std::filesystem::copy_file(path, backup, copy_ec);
        if (copy_ec) {
            error = "cannot create backup " + backup + ": " + copy_ec.message();
            std::error_code ignored;
            std::filesystem::remove(tmp, ignored);
            return false;
        }
    }

#ifdef _WIN32
    if (!MoveFileExA(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        error = "cannot replace " + path;
        std::error_code ignored;
        std::filesystem::remove(tmp, ignored);
        return false;
    }
#else
    std::filesystem::rename(tmp, path, ec);
    if (ec) {
        error = "cannot replace " + path + ": " + ec.message();
        std::error_code ignored;
        std::filesystem::remove(tmp, ignored);
        return false;
    }
#endif
    return true;
}

}  // namespace

// ---------------------------------------------------------------------------
// 公开接口
// ---------------------------------------------------------------------------

SystemContext make_seed_context() {
    // 与 documentation/interfaces.md 4.1 的演示数据一致。
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
    return SystemContext(students, teachers, administrators, courses,
                         teaching_classes, rule);
}

OperationResult initialize_storage(const StorageOptions& options,
                                   SystemContext& out_context) {
    std::error_code ec;
    if (std::filesystem::exists(options.path, ec)) {
        return load_state(options, out_context);
    }
    out_context = make_seed_context();
    const OperationResult saved = save_state(out_context, options);
    if (saved.code != ResultCode::Success) {
        return saved;
    }
    return {ResultCode::Success,
            "initialized " + options.path + " with seed data"};
}

OperationResult load_state(const StorageOptions& options,
                           SystemContext& out_context) {
    std::string content;
    std::string error;
    if (!read_file(options.path, content, error)) {
        return {ResultCode::StorageIoError, error};
    }

    JsonValue root;
    JsonParser parser(content);
    if (!parser.parse(root, error)) {
        return {ResultCode::StorageParseError, error};
    }

    if (!build_context_from_json(root, out_context, error)) {
        // schema 版本单独归类，其余都按数据一致性问题返回。
        const bool schema_error = error.find("schema_version") != std::string::npos;
        return {schema_error ? ResultCode::StorageSchemaMismatch
                             : ResultCode::StorageIntegrityError,
                error};
    }
    return {ResultCode::Success, "loaded " + options.path};
}

OperationResult save_state(const SystemContext& context,
                           const StorageOptions& options) {
    std::string error;
    if (!ensure_directory(options.path, options.create_dirs, error)) {
        return {ResultCode::StorageIoError, error};
    }
    const std::string content = build_state_json(context);
    if (!write_file_atomic(options.path, content, error)) {
        return {ResultCode::StorageIoError, error};
    }
    return {ResultCode::Success, "saved " + options.path};
}

OperationResult restore_state(const StorageOptions& options,
                              SystemContext& out_context) {
    const std::string backup = options.path + ".bak";
    std::error_code ec;
    if (!std::filesystem::exists(backup, ec)) {
        return {ResultCode::StorageBackupMissing, "no backup at " + backup};
    }

    StorageOptions backup_options = options;
    backup_options.path = backup;

    SystemContext restored = make_seed_context();
    const OperationResult loaded = load_state(backup_options, restored);
    if (loaded.code != ResultCode::Success) {
        return loaded;
    }

    const OperationResult saved = save_state(restored, options);
    if (saved.code != ResultCode::Success) {
        return saved;
    }
    out_context = std::move(restored);
    return {ResultCode::Success, "restored from " + backup};
}

}  // namespace course_registration
