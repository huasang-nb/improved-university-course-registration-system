// ============================================================================
// 持久化实现（Zhang Yikai）
//
// 设计：一个 state.json 快照 + .tmp / .bak 两份辅助文件。
//   - 保存：observe_registration_round() -> validate_state() -> 序列化 ->
//     写 .tmp -> （主文件仍有效时）备份为 .bak -> 原子替换。
//   - 恢复：读 .bak -> 解析 -> 校验 -> 写 .tmp -> 原子替换主文件；**不覆盖 .bak**，
//     所以恢复失败后可以重试，始终保留一份有效数据。
//   - 失败：不碰内存、不删原文件，返回带原因的 OperationResult。
//
// 为了不引入第三方依赖，这里带一个只够本文件使用的 JSON 读写子集：
// 对象、数组、字符串、整数、布尔、null；字符串支持 \" \\ \n \r \t \b \f
// 与 \uXXXX（含 UTF-16 代理对）。
// ============================================================================

#include "persistence_service.hpp"

#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
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

// 编码一个 Unicode 码点（含 BMP 之外、需要 4 字节的码点）。
void append_utf8(std::string& out, unsigned code) {
    if (code <= 0x7F) {
        out.push_back(static_cast<char>(code));
    } else if (code <= 0x7FF) {
        out.push_back(static_cast<char>(0xC0 | (code >> 6)));
        out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
    } else if (code <= 0xFFFF) {
        out.push_back(static_cast<char>(0xE0 | (code >> 12)));
        out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (code >> 18)));
        out.push_back(static_cast<char>(0x80 | ((code >> 12) & 0x3F)));
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

    bool read_hex4(size_t offset, unsigned& code) {
        if (offset + 4 > text_.size()) {
            return false;
        }
        code = 0;
        for (int k = 0; k < 4; ++k) {
            const int digit = hex_digit(text_[offset + static_cast<size_t>(k)]);
            if (digit < 0) {
                return false;
            }
            code = code * 16u + static_cast<unsigned>(digit);
        }
        return true;
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
                ++pos_;  // 越过反斜杠，指向转义字符
                if (pos_ >= text_.size()) {
                    return fail("unterminated escape");
                }
                const char e = text_[pos_];
                if (e == 'u') {
                    if (!parse_unicode_escape(out)) {
                        return false;
                    }
                    continue;  // parse_unicode_escape 已经推进 pos_
                }
                switch (e) {
                    case '"': out.push_back('"'); break;
                    case '\\': out.push_back('\\'); break;
                    case '/': out.push_back('/'); break;
                    case 'b': out.push_back('\b'); break;
                    case 'f': out.push_back('\f'); break;
                    case 'n': out.push_back('\n'); break;
                    case 'r': out.push_back('\r'); break;
                    case 't': out.push_back('\t'); break;
                    default:
                        return fail("unknown escape");
                }
                ++pos_;  // 越过转义字符
            } else {
                out.push_back(c);
                ++pos_;
            }
        }
        return fail("unterminated string");
    }

    // 处理 \uXXXX；遇到高位代理时组合后面的 \uXXXX 低位代理。
    bool parse_unicode_escape(std::string& out) {
        // pos_ 指向 'u'，4 个十六进制位在其后。
        unsigned code = 0;
        if (!read_hex4(pos_ + 1, code)) {
            return fail("invalid \\u escape");
        }
        pos_ += 5;  // 越过 'u' 与 4 位十六进制

        if (code >= 0xD800u && code <= 0xDBFFu) {
            // 高位代理：后面必须是 \uDC00-\uDFFF。
            if (pos_ + 1 >= text_.size() || text_[pos_] != '\\' ||
                text_[pos_ + 1] != 'u') {
                return fail("missing low surrogate");
            }
            unsigned low = 0;
            if (!read_hex4(pos_ + 2, low)) {
                return fail("invalid low surrogate escape");
            }
            if (low < 0xDC00u || low > 0xDFFFu) {
                return fail("invalid low surrogate");
            }
            code = 0x10000u + ((code - 0xD800u) << 10) + (low - 0xDC00u);
            pos_ += 6;  // 越过 '\' 'u' 与 4 位十六进制
        } else if (code >= 0xDC00u && code <= 0xDFFFu) {
            return fail("lone low surrogate");
        }
        append_utf8(out, code);
        return true;
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
    if (value->integer < static_cast<long long>(std::numeric_limits<int>::min()) ||
        value->integer > static_cast<long long>(std::numeric_limits<int>::max())) {
        error = "field " + field + " is out of int range";
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

}  // namespace

// ---------------------------------------------------------------------------
// 业务约束校验（公开入口）
// ---------------------------------------------------------------------------

OperationResult validate_state(const SystemContext& context,
                               std::vector<std::string>& problems) {
    problems.clear();

    const std::vector<Course> courses = context.courses();
    const std::vector<TeachingClass> classes = context.teaching_classes();
    const std::vector<Student>& students = context.students();
    const std::vector<Teacher>& teachers = context.teachers();
    const std::vector<Administrator>& administrators = context.administrators();
    const std::vector<Enrollment> enrollments = context.all_enrollments();
    const RegistrationRule rule = context.rule();

    auto add = [&problems](const std::string& text) {
        problems.push_back(text);
    };

    // ---- 1. 课程 / 教学班结构 ----
    for (const Course& course : courses) {
        bool has_class = false;
        for (const TeachingClass& item : classes) {
            if (item.course_id == course.course_id) {
                has_class = true;
                break;
            }
        }
        if (!has_class) {
            add("course " + std::to_string(course.course_id) +
                " has no teaching class");
        }
    }
    for (const TeachingClass& item : classes) {
        const std::string label =
            "teaching class " + std::to_string(item.teaching_class_id);
        if (item.capacity < 0) {
            add(label + " has negative capacity");
        }
        if (item.meetings.empty()) {
            add(label + " has no meeting");
        }
        for (const ClassMeeting& meeting : item.meetings) {
            if (meeting.day_of_week < 1 || meeting.day_of_week > 7) {
                add(label + " has an invalid day_of_week");
            }
            if (meeting.start_minute < 0 ||
                meeting.end_minute > 24 * 60 ||
                meeting.start_minute >= meeting.end_minute) {
                add(label + " has an invalid meeting time");
            }
        }
    }

    // ---- 1b. 内部数字 ID：各自集合内唯一（不要求跨角色全局唯一）----
    {
        std::vector<std::string> seen_courses;
        for (const Course& item : courses) {
            const std::string key = std::to_string(item.course_id);
            if (contains_key(seen_courses, key)) {
                add("duplicate course_id " + key);
            } else {
                seen_courses.push_back(key);
            }
        }
        std::vector<std::string> seen_classes;
        for (const TeachingClass& item : classes) {
            const std::string key = std::to_string(item.teaching_class_id);
            if (contains_key(seen_classes, key)) {
                add("duplicate teaching_class_id " + key);
            } else {
                seen_classes.push_back(key);
            }
        }
        std::vector<std::string> seen_students;
        for (const Student& item : students) {
            const std::string key = std::to_string(item.student_id);
            if (contains_key(seen_students, key)) {
                add("duplicate student_id " + key);
            } else {
                seen_students.push_back(key);
            }
        }
        std::vector<std::string> seen_teachers;
        for (const Teacher& item : teachers) {
            const std::string key = std::to_string(item.teacher_id);
            if (contains_key(seen_teachers, key)) {
                add("duplicate teacher_id " + key);
            } else {
                seen_teachers.push_back(key);
            }
        }
        std::vector<std::string> seen_admins;
        for (const Administrator& item : administrators) {
            const std::string key = std::to_string(item.administrator_id);
            if (contains_key(seen_admins, key)) {
                add("duplicate administrator_id " + key);
            } else {
                seen_admins.push_back(key);
            }
        }
    }

    // ---- 2. 账号唯一性：角色内 + 跨角色（数字 ID 不要求跨角色唯一）----
    std::map<std::string, std::string> accounts;
    auto insert_account = [&](const std::string& number,
                              const std::string& role) {
        const std::string key = lower_copy(number);
        const auto found = accounts.find(key);
        if (found != accounts.end()) {
            add("duplicate account " + number + " (" + found->second + " and " +
                role + ")");
        } else {
            accounts.emplace(key, role);
        }
    };
    for (const Student& item : students) {
        insert_account(item.student_number, "student");
    }
    for (const Teacher& item : teachers) {
        insert_account(item.teacher_number, "teacher");
    }
    for (const Administrator& item : administrators) {
        insert_account(item.account_number, "administrator");
    }

    // ---- 3. 引用完整性 ----
    std::vector<std::string> course_ids;
    for (const Course& item : courses) {
        course_ids.push_back(std::to_string(item.course_id));
    }
    std::vector<std::string> class_ids;
    for (const TeachingClass& item : classes) {
        class_ids.push_back(std::to_string(item.teaching_class_id));
    }
    std::vector<std::string> student_ids;
    for (const Student& item : students) {
        student_ids.push_back(std::to_string(item.student_id));
    }
    std::vector<std::string> teacher_ids;
    for (const Teacher& item : teachers) {
        teacher_ids.push_back(std::to_string(item.teacher_id));
    }
    for (const TeachingClass& item : classes) {
        if (!contains_key(course_ids, std::to_string(item.course_id))) {
            add("teaching class " + std::to_string(item.teaching_class_id) +
                " refers to unknown course " + std::to_string(item.course_id));
        }
        if (!contains_key(teacher_ids, std::to_string(item.teacher_id))) {
            add("teaching class " + std::to_string(item.teaching_class_id) +
                " refers to unknown teacher " +
                std::to_string(item.teacher_id));
        }
    }
    for (const Enrollment& item : enrollments) {
        if (!contains_key(student_ids, std::to_string(item.student_id))) {
            add("enrollment refers to unknown student " +
                std::to_string(item.student_id));
        }
        if (!contains_key(class_ids, std::to_string(item.teaching_class_id))) {
            add("enrollment refers to unknown teaching class " +
                std::to_string(item.teaching_class_id));
        }
    }

    // ---- 4. 有效选课不超容量 ----
    for (const TeachingClass& item : classes) {
        int active = 0;
        for (const Enrollment& enrollment : enrollments) {
            if (enrollment.teaching_class_id == item.teaching_class_id &&
                enrollment.status == EnrollmentStatus::Active) {
                ++active;
            }
        }
        if (active > item.capacity) {
            add("teaching class " + std::to_string(item.teaching_class_id) +
                " has " + std::to_string(active) +
                " active enrollments but capacity " +
                std::to_string(item.capacity));
        }
    }

    // ---- 5. 同一学生同一教学班不能有多个 active ----
    for (size_t i = 0; i < enrollments.size(); ++i) {
        if (enrollments[i].status != EnrollmentStatus::Active) {
            continue;
        }
        for (size_t j = i + 1; j < enrollments.size(); ++j) {
            if (enrollments[j].status == EnrollmentStatus::Active &&
                enrollments[i].student_id == enrollments[j].student_id &&
                enrollments[i].teaching_class_id ==
                    enrollments[j].teaching_class_id) {
                add("duplicate active enrollment for student " +
                    std::to_string(enrollments[i].student_id) +
                    " in teaching class " +
                    std::to_string(enrollments[i].teaching_class_id));
            }
        }
    }

    // ---- 6. 每个学生：时间冲突 + 学分上限 ----
    auto find_class = [&classes](int id) -> const TeachingClass* {
        for (const TeachingClass& item : classes) {
            if (item.teaching_class_id == id) {
                return &item;
            }
        }
        return nullptr;
    };
    auto find_course = [&courses](int id) -> const Course* {
        for (const Course& item : courses) {
            if (item.course_id == id) {
                return &item;
            }
        }
        return nullptr;
    };

    for (const Student& student : students) {
        std::vector<const TeachingClass*> active_classes;
        for (const Enrollment& enrollment : enrollments) {
            if (enrollment.student_id != student.student_id ||
                enrollment.status != EnrollmentStatus::Active) {
                continue;
            }
            const TeachingClass* item =
                find_class(enrollment.teaching_class_id);
            if (item != nullptr) {
                active_classes.push_back(item);
            }
        }

        for (size_t i = 0; i < active_classes.size(); ++i) {
            for (size_t j = i + 1; j < active_classes.size(); ++j) {
                // 同一教学班重复出现由第 5 条负责，这里跳过。
                if (active_classes[i] == active_classes[j]) {
                    continue;
                }
                bool conflict = false;
                for (const ClassMeeting& a : active_classes[i]->meetings) {
                    for (const ClassMeeting& b : active_classes[j]->meetings) {
                        if (a.day_of_week == b.day_of_week &&
                            a.start_minute < b.end_minute &&
                            b.start_minute < a.end_minute) {
                            conflict = true;
                        }
                    }
                }
                if (conflict) {
                    add("student " + std::to_string(student.student_id) +
                        " has a time conflict between teaching classes " +
                        std::to_string(active_classes[i]->teaching_class_id) +
                        " and " +
                        std::to_string(active_classes[j]->teaching_class_id));
                }
            }
        }

        int credits = 0;
        for (const TeachingClass* item : active_classes) {
            const Course* course = find_course(item->course_id);
            if (course != nullptr) {
                credits += course->credits;
            }
        }
        if (credits > rule.maximum_credits) {
            add("student " + std::to_string(student.student_id) + " has " +
                std::to_string(credits) +
                " credits, above the limit " +
                std::to_string(rule.maximum_credits));
        }
    }

    if (!problems.empty()) {
        return {ResultCode::StorageIntegrityError,
                "state violates business rules"};
    }
    return {ResultCode::Success, "state is consistent"};
}

namespace {

// ---------------------------------------------------------------------------
// JSON -> SystemContext（结构性解析；业务约束交给 validate_state）
// ---------------------------------------------------------------------------

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
                 "registration_open", error) ||
        !as_int(rule_value->find("maximum_credits"), rule.maximum_credits,
                "maximum_credits", error) ||
        !as_string(rule_value->find("opens_at"), rule.opens_at, "opens_at",
                   error) ||
        !as_string(rule_value->find("closes_at"), rule.closes_at, "closes_at",
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
                entry.meetings.push_back(std::move(meeting));
            }
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

    // 先构造，再整体校验；全部通过才替换 out。
    SystemContext parsed(students, teachers, administrators, courses, classes,
                         rule);
    parsed.registration().restore_enrollments(std::move(enrollments));
    parsed.restore_round_opened(round_opened);

    std::vector<std::string> problems;
    if (validate_state(parsed, problems).code != ResultCode::Success) {
        error = problems.empty() ? "state violates business rules"
                                 : problems.front();
        return false;
    }

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

// 把内容写到 path：先写 .tmp；backup_existing 为真且旧主文件仍能通过校验时，
// 才把旧主文件复制成 .bak（避免用损坏内容覆盖有效备份）；最后原子替换。
bool write_tmp_and_replace(const std::string& path, const std::string& content,
                           bool backup_existing, std::string& error) {
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

    if (backup_existing) {
        std::error_code ec;
        if (std::filesystem::exists(path, ec)) {
            // 只有旧主文件仍然可解析且通过业务校验时才覆盖备份。
            std::string existing;
            std::string read_error;
            bool old_is_valid = false;
            if (read_file(path, existing, read_error)) {
                JsonValue root;
                JsonParser parser(existing);
                std::string parse_error;
                if (parser.parse(root, parse_error)) {
                    SystemContext probe = make_seed_context();
                    std::string build_error;
                    old_is_valid = build_context_from_json(root, probe, build_error);
                }
            }
            if (old_is_valid) {
                const std::string backup = path + ".bak";
                std::error_code remove_ec;
                std::filesystem::remove(backup, remove_ec);
                std::error_code copy_ec;
                std::filesystem::copy_file(path, backup, copy_ec);
                if (copy_ec) {
                    error = "cannot create backup " + backup + ": " +
                            copy_ec.message();
                    std::error_code ignored;
                    std::filesystem::remove(tmp, ignored);
                    return false;
                }
            }
            // 旧主文件已损坏：保留原有 .bak，不覆盖。
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
    std::error_code ec;
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
    // 先写盘成功，再替换 out_context：失败时调用方的状态保持不变。
    SystemContext seeded = make_seed_context();
    const OperationResult saved = save_state(seeded, options);
    if (saved.code != ResultCode::Success) {
        return saved;
    }
    out_context = std::move(seeded);
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
        const bool schema_error = error.find("schema_version") != std::string::npos;
        return {schema_error ? ResultCode::StorageSchemaMismatch
                             : ResultCode::StorageIntegrityError,
                error};
    }
    return {ResultCode::Success, "loaded " + options.path};
}

OperationResult save_state(SystemContext& context,
                           const StorageOptions& options) {
    // 保存前先观察"本轮是否已开放"，否则自动开放后保存会把 false 写回文件。
    context.observe_registration_round();

    std::vector<std::string> problems;
    if (validate_state(context, problems).code != ResultCode::Success) {
        return {ResultCode::StorageIntegrityError,
                problems.empty() ? "state violates business rules"
                                 : problems.front()};
    }

    std::string error;
    if (!ensure_directory(options.path, options.create_dirs, error)) {
        return {ResultCode::StorageIoError, error};
    }
    const std::string content = build_state_json(context);
    if (!write_tmp_and_replace(options.path, content, true, error)) {
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

    // 恢复时不再备份（backup_existing=false），避免用损坏的主文件覆盖 .bak。
    restored.observe_registration_round();
    std::string error;
    const std::string content = build_state_json(restored);
    if (!write_tmp_and_replace(options.path, content, false, error)) {
        return {ResultCode::StorageIoError, error};
    }
    out_context = std::move(restored);
    return {ResultCode::Success, "restored from " + backup};
}

}  // namespace course_registration
