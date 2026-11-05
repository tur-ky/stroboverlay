#include "test.h"

std::vector<TestCase>& Tests() {
    static std::vector<TestCase> tests;
    return tests;
}
int g_failures = 0;

int main() {
    for (const TestCase& t : Tests()) {
        int before = g_failures;
        t.fn();
        std::printf("%s %s\n", g_failures == before ? "ok  " : "FAIL", t.name);
    }
    std::printf("%zu tests, %d failed checks\n", Tests().size(), g_failures);
    return g_failures == 0 ? 0 : 1;
}
