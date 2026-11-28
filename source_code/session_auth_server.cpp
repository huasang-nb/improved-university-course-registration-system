#include "session.hpp"

#include <filesystem>
#include <iostream>
#include <stdexcept>

using namespace course_registration;

int main(int argc, char** argv) {
    try {
        std::string state_path;
        int port = 8080;
        for (int index = 1; index < argc; ++index) {
            const std::string argument = argv[index];
            if (argument == "--help" && argc == 2) {
                std::cout << "session_auth_server --state EXISTING_FILE [--port 8080]\n"
                             "Local authentication API only; stop password_setup before starting.\n";
                return 0;
            }
            if (argument == "--state" && state_path.empty() && index + 1 < argc) {
                state_path = argv[++index];
            } else if (argument == "--port" && index + 1 < argc) {
                const std::string value = argv[++index];
                if (value.empty() || value.find_first_not_of("0123456789") != std::string::npos) {
                    throw std::invalid_argument("port must be an integer");
                }
                port = std::stoi(value);
                if (port < 1 || port > 65535) throw std::invalid_argument("port is out of range");
            } else {
                throw std::invalid_argument("unknown or incomplete argument");
            }
        }
        // 只加载已有文件，不能因路径写错就生成另一套带种子账号的数据。
        if (state_path.empty() || !std::filesystem::is_regular_file(state_path)) {
            throw std::invalid_argument("--state must name an existing state file");
        }
        StorageOptions storage{state_path, false};
        auto context = make_seed_context();
        const auto loaded = load_state(storage, context);
        if (loaded.code != ResultCode::Success) throw std::runtime_error(loaded.message);

        SessionOptions options;
        options.allowed_origin = "http://127.0.0.1:" + std::to_string(port);
        SessionRuntime runtime(context, storage, options);
        bind_session_runtime(runtime);
        httplib::Server server;
        runtime.register_auth_routes(server);
        std::cout << "Authentication API listening at " << options.allowed_origin << '\n';
        // 这是供组员接入和验证的最小入口。正式三端服务器复用 runtime 和注册函数。
        if (!server.listen("127.0.0.1", port)) throw std::runtime_error("cannot listen on port");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Authentication server failed: " << error.what() << '\n';
        return 1;
    }
}
