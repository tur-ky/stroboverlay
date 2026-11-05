#include "core/menu.h"

#include <algorithm>
#include <cstdio>

#include "imgui.h"

std::vector<std::string>* g_menuTextLog = nullptr;

namespace {

const float kPanelWidth = 420.0f;
const float kPanelHeight = 460.0f;
const float kPanelMargin = 24.0f;
const float kRailWidth = 48.0f;
const float kContentPadding = 16.0f;
const float kTypedWidth = 96.0f;
const float kThumbnailPixels = 72.0f;
const float kColorPickerWidth = 220.0f;
const double kSliderMaxMs = 2000.0;
const int kSliderMaxFrames = 2000;

struct RailItem {
    MenuSection section;
    const char* icon;
    const char* name;
};

const RailItem kRail[] = {
    {MenuSection::Timing, "\xEE\xA4\x96", "Timing"},
    {MenuSection::Appearance, "\xEE\x9E\x90", "Appearance"},
    {MenuSection::Mask, "\xEE\xA2\x90", "Mask"},
    {MenuSection::Profiles, "\xEE\xA3\xB1", "Profiles"},
    {MenuSection::Display, "\xEE\x9F\xB4", "Display"},
};

void Log(const char* text) {
    if (g_menuTextLog) g_menuTextLog->push_back(text);
}

void Label(const char* text) {
    Log(text);
    ImGui::TextUnformatted(text);
}

void Hint(const char* text) {
    Log(text);
    ImGui::PushTextWrapPos(0.0f);
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextUnformatted(text);
    ImGui::PopStyleColor();
    ImGui::PopTextWrapPos();
}

bool LoggedButton(const char* text, const ImVec2& size = ImVec2(0, 0)) {
    Log(text);
    return ImGui::Button(text, size);
}

// One side of a two-way switch. The active side is white on black's inverse.
bool Segment(const char* text, bool active) {
    Log(text);
    if (active) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(1, 1, 1, 1));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1, 1, 1, 1));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1, 1, 1, 1));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0, 0, 0, 1));
    }
    bool pressed = ImGui::Button(text);
    if (active) ImGui::PopStyleColor(4);
    return pressed;
}

bool DurationMs(const char* label, const char* id, double& value) {
    Label(label);
    ImGui::PushID(id);
    bool changed = false;
    const double lo = 0.0;
    const float typedWidth = kTypedWidth * ImGui::GetStyle().FontScaleDpi;
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - typedWidth - ImGui::GetStyle().ItemSpacing.x);
    changed |= ImGui::SliderScalar("##slider", ImGuiDataType_Double, &value, &lo, &kSliderMaxMs, "");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(typedWidth);
    changed |= ImGui::InputDouble("##typed", &value, 0.0, 0.0, "%.1f ms");
    ImGui::PopID();
    return changed;
}

bool DurationFrames(const char* label, const char* id, int& value) {
    Label(label);
    ImGui::PushID(id);
    bool changed = false;
    const float typedWidth = kTypedWidth * ImGui::GetStyle().FontScaleDpi;
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - typedWidth - ImGui::GetStyle().ItemSpacing.x);
    changed |= ImGui::SliderInt("##slider", &value, 0, kSliderMaxFrames, "");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(typedWidth);
    changed |= ImGui::InputInt("##typed", &value, 0, 0);
    ImGui::PopID();
    return changed;
}

void DrawTiming(MenuContext& ctx, MenuActions& actions) {
    StrobeSettings& s = ctx.settings->strobe;
    TimingSettings& t = s.timing;
    bool changed = false;

    if (Segment("ms", t.mode == TimingMode::Ms)) {
        t.mode = TimingMode::Ms;
        changed = true;
    }
    ImGui::SameLine();
    if (Segment("frames", t.mode == TimingMode::Frames)) {
        t.mode = TimingMode::Frames;
        changed = true;
    }

    if (t.mode == TimingMode::Ms) {
        changed |= DurationMs("Strobe", "strobe", t.onMs);
        changed |= DurationMs("Game visible", "visible", t.offMs);
    } else {
        changed |= DurationFrames("Strobe", "strobe", t.onFrames);
        changed |= DurationFrames("Game visible", "visible", t.offFrames);

        char text[64];
        snprintf(text, sizeof(text), "Match monitor (%.0f Hz)", ctx.detectedHz);
        Log(text);
        changed |= ImGui::Checkbox(text, &s.refreshAuto);
        if (!s.refreshAuto) {
            ImGui::SetNextItemWidth(-1.0f);
            changed |= ImGui::InputDouble("##refresh", &t.refreshHz, 0.0, 0.0, "%.2f Hz");
        }
    }

    if (changed) {
        ClampTiming(t);
        actions.strobeSettingsChanged = true;
    }
}

