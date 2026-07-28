#pragma once

// CRITICAL: Must be defined before ImGui is loaded
#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif

#include "imgui.h"
#include "imgui_internal.h"
#include <algorithm>

inline bool PremiumButton(const char* label, const ImVec2& size_arg) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return false;

    ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = g.Style;
    const ImGuiID id = window->GetID(label);
    const ImVec2 label_size = ImGui::CalcTextSize(label, NULL, true);

    ImVec2 pos = window->DC.CursorPos;
    ImVec2 size = ImGui::CalcItemSize(size_arg, label_size.x + style.FramePadding.x * 2.0f, label_size.y + style.FramePadding.y * 2.0f);
    const ImRect bb(pos, pos + size);
    ImGui::ItemSize(size, style.FramePadding.y);

    if (!ImGui::ItemAdd(bb, id)) return false;

    bool hovered, held;
    bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held, 0);
    ImU32 col_top, col_bot, border_col;

    if (g.CurrentItemFlags & ImGuiItemFlags_Disabled) {
        col_top = IM_COL32(30, 30, 35, 255); col_bot = IM_COL32(15, 15, 18, 255); border_col = IM_COL32(50, 50, 60, 255);
    }
    else if (held) {
        col_top = IM_COL32(140, 50, 220, 255); col_bot = IM_COL32(90, 20, 150, 255); border_col = IM_COL32(190, 100, 255, 255);
    }
    else if (hovered) {
        col_top = IM_COL32(110, 30, 180, 255); col_bot = IM_COL32(60, 15, 100, 255); border_col = IM_COL32(150, 50, 220, 255);
    }
    else {
        col_top = IM_COL32(40, 20, 60, 200); col_bot = IM_COL32(20, 10, 30, 200); border_col = IM_COL32(80, 40, 120, 150);
    }

    window->DrawList->AddRectFilledMultiColor(bb.Min, bb.Max, col_top, col_top, col_bot, col_bot);
    window->DrawList->AddRect(bb.Min, bb.Max, border_col, 4.0f);

    if (g.CurrentItemFlags & ImGuiItemFlags_Disabled) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.55f, 1.0f));
        ImGui::RenderTextClipped(bb.Min + style.FramePadding, bb.Max - style.FramePadding, label, NULL, &label_size, style.ButtonTextAlign, &bb);
        ImGui::PopStyleColor();
    }
    else {
        ImGui::RenderTextClipped(bb.Min + style.FramePadding, bb.Max - style.FramePadding, label, NULL, &label_size, style.ButtonTextAlign, &bb);
    }

    return pressed;
}

inline bool PremiumToggle(const char* label, bool* v) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return false;

    ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = g.Style;
    const ImGuiID id = window->GetID(label);
    const ImVec2 label_size = ImGui::CalcTextSize(label, NULL, true);

    float h = ImGui::GetFrameHeight() * 0.65f;
    float w = h * 2.1f;
    float r = h * 0.50f;
    ImVec2 pos = window->DC.CursorPos;
    float total_w = (label_size.x > 0.0f ? label_size.x + style.ItemInnerSpacing.x : 0.0f) + w;
    float frame_h = ImMax(label_size.y, h) + style.FramePadding.y * 2.0f;
    ImRect total_bb(pos, pos + ImVec2(total_w, frame_h));

    ImGui::ItemSize(total_bb, style.FramePadding.y);
    if (!ImGui::ItemAdd(total_bb, id)) return false;

    bool hovered, held;
    bool pressed = ImGui::ButtonBehavior(total_bb, id, &hovered, &held);
    if (pressed) *v = !(*v);

    float* t_ptr = window->StateStorage.GetFloatRef(id, *v ? 1.0f : 0.0f);
    float t = *t_ptr;
    float target_t = *v ? 1.0f : 0.0f;

    if (t != target_t) {
        float speed = ImGui::GetIO().DeltaTime * 12.0f;
        if (target_t > t) t = ImMin(target_t, t + speed);
        else t = ImMax(target_t, t - speed);
        *t_ptr = t;
    }

    if (label_size.x > 0.0f) {
        ImGui::RenderText(ImVec2(pos.x, pos.y + (frame_h - label_size.y) * 0.5f), label);
    }

    float tx = pos.x + (label_size.x > 0.0f ? label_size.x + style.ItemInnerSpacing.x : 0.0f);
    float ty = pos.y + (frame_h - h) * 0.5f;

    window->DrawList->AddRectFilled(ImVec2(tx, ty), ImVec2(tx + w, ty + h), (t > 0.5f) ? IM_COL32(120, 40, 200, 255) : IM_COL32(50, 50, 60, 255), r);
    window->DrawList->AddCircleFilled(ImVec2(tx + r + t * (w - r * 2.0f), ty + r), r - 1.0f, IM_COL32(255, 255, 255, 255));

    return pressed;
}

inline void PremiumGauge(const char* label, float fraction, const char* centerText, float radius) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImVec2 center = ImVec2(pos.x + radius, pos.y + radius);
    float thick = radius * 0.25f;

    float PI = 3.1415926535f;
    float start_a = PI * 0.75f;
    float range = PI * 1.50f;

    dl->PathArcTo(center, radius, start_a, start_a + range, 40);
    dl->PathStroke(IM_COL32(30, 30, 35, 255), 0, thick);

    if (fraction > 0.005f) {
        int total = 100;
        int active = (int)(total * fraction);
        if (active < 1) active = 1;

        float step = range / total;
        float overlap = step * 1.2f;

        ImVec4 start_v = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
        ImVec4 end_v = ImVec4(0.65f, 0.35f, 1.00f, 1.0f);

        for (int i = 0; i < active; i++) {
            float a0 = start_a + step * i;
            float a1 = a0 + step + overlap;

            float blend = ImClamp((float)i / (total * 0.15f), 0.0f, 1.0f);
            ImU32 col = ImGui::ColorConvertFloat4ToU32(ImVec4(
                start_v.x + (end_v.x - start_v.x) * blend,
                start_v.y + (end_v.y - start_v.y) * blend,
                start_v.z + (end_v.z - start_v.z) * blend,
                1.0f
            ));

            dl->PathArcTo(center, radius, a0, a1, 4);
            dl->PathStroke(col, 0, thick);
        }
    }

    ImVec2 ts = ImGui::CalcTextSize(centerText);
    dl->AddText(ImVec2(center.x - ts.x * 0.5f, center.y - ts.y * 0.5f), IM_COL32(255, 255, 255, 255), centerText);

    ImVec2 ls = ImGui::CalcTextSize(label);
    dl->AddText(ImVec2(center.x - ls.x * 0.5f, center.y + radius + 10.0f), IM_COL32(180, 180, 190, 255), label);

    ImGui::Dummy(ImVec2(radius * 2.0f, radius * 2.0f + 40.0f));
}