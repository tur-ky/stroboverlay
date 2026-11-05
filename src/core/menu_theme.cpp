#include "core/menu.h"
#include "imgui.h"

namespace {

ImVec4 Grey(int level, float alpha = 1.0f) {
    float v = level / 255.0f;
    return ImVec4(v, v, v, alpha);
}

const float kFontPixels = 15.0f;
// Segoe MDL2 Assets holds the rail icons in this private-use range.
const ImWchar kIconRange[] = {0xE700, 0xF8FF, 0};

}  // namespace

void ApplyMenuTheme(float dpiScale) {
    ImGuiStyle& style = ImGui::GetStyle();
    style = ImGuiStyle();
    ImGui::StyleColorsDark(&style);

    for (ImVec4& c : style.Colors) {
        float luma = 0.2126f * c.x + 0.7152f * c.y + 0.0722f * c.z;
        c = ImVec4(luma, luma, luma, c.w);
    }

    ImVec4* c = style.Colors;
    c[ImGuiCol_WindowBg] = Grey(0);
    c[ImGuiCol_ChildBg] = Grey(0);
    c[ImGuiCol_PopupBg] = Grey(20);
    c[ImGuiCol_Border] = Grey(58);
    c[ImGuiCol_BorderShadow] = Grey(0, 0.0f);
    c[ImGuiCol_Text] = Grey(255);
    c[ImGuiCol_TextDisabled] = Grey(140);
    c[ImGuiCol_FrameBg] = Grey(20);
    c[ImGuiCol_FrameBgHovered] = Grey(34);
    c[ImGuiCol_FrameBgActive] = Grey(34);
    c[ImGuiCol_Button] = Grey(20);
    c[ImGuiCol_ButtonHovered] = Grey(34);
    c[ImGuiCol_ButtonActive] = Grey(58);
    c[ImGuiCol_Header] = Grey(34);
    c[ImGuiCol_HeaderHovered] = Grey(34);
    c[ImGuiCol_HeaderActive] = Grey(58);
    c[ImGuiCol_SliderGrab] = Grey(255);
    c[ImGuiCol_SliderGrabActive] = Grey(255);
    c[ImGuiCol_CheckMark] = Grey(255);
    c[ImGuiCol_Separator] = Grey(58);
    c[ImGuiCol_ScrollbarBg] = Grey(0);
    c[ImGuiCol_ScrollbarGrab] = Grey(58);
    c[ImGuiCol_ScrollbarGrabHovered] = Grey(140);
    c[ImGuiCol_ScrollbarGrabActive] = Grey(255);
    c[ImGuiCol_TextSelectedBg] = Grey(140, 0.5f);

    style.WindowRounding = 8.0f;
    style.ChildRounding = 0.0f;
    style.FrameRounding = 4.0f;
    style.GrabRounding = 4.0f;
    style.PopupRounding = 4.0f;
    style.WindowBorderSize = 1.0f;
    style.ChildBorderSize = 0.0f;
    style.WindowPadding = ImVec2(0.0f, 0.0f);
    style.FramePadding = ImVec2(8.0f, 5.0f);
    style.ItemSpacing = ImVec2(8.0f, 8.0f);
    style.GrabMinSize = 12.0f;
    style.ScaleAllSizes(dpiScale);
    style.FontScaleDpi = dpiScale;
}

void LoadMenuFonts(float) {
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();
    ImFont* text = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeui.ttf", kFontPixels);
    if (!text) {
        io.Fonts->AddFontDefault();
        return;
    }
    ImFontConfig merge;
    merge.MergeMode = true;
    merge.GlyphRanges = kIconRange;
    io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segmdl2.ttf", kFontPixels, &merge);
}