void DrawThumbnails(MenuContext& ctx, const std::vector<std::wstring>& files, std::wstring& selected,
                    bool& changed) {
    const float scale = ImGui::GetStyle().FontScaleDpi;
    const float size = kThumbnailPixels * scale;
    const float gap = ImGui::GetStyle().ItemSpacing.x;
    const float cell = size + 4.0f * scale + gap;
    const float avail = ImGui::GetContentRegionAvail().x;
    float x = 0.0f;

    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2.0f * scale, 2.0f * scale));
    for (const std::wstring& file : files) {
        if (x > 0.0f && x + cell - gap <= avail) {
            ImGui::SameLine();
        } else {
            x = 0.0f;
        }

        void* texture = ctx.thumbnail ? ctx.thumbnail(file) : nullptr;
        const bool active = file == selected;
        ImGui::PushID(reinterpret_cast<const char*>(file.c_str()),
                      reinterpret_cast<const char*>(file.c_str() + file.size()));
        ImGui::PushStyleColor(ImGuiCol_Button, active ? ImVec4(1, 1, 1, 1) : ImVec4(0.14f, 0.14f, 0.14f, 1));
        if (ImGui::ImageButton("thumb", reinterpret_cast<ImTextureID>(texture), ImVec2(size, size))) {
            selected = file;
            changed = true;
        }
        ImGui::PopStyleColor();
        ImGui::PopID();
        x += cell;
    }
    ImGui::PopStyleVar();
}

void DrawAppearance(MenuContext& ctx, MenuActions& actions) {
    StrobeSettings& s = ctx.settings->strobe;
    bool changed = false;
    if (Segment("Color", s.fill == FillKind::Color)) {
        s.fill = FillKind::Color;
        changed = true;
    }
    ImGui::SameLine();
    if (Segment("Image", s.fill == FillKind::Image)) {
        s.fill = FillKind::Image;
        changed = true;
    }

    if (s.fill == FillKind::Color) {
        float rgb[3] = {((s.colorRgb >> 16) & 0xFF) / 255.0f, ((s.colorRgb >> 8) & 0xFF) / 255.0f,
                        (s.colorRgb & 0xFF) / 255.0f};
        ImGui::SetNextItemWidth(kColorPickerWidth * ImGui::GetStyle().FontScaleDpi);
        if (ImGui::ColorPicker3("##color", rgb,
                                ImGuiColorEditFlags_NoSidePreview | ImGuiColorEditFlags_NoLabel |
                                    ImGuiColorEditFlags_DisplayHex)) {
            const uint32_t r = static_cast<uint32_t>(rgb[0] * 255.0f + 0.5f);
            const uint32_t g = static_cast<uint32_t>(rgb[1] * 255.0f + 0.5f);
            const uint32_t b = static_cast<uint32_t>(rgb[2] * 255.0f + 0.5f);
            s.colorRgb = (r << 16) | (g << 8) | b;
            changed = true;
        }
    } else {
        if (ctx.images.empty()) Hint("Import an image to draw it on each strobe.");
        DrawThumbnails(ctx, ctx.images, s.imageFile, changed);
        if (LoggedButton("Import image")) actions.importImage = true;
    }
    if (changed) actions.strobeSettingsChanged = true;
}

void DrawMask(MenuContext& ctx, MenuActions& actions) {
    StrobeSettings& s = ctx.settings->strobe;
    bool changed = false;
    Log("Use mask");
    changed |= ImGui::Checkbox("Use mask", &s.maskEnabled);
    Log("Invert");
    changed |= ImGui::Checkbox("Invert", &s.maskInvert);
    Hint("White draws the strobe, black leaves the game visible.");
    if (ctx.masks.empty()) Hint("Import a black and white image to use it here.");
    DrawThumbnails(ctx, ctx.masks, s.maskFile, changed);
    if (LoggedButton("Import mask")) actions.importMask = true;
    if (changed) actions.strobeSettingsChanged = true;
}

void DrawContent(MenuContext& ctx, MenuSection section, MenuActions& actions) {
    switch (section) {
        case MenuSection::Timing:
            DrawTiming(ctx, actions);
            break;
        case MenuSection::Appearance:
            DrawAppearance(ctx, actions);
            break;
        case MenuSection::Mask:
            DrawMask(ctx, actions);
            break;
        case MenuSection::Profiles:
        case MenuSection::Display:
            break;
    }
}

