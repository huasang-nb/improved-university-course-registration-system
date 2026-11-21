// libsodium 链接与 Argon2id 行为探针（Zhu Ziliang）
//
// 只在 -DCOURSE_REGISTRATION_USE_SODIUM=ON 时编译进构建，并用 ctest 注册为
// libsodium_probe：验证"能编译、能链接、哈希与校验结果正确"。
// 使用 MIN 参数，避免拖慢测试。

#include <sodium.h>

#include <cstdio>
#include <cstring>

int main() {
    if (sodium_init() < 0) {
        std::printf("FAIL: sodium_init\n");
        return 1;
    }

    const char* password = "Probe-Password-123";
    char hashed[crypto_pwhash_STRBYTES];
    if (crypto_pwhash_str(hashed, password, std::strlen(password),
                          crypto_pwhash_OPSLIMIT_MIN,
                          crypto_pwhash_MEMLIMIT_MIN) != 0) {
        std::printf("FAIL: crypto_pwhash_str\n");
        return 1;
    }

    const int ok = crypto_pwhash_str_verify(
        hashed, password, std::strlen(password));
    const int wrong = crypto_pwhash_str_verify(hashed, "wrong-password", 14);

    std::printf("libsodium_probe: hash_len=%zu verify_ok=%d verify_wrong=%d\n",
                std::strlen(hashed), ok, wrong);

    if (ok != 0 || wrong == 0) {
        std::printf("FAIL: verify semantics\n");
        return 1;
    }
    return 0;
}
