#include <algorithm>
#include <string>
#include <vector>

#include "core/menu.h"
#include "imgui.h"
#include "test.h"

namespace {

// One headless ImGui frame around DrawMenu, with no renderer attached.
struct Frame {
    Frame() {
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
        io.IniFilename = nullptr;
        io.DisplaySize = ImVec2(1024, 768);
        io.DeltaTime = 1.0f / 60.0f;
        io.Fonts->AddFontDefault();
        ApplyMenuTheme(1.0f);
    }
    ~Frame() { ImGui::DestroyContext(); }

    MenuActions Draw(MenuContext& ctx, MenuSection section, std::vector<std::string>* log) {
        g_menuTextLog = log;
        ImGui::NewFrame();
        MenuActions actions = DrawMenu(ctx, section);
        ImGui::EndFrame();
        g_menuTextLog = nullptr;
        return actions;
    }
};

MenuContext MakeContext(AppSettings& settings) {
    MenuContext ctx;
    ctx.settings = &settings;
    ctx.dpiScale = 1.0f;
    ctx.detectedHz = 399.0;
    return ctx;
}

bool Contains(const std::vector<std::string>& labels, const char* text) {
    return std::find(labels.begin(), labels.end(), text) != labels.end();
}

}  // namespace

TEST(theme_uses_only_greys) {
    Frame frame;
    for (const ImVec4& c : ImGui::GetStyle().Colors) {
        CHECK(c.x == c.y);
        CHECK(c.y == c.z);
    }
}

TEST(timing_labels_say_strobe_and_game_visible) {
    Frame frame;
    AppSettings settings;
    MenuContext ctx = MakeContext(settings);
    std::vector<std::string> labels;
    frame.Draw(ctx, MenuSection::Timing, &labels);
    CHECK(Contains(labels, "Strobe"));
    CHECK(Contains(labels, "Game visible"));
    CHECK(!Contains(labels, "On"));
    CHECK(!Contains(labels, "Off"));
}

TEST(every_section_draws) {
    Frame frame;
    AppSettings settings;
    MenuContext ctx = MakeContext(settings);
    for (MenuSection s : {MenuSection::Timing, MenuSection::Appearance, MenuSection::Mask,
                          MenuSection::Profiles, MenuSection::Display}) {
        frame.Draw(ctx, s, nullptr);
    }
}

TEST(running_state_labels_the_start_button) {
    Frame frame;
    AppSettings settings;
    MenuContext ctx = MakeContext(settings);
    std::vector<std::string> labels;
    frame.Draw(ctx, MenuSection::Timing, &labels);
    CHECK(Contains(labels, "Start strobe"));
    labels.clear();
    ctx.running = true;
    frame.Draw(ctx, MenuSection::Timing, &labels);
    CHECK(Contains(labels, "Stop strobe"));
}

TEST(clamp_keeps_values_config_can_reload) {
    TimingSettings t;
    t.onMs = -3;
    t.offMs = 500000;
    t.onFrames = -1;
    t.offFrames = 50000;
    ClampTiming(t);
    CHECK(t.onMs == 0.0);
    CHECK(t.offMs == 99999.0);
    CHECK(t.onFrames == 0);
    CHECK(t.offFrames == 9999);
}

TEST(delete_needs_second_request_for_same_name) {
    ConfirmState c;
    CHECK(!c.Request(L"a"));
    CHECK(c.Request(L"a"));
    CHECK(!c.Request(L"a"));
    CHECK(!c.Request(L"b"));
    CHECK(c.Request(L"b"));
}