void DrawRail(MenuSection& section, float height) {
    ImGui::BeginChild("rail", ImVec2(kRailWidth * ImGui::GetStyle().FontScaleDpi, height));
    const float scale = ImGui::GetStyle().FontScaleDpi;
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 origin = ImGui::GetWindowPos();
    draw->AddRectFilled(origin, ImVec2(origin.x + kRailWidth * scale, origin.y + height),
                        ImGui::GetColorU32(ImGuiCol_FrameBg), 8.0f * scale,
                        ImDrawFlags_RoundCornersLeft);

    ImGui::SetCursorPosY(8.0f * scale);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    for (const RailItem& item : kRail) {
        const bool active = item.section == section;
        ImGui::PushID(item.name);
        ImGui::SetCursorPosX(6.0f * scale);
        ImGui::PushStyleColor(ImGuiCol_Button, active ? ImVec4(1, 1, 1, 1) : ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                              active ? ImVec4(1, 1, 1, 1) : ImGui::GetStyleColorVec4(ImGuiCol_ButtonHovered));
        ImGui::PushStyleColor(ImGuiCol_Text, active ? ImVec4(0, 0, 0, 1) : ImVec4(1, 1, 1, 1));
        if (ImGui::Button(item.icon, ImVec2(36.0f * scale, 36.0f * scale))) section = item.section;
        ImGui::PopStyleColor(3);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", item.name);
        ImGui::PopID();
    }
    ImGui::PopStyleVar();
    ImGui::EndChild();
}

const char* SectionName(MenuSection section) {
    for (const RailItem& item : kRail) {
        if (item.section == section) return item.name;
    }
    return "";
}

}  // namespace

void ClampTiming(TimingSettings& t) {
    t.onMs = std::clamp(t.onMs, 0.0, 99999.0);
    t.offMs = std::clamp(t.offMs, 0.0, 99999.0);
    t.onFrames = std::clamp(t.onFrames, 0, 9999);
    t.offFrames = std::clamp(t.offFrames, 0, 9999);
    if (!(t.refreshHz > 0.0)) t.refreshHz = 60.0;
    t.refreshHz = std::min(t.refreshHz, 9999.0);
}

MenuActions DrawMenu(MenuContext& ctx, MenuSection& section) {
    MenuActions actions;
    const float scale = ctx.dpiScale;
    const ImVec2 size(kPanelWidth * scale, kPanelHeight * scale);
    const float padding = kContentPadding * scale;

    ImGui::SetNextWindowPos(ImVec2(kPanelMargin * scale, kPanelMargin * scale));
    ImGui::SetNextWindowSize(size);
    ImGui::Begin("Stroboverlay", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar |
                     ImGuiWindowFlags_NoSavedSettings);

    DrawRail(section, size.y);
    ImGui::SameLine(0.0f, 0.0f);

    ImGui::BeginChild("body", ImVec2(0.0f, size.y));
    ImGui::SetCursorPos(ImVec2(padding, padding));
    ImGui::BeginGroup();
    const float contentWidth = ImGui::GetContentRegionAvail().x - padding;
    // Two lines for warnings stay reserved, so the start button never moves.
    const float footer = ImGui::GetFrameHeight() + ImGui::GetTextLineHeightWithSpacing() * 2.0f +
                         padding + 8.0f * scale;
    ImGui::BeginChild("section", ImVec2(contentWidth, size.y - padding - footer));
    Label(SectionName(section));
    ImGui::Separator();
    DrawContent(ctx, section, actions);
    ImGui::EndChild();

    const float buttonY = size.y - padding - ImGui::GetFrameHeight();
    if (!ctx.warning.empty()) {
        char text[256];
        WideCharToMultiByte(CP_UTF8, 0, ctx.warning.c_str(), -1, text, sizeof(text), nullptr, nullptr);
        text[sizeof(text) - 1] = 0;
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + contentWidth);
        Hint(text);
        ImGui::PopTextWrapPos();
    }
    ImGui::SetCursorPosY(buttonY);
    if (LoggedButton(ctx.running ? "Stop strobe" : "Start strobe", ImVec2(contentWidth, 0.0f))) {
        actions.toggleStrobe = true;
    }
    ImGui::EndGroup();
    ImGui::EndChild();
    ImGui::End();

    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) && !ImGui::GetIO().WantTextInput) {
        actions.closeMenu = true;
    }
    return actions;
}
