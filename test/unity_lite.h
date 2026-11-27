#ifndef NEZHA_UNITY_LITE_H
#define NEZHA_UNITY_LITE_H

#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

namespace nezha_test {

struct TestCase {
    std::string name;
    std::function<void()> fn;
};

inline std::vector<TestCase> &registry() {
    static std::vector<TestCase> tests;
    return tests;
}

inline int &currentFailures() {
    static int failures = 0;
    return failures;
}

struct Registrar {
    Registrar(const char *name, std::function<void()> fn) { registry().push_back({name, std::move(fn)}); }
};

inline int runAll(const char *suiteName) {
    int passed = 0;
    int failed = 0;
    std::printf("\n=== Suite: %s (%zu tests) ===\n", suiteName, registry().size());
    for (const auto &t : registry()) {
        currentFailures() = 0;
        t.fn();
        if (currentFailures() == 0) {
            std::printf("  [PASS] %s\n", t.name.c_str());
            passed++;
        } else {
            std::printf("  [FAIL] %s (%d assertion(s) failed)\n", t.name.c_str(), currentFailures());
            failed++;
        }
    }
    std::printf("=== %s: %d/%d passed ===\n", suiteName, passed, passed + failed);
    return failed == 0 ? 0 : 1;
}

inline void assertStrEq(const char *sa, const char *sb, const char *file, int line) {
    if (!sa || !sb || std::strcmp(sa, sb) != 0) {
        std::fprintf(
            stderr, "    ASSERT_STREQ failed at %s:%d: \"%s\" != \"%s\"\n", file, line,
            sa ? sa : "(null)", sb ? sb : "(null)"
        );
        currentFailures()++;
    }
}

} // namespace nezha_test

#define TEST_CASE(name)                                                                                      \
    static void test_fn_##name();                                                                            \
    static ::nezha_test::Registrar test_reg_##name(#name, test_fn_##name);                                   \
    static void test_fn_##name()

#define ASSERT_TRUE(cond)                                                                                    \
    do {                                                                                                     \
        if (!(cond)) {                                                                                       \
            std::fprintf(stderr, "    ASSERT_TRUE failed at %s:%d: %s\n", __FILE__, __LINE__, #cond);       \
            ::nezha_test::currentFailures()++;                                                               \
        }                                                                                                    \
    } while (0)

#define ASSERT_FALSE(cond) ASSERT_TRUE(!(cond))

#define ASSERT_EQ(a, b)                                                                                      \
    do {                                                                                                     \
        auto _a = (a);                                                                                       \
        auto _b = (b);                                                                                       \
        if (!(_a == _b)) {                                                                                   \
            std::fprintf(stderr, "    ASSERT_EQ failed at %s:%d: %s != %s\n", __FILE__, __LINE__, #a, #b);  \
            ::nezha_test::currentFailures()++;                                                               \
        }                                                                                                    \
    } while (0)

#define ASSERT_NE(a, b)                                                                                      \
    do {                                                                                                     \
        auto _a = (a);                                                                                       \
        auto _b = (b);                                                                                       \
        if (_a == _b) {                                                                                      \
            std::fprintf(stderr, "    ASSERT_NE failed at %s:%d: %s == %s\n", __FILE__, __LINE__, #a, #b);  \
            ::nezha_test::currentFailures()++;                                                               \
        }                                                                                                    \
    } while (0)

#define ASSERT_STREQ(a, b) ::nezha_test::assertStrEq((a), (b), __FILE__, __LINE__)

#define ASSERT_MEMEQ(a, b, n)                                                                                \
    do {                                                                                                     \
        if (std::memcmp((a), (b), (n)) != 0) {                                                               \
            std::fprintf(                                                                                    \
                stderr, "    ASSERT_MEMEQ failed at %s:%d: %s != %s (%zu bytes)\n", __FILE__, __LINE__, #a, \
                #b, (size_t)(n)                                                                              \
            );                                                                                               \
            ::nezha_test::currentFailures()++;                                                               \
        }                                                                                                    \
    } while (0)

#endif // NEZHA_UNITY_LITE_H
