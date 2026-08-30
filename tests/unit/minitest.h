// =============================================================================
// minitest.h: 极简单测框架（core 层零 Qt 依赖，任何 g++ 环境可运行）
// 用法：TEST(name) { CHECK(cond); CHECK_EQ(a, b); } ... int main(){ return minitest::runAll(); }
// =============================================================================
#pragma once

#include <cstdio>
#include <functional>
#include <vector>

namespace minitest {

struct Case {
    const char* name;
    void (*fn)();
};

inline std::vector<Case>& registry() {
    static std::vector<Case> r;
    return r;
}

struct Registrar {
    Registrar(const char* n, void (*fn)()) { registry().push_back({n, fn}); }
};

inline int checks = 0;
inline int failures = 0;

#define TEST(name)                                             \
    static void name();                                        \
    static ::minitest::Registrar reg_##name(#name, &name);     \
    static void name()

#define CHECK(cond)                                                          \
    do {                                                                     \
        ++::minitest::checks;                                                \
        if (!(cond)) {                                                       \
            ++::minitest::failures;                                          \
            std::fprintf(stderr, "  FAIL %s:%d: %s\n", __FILE__, __LINE__,   \
                         #cond);                                             \
        }                                                                    \
    } while (0)

#define CHECK_EQ(a, b)                                                        \
    do {                                                                      \
        ++::minitest::checks;                                                 \
        auto va = (a);                                                        \
        auto vb = (b);                                                        \
        if (!(va == vb)) {                                                    \
            ++::minitest::failures;                                           \
            std::fprintf(stderr, "  FAIL %s:%d: %s == %s\n", __FILE__,        \
                         __LINE__, #a, #b);                                   \
        }                                                                     \
    } while (0)

inline int runAll() {
    for (const auto& c : registry()) {
        const int before = failures;
        std::fprintf(stdout, "[RUN ] %s\n", c.name);
        c.fn();
        std::fprintf(stdout, "[%s] %s\n", failures == before ? "PASS" : "FAIL",
                     c.name);
    }
    std::fprintf(stdout, "\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}

}  // namespace minitest