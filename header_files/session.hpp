#pragma once

#include "password_service.hpp"
#include "persistent_administration_service.hpp"
#include "persistent_registration_service.hpp"

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <type_traits>
#include <utility>

namespace course_registration {

struct SessionCheck {
    OperationResult result;
    std::optional<UserIdentity> identity;
};

enum class ResultSource { Session, Permission, Csrf, Business, Internal };

void write_result(httplib::Response& response, const OperationResult& result,
                  ResultSource source, const nlohmann::json& data = nullptr);

// 时限可通过配置调整。测试可以提供自己的时钟，不需要实际等待半小时。
struct SessionOptions {
    std::string allowed_origin = "http://127.0.0.1:8080";
    std::chrono::seconds idle_timeout = std::chrono::minutes(30);
    std::chrono::seconds absolute_timeout = std::chrono::hours(8);
    std::function<std::chrono::steady_clock::time_point()> now =
        std::chrono::steady_clock::now;
};

class SessionRuntime;

// 这个对象只在 with_business_lock 的回调内有效。不要保留它或它返回的引用。
class LockedServices {
public:
    PersistentAdministratorService& admin_service() { return admin_; }
    TeacherService& teacher_service() { return teacher_; }
    PersistentRegistrationService& registration_service() { return registration_; }

    // 为兼容 Zhu 的 Services 适配器保留真实引用。资料修改仍要调用持久化服务。
    SystemContext& context() { return context_; }
    SystemContext context_snapshot() const { return context_; }

private:
    friend class SessionRuntime;
    LockedServices(SystemContext& context, const StorageOptions& storage)
        : context_(context), admin_(context, storage), teacher_(context),
          registration_(context, storage) {}
    SystemContext& context_;
    PersistentAdministratorService admin_;
    TeacherService teacher_;
    PersistentRegistrationService registration_;
};

// 一个服务器只构造一个运行对象。会话表和业务状态共用一把锁，避免锁顺序相反。
// context 必须比本对象活得更久。服务器完全停止处理请求后，才能销毁本对象。
class SessionRuntime {
public:
    SessionRuntime(SystemContext& context, StorageOptions storage,
                   SessionOptions options = {});
    ~SessionRuntime();
    SessionRuntime(const SessionRuntime&) = delete;
    SessionRuntime& operator=(const SessionRuntime&) = delete;

    SessionCheck require_session(const httplib::Request& request);
    OperationResult require_csrf(const httplib::Request& request);
    OperationResult run_business_locked(
        const UserIdentity& actor, Role required_role,
        const std::function<void(LockedServices&)>& operation);

    // 注册统一认证路由，同时安装兜底异常处理。请在 server.listen() 前调用。
    void register_auth_routes(httplib::Server& server);

private:
    struct Session {
        UserIdentity identity;
        std::string password_hash;
        std::string csrf_token;
        std::chrono::steady_clock::time_point created_at;
        std::chrono::steady_clock::time_point last_seen;
    };

    OperationResult check_actor_locked(const UserIdentity& actor) const;
    Session* find_session_locked(const httplib::Request& request, bool refresh);
    void revoke_account_locked(const UserIdentity& actor);
    bool same_origin(const httplib::Request& request) const;
    std::string cookie(const std::string& token, bool clear = false) const;
    void login(const httplib::Request& request, httplib::Response& response);
    void me(const httplib::Request& request, httplib::Response& response);
    void logout(const httplib::Request& request, httplib::Response& response);
    void password(const httplib::Request& request, httplib::Response& response);

    SystemContext& context_;
    StorageOptions storage_;
    SessionOptions options_;
    std::string origin_;
    bool secure_cookie_ = false;
    std::mutex mutex_;
    std::map<std::string, Session> sessions_;
};

// Zhu 已按不带 runtime 参数的签名编写适配器，因此提供这个单进程接入点。
// 在启动服务器之前绑定；运行期间不能切换。测试在停止旧服务器后可以重新绑定。
void bind_session_runtime(SessionRuntime& runtime);
SessionRuntime& active_session_runtime();
SessionCheck require_session(const httplib::Request& request);
OperationResult require_csrf(const httplib::Request& request);

// 回调的业务结果由捕获变量带出来；本函数只返回身份或角色检查的结果。
// 异常继续交给 HTTP 兜底处理，不会被转成 401。RAII 会先释放业务锁。
template<class Fn>
OperationResult with_business_lock(const UserIdentity& actor, Role required_role,
                                    Fn&& fn) {
    static_assert(std::is_void_v<std::invoke_result_t<Fn, LockedServices&>>,
                  "business callback must return void; capture the business result");
    return active_session_runtime().run_business_locked(
        actor, required_role, std::forward<Fn>(fn));
}

}  // namespace course_registration
