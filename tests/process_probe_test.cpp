#include "core/menu.h"
#include "core/process_probe.h"
#include "test.h"

TEST(finds_own_process) { CHECK(IsProcessRunning({L"CORE_TESTS.EXE"})); }

TEST(missing_process) { CHECK(!IsProcessRunning({L"definitely-not-running.exe"})); }

TEST(any_listed_name_matches) {
    CHECK(IsProcessRunning({L"definitely-not-running.exe", L"core_tests.exe"}));
}

TEST(dpi_scale) {
    CHECK(MenuScaleForDpi(96) == 1.0f);
    CHECK(MenuScaleForDpi(144) == 1.5f);
}
