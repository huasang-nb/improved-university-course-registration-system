// ============================================================================
// 本地验证用的最小 HTTP 服务（只给教师/管理员接口与页面联调用）
//
// 这不是交付给组长集成的产品入口：真正的登录、会话、CSRF 与公共骨架由
// Yuhua 负责。本文件只做一件事——把已经写好的路由适配层挂到一个真实的
// httplib::Server 上，方便在浏览器里点页面、用 curl 验证接口，而不必等
// 公共骨架落地。
//
// 安全性：仅监听 127.0.0.1；会话身份来自命令行参数（--role/--id），
// 不做密码校验，csrf_ok 恒为 true。**不要**把它当作可部署的服务。
//
// 用法：
//   http_teacher_admin_dev_server --role admin --id 900
//   http_teacher_admin_dev_server --role teacher --id 100 --port 8081
// 打开 http://127.0.0.1:8080/ 会自动跳到对应角色的页面。
//
// 路径说明：仓库可能位于含中文的目录（如“工作文件夹”），而 cpp-httplib 用
// 窄字符 API 读文件。这里用 CommandLineToArgvW 取 UTF-16 参数，并在无法用
// 窄路径打开时退回到 8.3 短路径，避免“静态文件 404”。
// ============================================================================

#include <cstdlib>
#include <iostream>
#include <mutex>
#include <string>
#include <vector>

#ifdef _WIN32
// 必须先排除 winsock.h，否则会和 cpp-httplib 引入的 winsock2.h 冲突。
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#endif

#include "httplib.h"
#include "http_support.hpp"
#include "http_teacher_admin_routes.hpp"
#include "persistence_service.hpp"
#include "persistent_administration_service.hpp"
#include "teacher_and_administration_services.hpp"

namespace {

struct Options {
    std::string host = "127.0.0.1";
    int port = 8080;
    std::string state = "data/http-dev-state.json";
    std::string web_root = "web";
    std::string role = "admin";
    int id = 900;
};

bool parse_role(const std::string& text, course_registration::Role& role) {
    if (text == "admin" || text == "administrator") {
        role = course_registration::Role::Administrator;
        return true;
    }
    if (text == "teacher") {
        role = course_registration::Role::Teacher;
        return true;
    }
    if (text == "student") {
        role = course_registration::Role::Student;
        return true;
    }
    return false;
}

std::string role_name(course_registration::Role role) {
    switch (role) {
        case course_registration::Role::Student:
            return "student";
        case course_registration::Role::Teacher:
            return "teacher";
        case course_registration::Role::Administrator:
            return "administrator";
    }
    return "unknown";
}

bool is_ascii(const std::string& text) {
    for (unsigned char ch : text) {
        if (ch >= 0x80) {
            return false;
        }
    }
    return true;
}

#ifdef _WIN32

std::wstring utf8_to_wide(const std::string& text) {
    if (text.empty()) {
        return std::wstring();
    }
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(),
                                         static_cast<int>(text.size()), nullptr, 0);
    std::wstring wide(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(),
                        size);
    return wide;
}

std::string wide_to_utf8(const std::wstring& text) {
    if (text.empty()) {
        return std::string();
    }
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(),
                                         static_cast<int>(text.size()), nullptr, 0, nullptr,
                                         nullptr);
    std::string utf8(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), utf8.data(), size,
                        nullptr, nullptr);
    return utf8;
}

std::vector<std::string> utf8_arguments() {
    int count = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &count);
    if (argv == nullptr) {
        return {};
    }
    std::vector<std::string> result;
    for (int i = 0; i < count; ++i) {
        result.push_back(wide_to_utf8(argv[i]));
    }
    LocalFree(argv);
    return result;
}

// cpp-httplib 的静态文件读取走窄路径。目录名含中文时先换成 8.3 短路径，
// 短路径是纯 ASCII，窄 API 才能打开。短路径拿不到时原样返回并提示。
std::string narrow_path(const std::string& utf8, const char* what) {
    if (is_ascii(utf8)) {
        return utf8;
    }
    const std::wstring wide = utf8_to_wide(utf8);
    wchar_t buffer[MAX_PATH] = {};
    const DWORD size = GetShortPathNameW(wide.c_str(), buffer, MAX_PATH);
    if (size == 0 || size >= MAX_PATH) {
        std::cerr << "warning: cannot shorten " << what << " " << utf8
                  << "; narrow file APIs may fail on this path\n";
        return utf8;
    }
    return wide_to_utf8(std::wstring(buffer, size));
}

