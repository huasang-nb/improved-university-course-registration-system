// 本机密码设置工具是暂行方案，后续账号初始化和重置政策仍待团队讨论。
// 运行前先停止所有使用同一 JSON 文件的程序。密码在控制台中隐藏输入两次。

#include "password_service.hpp"

#include <sodium.h>

#define NOMINMAX
#include <windows.h>

#include <array>
#include <filesystem>
#include <iostream>
#include <stdexcept>

using namespace course_registration;
namespace fs = std::filesystem;

namespace {

// 两个设置工具同时读旧文件再保存，会覆盖彼此的修改。
// 在读取 JSON 前独占打开同名的锁文件，直到本次设置结束才释放。
// 锁文件保留在磁盘上，是否被占用由 Windows 的句柄判断，程序退出后会自动释放句柄。
class PasswordFileLock {
public:
    explicit PasswordFileLock(const fs::path& state_path) {
        fs::path lock_path = state_path;
        lock_path += L".password.lock";
        handle_ = CreateFileW(lock_path.c_str(), GENERIC_READ | GENERIC_WRITE,
                              0, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle_ == INVALID_HANDLE_VALUE) {
            throw std::runtime_error(
                "cannot lock state file; another password tool may be running");
        }
    }
    ~PasswordFileLock() { CloseHandle(handle_); }
    PasswordFileLock(const PasswordFileLock&) = delete;
    PasswordFileLock& operator=(const PasswordFileLock&) = delete;

private:
    HANDLE handle_ = INVALID_HANDLE_VALUE;
};

// 控制台使用 UTF-16 输入，转换为 UTF-8 后交给密码校验代码。
// 输入过程中不显示密码。离开作用域时清除两种形式的输入缓冲区。
class ConsolePassword {
public:
    explicit ConsolePassword(const char* prompt) {
        input_ = GetStdHandle(STD_INPUT_HANDLE);
        if (!GetConsoleMode(input_, &original_mode_)) {
            throw std::runtime_error("password input requires an interactive console");
        }
        // 暂时关闭回显和 Ctrl+C 的自动终止行为，让取消时也能清理密码并恢复控制台。
        const DWORD mode = original_mode_ &
            ~(ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT | ENABLE_PROCESSED_INPUT);
        if (!SetConsoleMode(input_, mode)) {
            throw std::runtime_error("cannot configure hidden password input");
        }
        std::cout << prompt << std::flush;
        try {
            read();
        } catch (...) {
            clear();
            SetConsoleMode(input_, original_mode_);
            throw;
        }
    }
    ~ConsolePassword() {
        clear();
        SetConsoleMode(input_, original_mode_);
    }
    ConsolePassword(const ConsolePassword&) = delete;
    ConsolePassword& operator=(const ConsolePassword&) = delete;
    const std::string& value() const { return utf8_; }

private:
    void clear() {
        sodium_memzero(wide_.data(), sizeof(wide_));
        sodium_memzero(utf8_.data(), utf8_.size());
    }

