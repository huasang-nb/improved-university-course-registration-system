// ============================================================================
// 团队公共 Web 入口（教师 / 管理员页面与接口）
//
// 这是给组长整合时用的“同一个 server”示例，不是第二个登录入口：
//   1. 只创建一个 SessionRuntime，由它注册 /api/auth/*（登录、me、退出、改密）；
//   2. 教师 / 管理员路由通过 register_teacher_admin_routes_with_session() 注册，
//      身份、CSRF 与业务锁都取自公共会话层，本模块不再自己解析 Cookie；
//   3. 静态页面与接口同源，页面里的 fetch 直接带 Cookie 和 X-CSRF-Token。
//
// 启动前必须已有状态文件（先跑种子数据与 password_setup 设置初始密码）。
// 文件不存在时直接退出，不静默生成新文件覆盖真实数据。
//
// 用法：
//   http_team4_web_server --state .\build\demo\state.json --port 8080
//
// 路径说明：仓库可能位于含中文的目录，而 cpp-httplib 用窄字符 API 读文件，
// 所以挂载静态目录前会尝试换成 8.3 短路径（纯 ASCII）。
// ============================================================================

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#ifdef _WIN32
// 必须先排除 winsock.h，否则会和 cpp-httplib 引入的 winsock2.h 冲突。
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#endif

#include "httplib.h"
#include "http_teacher_admin_routes.hpp"
#include "persistence_service.hpp"
#include "session.hpp"

namespace {

struct Options {
    std::string host = "127.0.0.1";
    int port = 8080;
    std::string state;
    std::string web_root = "web";
};

#ifdef _WIN32
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

std::wstring utf8_to_wide(const std::string& text) {
    if (text.empty()) {
        return std::wstring();
    }
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                                         nullptr, 0);
    std::wstring wide(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), size);
    return wide;
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

bool is_ascii(const std::string& text) {
    for (const char ch : text) {
        if (static_cast<unsigned char>(ch) >= 0x80) {
            return false;
        }
    }
    return true;
}

// cpp-httplib 只接受窄路径；中文目录换成短路径后才能被窄 API 打开。
std::string narrow_path(const std::string& utf8) {
    if (is_ascii(utf8)) {
        return utf8;
    }
    const std::wstring wide = utf8_to_wide(utf8);
    wchar_t buffer[MAX_PATH] = {};
    const DWORD size = GetShortPathNameW(wide.c_str(), buffer, MAX_PATH);
    if (size == 0 || size >= MAX_PATH) {
        std::cerr << "warning: cannot shorten path " << utf8
                  << "; static files may fail to load\n";
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

    std::vector<std::string> args;
#ifdef _WIN32
    args = utf8_arguments();
#endif
    if (args.empty()) {
        args.push_back("http_team4_web_server");
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
        } else if (arg == "--help") {
            std::cout << "usage: http_team4_web_server --state <state.json> [--host 127.0.0.1]\n"
                         "       [--port 8080] [--web-root web]\n"
                         "  state.json must already exist; create it with the seed data first.\n";
            return 0;
        } else {
            std::cerr << "unknown argument: " << arg << "\n";
            return 2;
        }
    }
    if (options.state.empty()) {
        std::cerr << "--state is required: this server never creates a new state file\n";
        return 2;
    }

    StorageOptions storage;
    storage.path = options.state;
    storage.create_dirs = false;

    // SystemContext 没有默认构造函数；加载失败说明文件缺失或损坏，
    // 这时直接退出，避免用空数据把真实状态覆盖掉。
    SystemContext context = make_seed_context();
    const OperationResult loaded = load_state(storage, context);
    if (loaded.code != ResultCode::Success) {
        std::cerr << "cannot load state " << options.state << ": " << loaded.message << "\n"
                  << "use the seed / password_setup tools to create it first\n";
        return 1;
    }

    SessionOptions session_options;
    session_options.allowed_origin =
        "http://" + options.host + ":" + std::to_string(options.port);

    // 只建立一个运行对象：会话表、业务锁、CSRF 全都由它统一管理。
    // runtime 必须比 server 活得久，所以先构造、后 listen。
    SessionRuntime runtime(context, storage, session_options);
    bind_session_runtime(runtime);

    httplib::Server server;
    runtime.register_auth_routes(server);
    http::register_teacher_admin_routes_with_session(server);

    server.Get("/", [](const httplib::Request&, httplib::Response& response) {
        response.set_redirect("/login.html");
    });

    std::string web_root = options.web_root;
#ifdef _WIN32
    web_root = narrow_path(web_root);
#endif
    if (!server.set_mount_point("/", web_root)) {
        std::cerr << "cannot mount web root " << options.web_root << "\n";
        return 1;
    }

    std::cout << "teacher/admin web server on http://" << options.host << ":" << options.port
              << " (state file " << options.state << ")\n";
    if (!server.listen(options.host, options.port)) {
        std::cerr << "cannot listen on " << options.host << ":" << options.port << "\n";
        return 1;
    }
    return 0;
}