#endif

}  // namespace

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    using namespace course_registration;
    using namespace course_registration::http;

    std::vector<std::string> args;
#ifdef _WIN32
    args = utf8_arguments();
#endif
    if (args.empty()) {
        args.push_back("http_teacher_admin_dev_server");
    }

    Options options;
    for (std::size_t i = 1; i < args.size(); ++i) {
        const std::string& arg = args[i];
        const bool has_value = i + 1 < args.size();
        if (arg == "--host" && has_value) {
            options.host = args[++i];
        } else if (arg == "--port" && has_value) {
            options.port = std::atoi(args[++i].c_str());
        } else if (arg == "--state" && has_value) {
            options.state = args[++i];
        } else if (arg == "--web-root" && has_value) {
            options.web_root = args[++i];
        } else if (arg == "--role" && has_value) {
            options.role = args[++i];
        } else if (arg == "--id" && has_value) {
            options.id = std::atoi(args[++i].c_str());
        } else if (arg == "--help") {
            std::cout << "usage: http_teacher_admin_dev_server [--host 127.0.0.1] [--port 8080]\n"
                         "       [--state data/http-dev-state.json] [--web-root web]\n"
                         "       [--role admin|teacher|student] [--id 900]\n";
            return 0;
        } else {
            std::cerr << "unknown argument: " << arg << "\n";
            return 2;
        }
    }

    Role role = Role::Administrator;
    if (!parse_role(options.role, role)) {
        std::cerr << "unknown role: " << options.role << "\n";
        return 2;
    }

    // SystemContext 没有默认构造函数：先用种子数据建好，再按持久化文件覆盖。
    SystemContext context = make_seed_context();
    StorageOptions storage;
    storage.path = options.state;
    storage.create_dirs = true;
    const OperationResult loaded = initialize_storage(storage, context);
    if (loaded.code != ResultCode::Success) {
        std::cerr << "cannot open state " << options.state << ": " << loaded.message << "\n";
        return 1;
    }

    UserIdentity identity{};
    bool found = false;
    if (role == Role::Administrator) {
        for (const Administrator& item : context.administrators()) {
            if (item.administrator_id == options.id) {
                identity = UserIdentity{item.administrator_id, item.account_number, role};
                found = true;
            }
        }
    } else if (role == Role::Teacher) {
        for (const Teacher& item : context.teachers()) {
            if (item.teacher_id == options.id) {
                identity = UserIdentity{item.teacher_id, item.teacher_number, role};
                found = true;
            }
        }
    } else {
        for (const Student& item : context.students()) {
            if (item.student_id == options.id) {
                identity = UserIdentity{item.student_id, item.student_number, role};
                found = true;
            }
        }
    }
    if (!found) {
        std::cerr << "no " << options.role << " with id " << options.id << " in " << options.state
                  << "\n";
        return 1;
    }

    PersistentAdministratorService admin(context, storage);
    TeacherService teacher(context);
    std::mutex business_mutex;
    Services services{context, admin, teacher, business_mutex};

    // 本地验证：所有请求都以命令行指定的身份通过，CSRF 视为已校验。
    const SessionLookup lookup = [identity](const httplib::Request&) {
        SessionView view;
        view.identity = identity;
        view.csrf_ok = true;
        return view;
    };

    httplib::Server server;
    register_teacher_admin_routes(server, services, lookup);

    server.Get("/", [role](const httplib::Request&, httplib::Response& response) {
        response.set_redirect(role == Role::Administrator ? "/admin.html" : "/teacher.html");
    });

    std::string web_root = options.web_root;
#ifdef _WIN32
    web_root = narrow_path(web_root, "web root");
#endif
    if (!server.set_mount_point("/", web_root)) {
        std::cerr << "warning: cannot serve static files from " << web_root << "\n";
    }

    std::cout << "dev server: http://" << options.host << ":" << options.port << "/ as "
              << options.role << " " << options.id << " (" << role_name(role) << ")\n";
    std::cout << "state=" << options.state << ", web=" << web_root << "\n";
    std::cout << "press Ctrl+C to stop\n";
    if (!server.listen(options.host.c_str(), options.port)) {
        std::cerr << "cannot listen on " << options.host << ":" << options.port << "\n";
        return 1;
    }
    return 0;
}