    void read() {
        std::size_t length = 0;
        while (true) {
            INPUT_RECORD input{};
            DWORD count = 0;
            if (!ReadConsoleInputW(input_, &input, 1, &count) || count != 1) {
                throw std::runtime_error("cannot read password input");
            }
            if (input.EventType != KEY_EVENT || !input.Event.KeyEvent.bKeyDown) {
                continue;
            }
            const auto& key = input.Event.KeyEvent;
            const wchar_t character = key.uChar.UnicodeChar;
            if (key.wVirtualKeyCode == VK_RETURN) {
                std::cout << '\n';
                break;
            }
            if (character == 3 || key.wVirtualKeyCode == VK_ESCAPE) {
                throw std::runtime_error("password entry cancelled");
            }
            if (character == 0) {
                // 方向键等按键没有文本内容，保留已经输入的密码。
                continue;
            }
            if (key.wVirtualKeyCode == VK_BACK) {
                if (length > 0) {
                    --length;
                    if (wide_[length] >= 0xDC00 && wide_[length] <= 0xDFFF &&
                        length > 0 && wide_[length - 1] >= 0xD800 &&
                        wide_[length - 1] <= 0xDBFF) {
                        wide_[--length] = 0;
                    }
                    wide_[length] = 0;
                }
                continue;
            }
            if (character < 32) {
                throw std::runtime_error("unsupported control key in password input");
            }
            // 4096 个 UTF-16 字符是本机工具输入缓冲区的限制，不作为账号密码政策。
            for (WORD repeat = 0; repeat < key.wRepeatCount; ++repeat) {
                if (length == wide_.size()) {
                    throw std::runtime_error("password exceeds console input capacity");
                }
                wide_[length++] = character;
            }
        }
        if (length == 0) {
            throw std::runtime_error("password must not be empty");
        }
        const int bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
            wide_.data(), static_cast<int>(length), nullptr, 0, nullptr, nullptr);
        if (bytes == 0) {
            throw std::runtime_error("cannot decode password input");
        }
        utf8_.resize(bytes);
        if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide_.data(),
                static_cast<int>(length), utf8_.data(), bytes, nullptr, nullptr) != bytes) {
            throw std::runtime_error("cannot convert password input");
        }
    }

    std::array<wchar_t, 4096> wide_{};
    std::string utf8_;
    HANDLE input_ = INVALID_HANDLE_VALUE;
    DWORD original_mode_ = 0;
};

void usage() {
    std::cout << "Temporary local password setup\n"
                 "Usage: password_setup --state PATH --account ACCOUNT [--replace]\n"
                 "Stop all programs using this state file before running this tool.\n"
                 "Passwords are entered twice in the console and are not echoed.\n"
                 "--replace explicitly allows replacing an existing password.\n";
}

}  // namespace

int main(int argc, char** argv) {
    std::string state_path;
    std::string account_number;
    bool replace_existing = false;
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--help" && argc == 2) {
            usage();
            return 0;
        }
        if (argument == "--state" && state_path.empty() && index + 1 < argc) {
            state_path = argv[++index];
        } else if (argument == "--account" && account_number.empty() && index + 1 < argc) {
            account_number = argv[++index];
        } else if (argument == "--replace" && !replace_existing) {
            replace_existing = true;
        } else {
            usage();
            return 2;
        }
    }
    if (state_path.empty() || normalize_account_key(account_number).empty()) {
        usage();
        return 2;
    }
    try {
        if (!fs::is_regular_file(state_path)) {
            throw std::runtime_error("state file must already exist");
        }
        // 先解析成同一个规范路径，避免 . 和 .. 等写法绕过两个工具之间的文件锁。
        const auto canonical_path = fs::canonical(state_path);
        PasswordFileLock lock(canonical_path);
        StorageOptions storage{canonical_path.string(), false};
        auto context = make_seed_context();
        auto result = load_state(storage, context);
        if (result.code != ResultCode::Success) {
            throw std::runtime_error("cannot load state: " + result.message);
        }
        PasswordAccount account;
        result = find_password_account(context, account_number, account);
        if (result.code != ResultCode::Success) {
            throw std::runtime_error(result.message);
        }
        if (account.password_set && !replace_existing) {
            throw std::runtime_error("password already exists; use --replace to replace it");
        }
        if (sodium_init() < 0) {
            throw std::runtime_error("cannot initialize libsodium");
        }
        ConsolePassword first("New password: ");
        ConsolePassword confirmation("Repeat password: ");
        if (first.value() != confirmation.value()) {
            throw std::runtime_error("passwords do not match");
        }
        result = set_account_password(context, storage, account_number,
                                       first.value(), replace_existing);
        if (result.code != ResultCode::Success) {
            throw std::runtime_error("password was not saved: " + result.message);
        }
        std::cout << "Password saved. Account status and other records were preserved.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Password setup failed: " << error.what() << '\n';
        return 1;
    }
}
