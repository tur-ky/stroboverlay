#pragma once
#include <cmath>
#include <cstdio>
#include <vector>

struct TestCase { const char* name; void (*fn)(); };
std::vector<TestCase>& Tests();
extern int g_failures;

struct TestRegistrar {
    TestRegistrar(const char* name, void (*fn)()) { Tests().push_back({name, fn}); }
};

#define TEST(name)                                         \
    static void test_##name();                             \
    static TestRegistrar reg_##name(#name, test_##name);   \
    static void test_##name()

#define CHECK(expr)                                                         \
    do {                                                                    \
        if (!(expr)) {                                                      \
            std::printf("%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #expr); \
            ++g_failures;                                                   \
        }                                                                   \
    } while (0)

#define CHECK_NEAR(a, b, eps) CHECK(std::fabs((a) - (b)) <= (eps))
