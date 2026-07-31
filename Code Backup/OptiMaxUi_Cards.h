#pragma once

#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif

#include "imgui.h"
#include "imgui_internal.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <string>
#include <vector>

// ==========================================================================
// DASHBOARD WIDGETS (v4.3.0 UI pass)
//
// The pieces the agreed mockup is built from. Every one of them is plain
// draw-list work - rects, arcs, quads, text - because that is the whole reason
// the mockup was constrained to those primitives: what was designed in the
// browser had to be buildable here without a render-target rework.
//
// CONVENTIONS TAKEN FROM OptiMaxUi_Widgets.h RATHER THAN INVENTED:
//   - animation uses window->StateStorage.GetFloatRef(id, ...) and lerps toward
//     a target each frame, exactly as PremiumToggle already does. ImGui has no
//     CSS transition; this is the pattern the codebase already chose.
//   - hover that must NOT consume clicks uses IsMouseHoveringRect, so a row can
//     highlight and show a tooltip while the switch inside it stays clickable.
//     Adding an item id for the whole row would swallow the switch's input.
//
// WHY A SEPARATE FILE. OptiMaxUi_Widgets.h holds the three primitives the whole
// app uses (button, toggle, gauge). These are composites specific to the
// dashboard, and keeping them apart means a change here cannot break a control
// that appears on forty other rows.
// ==========================================================================

namespace OptiMaxUi {

    // ---- icons -----------------------------------------------------------
    // 🔴 WRITTEN AS EXPLICIT UTF-8 BYTES, NOT u8"..." LITERALS, AND THAT MATTERS.
    //
    // These sources have no UTF-8 BOM, so MSVC reads them in the system codepage
    // (CP1252 here). A u8"" literal containing a real multi-byte character is
    // then decoded as CP1252 and re-encoded, and what reaches the screen is
    // mojibake - the first build of this UI rendered the CPU icon as "a-%o" and
    // the microsecond sign as "Aus".
    //
    // /utf-8 on the compiler would also fix it, but it changes how EVERY literal
    // in the project is interpreted, and a single genuinely CP1252-encoded file
    // anywhere would start producing garbage instead. Hex escapes are immune to
    // the file's encoding entirely, which is the property worth having.
    #define OMX_ICON_CPU      "\xE2\x97\x89"   // U+25C9 fisheye
    #define OMX_ICON_MEM      "\xE2\x96\xA4"   // U+25A4 square with horizontal fill
    #define OMX_ICON_CLOCK    "\xE2\x97\xB7"   // U+25F7 circle with upper right quadrant
    #define OMX_ICON_TIMER    "\xE2\x8F\xB1"   // U+23F1 stopwatch
    #define OMX_ICON_WARN     "\xE2\x96\xB2"   // U+25B2 up-pointing triangle
    #define OMX_ICON_BOLT     "\xE2\x9A\xA1"   // U+26A1 high voltage
    #define OMX_ICON_PLAY     "\xE2\x96\xB6"   // U+25B6 right-pointing triangle
    #define OMX_ICON_GEAR     "\xE2\x9A\x99"   // U+2699 gear
    #define OMX_ICON_SPARK    "\xE2\x9C\xA6"   // U+2726 black four pointed star
    #define OMX_ICON_DIAMOND  "\xE2\x97\x88"   // U+25C8 white diamond containing black
    #define OMX_ICON_CLOUD    "\xE2\x98\x81"   // U+2601 cloud
    #define OMX_ICON_LIST     "\xE2\x98\xB0"   // U+2630 trigram for heaven
    #define OMX_ICON_FLASK    "\xE2\x9A\x97"   // U+2697 alembic
    #define OMX_ICON_GRID     "\xE2\x96\x9A"   // U+259A quadrant upper-left lower-right
    #define OMX_MICRO         "\xC2\xB5"       // U+00B5 micro sign


    // ---- palette, matching the agreed mockup exactly -------------------
    // Kept here as one block so a colour change is one edit rather than a hunt.
    // These are the same values the mockup's CSS variables carry.
    inline const ImU32 kCard = IM_COL32(21, 15, 35, 255);      // #150F23
    inline const ImU32 kCardHi = IM_COL32(34, 24, 56, 255);      // #221838
    inline const ImU32 kLine = IM_COL32(42, 31, 66, 255);      // #2A1F42
    inline const ImU32 kLineHi = IM_COL32(74, 46, 138, 255);     // #4A2E8A
    inline const ImU32 kInk = IM_COL32(241, 238, 249, 255);
    inline const ImU32 kDim = IM_COL32(140, 131, 163, 255);
    inline const ImU32 kDimmer = IM_COL32(91, 83, 114, 255);
    inline const ImU32 kAccent = IM_COL32(139, 69, 240, 255);     // #8B45F0
    inline const ImU32 kAccentBr = IM_COL32(169, 108, 255, 255);    // #A96CFF
    inline const ImU32 kAccentHot = IM_COL32(199, 155, 255, 255);    // #C79BFF
    inline const ImU32 kCalm = IM_COL32(61, 214, 140, 255);     // #3DD68C
    inline const ImU32 kWarn = IM_COL32(245, 165, 36, 255);     // #F5A524
    inline const ImU32 kRed = IM_COL32(248, 113, 113, 255);

    // Per-metric trace colours. Chosen to stay in the same value range as the
    // accent so none of them shouts louder than the others, and to be
    // distinguishable without relying on hue alone being read correctly - each
    // sits at a different lightness too, so they survive a colourblind viewer
    // and a bad monitor.
    inline const ImU32 kSigCpu = IM_COL32(94, 200, 255, 255);   // cyan
    inline const ImU32 kSigMem = IM_COL32(61, 214, 140, 255);   // green
    inline const ImU32 kSigDpc = IM_COL32(245, 165, 36, 255);   // amber
    inline const ImU32 kSigStall = IM_COL32(248, 113, 113, 255);   // red

    inline ImU32 Fade(ImU32 c, float a) {
        return (c & 0x00FFFFFF) | ((ImU32)(255.0f * a) << 24);
    }

    // ---- metric history -------------------------------------------------
    // The one piece of genuinely new plumbing in the redesign. The app samples
    // CPU, memory and DPC every telemetry tick and keeps nothing, so there was
    // no past to draw. 60 floats is a minute at 1Hz and costs nothing.
    //
    // Deliberately a fixed array rather than a deque: this is written from the
    // telemetry thread and read from the UI thread every frame, and a container
    // that reallocates would be a data race waiting to happen. A fixed buffer
    // with an atomic head is safe enough for drawing - a frame that catches a
    // half-written sample draws one pixel wrong and nobody can see it.
    template <int N>
    struct MetricHistory {
        float v[N] = {};
        int head = 0;
        int filled = 0;

        void Push(float x) {
            v[head] = x;
            head = (head + 1) % N;
            if (filled < N) filled++;
        }
        // Oldest-first, so the caller can just walk it left to right.
        void Snapshot(float* out, int& count) const {
            count = filled;
            const int start = (filled < N) ? 0 : head;
            for (int i = 0; i < filled; ++i) out[i] = v[(start + i) % N];
        }
    };

    // ---- sparkline ------------------------------------------------------
    // Filled area plus a stroked line. The fill is drawn as one quad per
    // segment rather than a single polygon because AddConvexPolyFilled requires
    // convexity and the area under a wobbly curve is not convex - a trapezoid
    // per segment always is.
    inline void Sparkline(ImDrawList* dl, ImVec2 p0, ImVec2 size,
        const float* vals, int count, float vmax,
        ImU32 stroke, ImU32 fill, float thickness = 1.7f) {
        if (count < 2) return;

        // AUTO-SCALE WHEN vmax IS NOT GIVEN, and this matters more than it looks.
        // Drawn against a fixed 0-100 scale, an idle machine's 9% CPU trace is a
        // flat line pinned to the bottom of the band - which is what the first
        // build showed, and it read as "the graph is broken" rather than "the CPU
        // is quiet". The absolute figure is already printed above in 34px type;
        // the sparkline's job is shape, so it scales to what it actually contains.
        //
        // The headroom stops a dead-flat series from filling the band, and the
        // floor stops pure noise being amplified into a mountain range.
        //
        // 🔴 THIS NUMBER HAS BEEN WRONG IN BOTH DIRECTIONS. 1.30x put a steady
        // 30% memory trace near the top of the band, and since the fill runs
        // from the line DOWN to the baseline it read as a solid block rather
        // than a graph. 2.20x then over-corrected: a trace at its own maximum
        // reached only 45% of the band, so with the band at the card's bottom
        // edge the whole plot lived in a few pixels above the border and was
        // reported as invisible. 1.55x puts a peak at ~65% of the band - high
        // enough to read as a graph, low enough that the fill stays a wash.
        //
        // The floor stops a flat-zero series being amplified into noise.
        float scale = vmax;
        if (scale <= 0.0f) {
            float mx = 0.0f;
            for (int i = 0; i < count; ++i) mx = ImMax(mx, vals[i]);
            scale = ImMax(mx * 1.55f, 1.0f);
        }

        // A floor under the trace, so a zero sample sits ON the baseline rather
        // than a pixel below it where the card border eats it.
        const float usable = ImMax(size.y - 2.0f, 1.0f);
        const float dx = size.x / (float)(count - 1);
        auto Y = [&](int i) {
            const float f = ImClamp(vals[i] / scale, 0.0f, 1.0f);
            return p0.y + size.y - 1.0f - f * usable;
            };

        for (int i = 0; i < count - 1; ++i) {
            const float x0 = p0.x + dx * i, x1 = p0.x + dx * (i + 1);
            dl->AddQuadFilled(ImVec2(x0, Y(i)), ImVec2(x1, Y(i + 1)),
                ImVec2(x1, p0.y + size.y), ImVec2(x0, p0.y + size.y), fill);
        }
        // Stroke as one polyline so joins are handled by ImGui rather than by
        // stacking N separate segments, which shows as notches at every vertex.
        ImVec2 pts[256];
        const int n = ImMin(count, 256);
        for (int i = 0; i < n; ++i) pts[i] = ImVec2(p0.x + dx * i, Y(i));
        dl->AddPolyline(pts, n, stroke, 0, thickness);
    }

    // ---- modal styling ---------------------------------------------------
    // 🔴 MODALS READ WindowRounding AND WindowBorderSize, *NOT* THE POPUP
    // EQUIVALENTS. imgui.cpp:7763 excludes modal windows from the PopupRounding
    // branch (it requires !Modal), so setting PopupRounding rounds tooltips and
    // dropdowns and leaves every modal square - which is exactly what the first
    // attempt at this did. Cost three builds to find.
    //
    // PUSHED PER DIALOG, NEVER SET GLOBALLY. The main window is fullscreen and
    // borderless: a global WindowRounding would round its corners against the
    // screen edge, and a global WindowBorderSize would draw an outline around
    // the whole application.
    //
    // 🔴 AND WHY THIS IS AN OBJECT RATHER THAN TWO LINES YOU TYPE EACH TIME.
    // The push happens every frame, but BeginPopupModal only returns true on the
    // frames the dialog is OPEN - so the matching pop has to sit OUTSIDE the
    // if-block, which is not where anyone's fingers put it. Get it wrong and one
    // stack entry leaks per frame, and ImGui asserts at shutdown rather than at
    // the site, so the report points nowhere near the cause. A destructor cannot
    // be put in the wrong place.
    //
    //     {
    //         OptiMaxUi::ModalStyle _ms;
    //         if (ImGui::BeginPopupModal("Name", ...)) { ...; ImGui::EndPopup(); }
    //     }
    //
    // Radius 10, matching ChildRounding. The theory that a large panel needs a
    // smaller radius than a card to read as equally rounded was wrong on screen:
    // 8 looked sharper than the cards rather than matched to them. Settled at 10
    // by the owner's eye. The border colour is deliberately not overridden -
    // ImGuiCol_Border is already the value every dashboard card uses.
    struct ModalStyle {
        ModalStyle() {
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
        }
        ~ModalStyle() { ImGui::PopStyleVar(2); }
        ModalStyle(const ModalStyle&) = delete;
        ModalStyle& operator=(const ModalStyle&) = delete;
    };

    // ---- centred icon ----------------------------------------------------
    // 🔴 CalcTextSize CANNOT CENTRE A GLYPH IN A BOX, and every icon tile in
    // this UI was drawn as if it could.
    //
    // CalcTextSize returns the ADVANCE width and the FULL LINE HEIGHT. The
    // advance includes each glyph's side bearings, which for these Segoe UI
    // Symbol shapes are not symmetric, and the line height covers ascent plus
    // descent - space most of which no icon glyph occupies. Centring the
    // advance box therefore centres the metrics rather than the ink, and every
    // icon sat low and slightly off to one side. It looked like a rounding
    // error, which is why it survived: each glyph was wrong by a different
    // amount, so no single offset would have fixed them.
    //
    // The baked glyph carries its real ink box (X0,Y0)-(X1,Y1) in pixels at
    // the current size, already relative to the text draw position. Centring
    // THAT is exact, and self-corrects when the font size changes.
    inline void DrawIconCentered(ImDrawList* dl, ImVec2 boxMin, ImVec2 boxMax,
        const char* icon, ImU32 col) {
        const ImVec2 c((boxMin.x + boxMax.x) * 0.5f, (boxMin.y + boxMax.y) * 0.5f);

        unsigned int cp = 0;
        ImTextCharFromUtf8(&cp, icon, icon + strlen(icon));
        ImFontBaked* baked = ImGui::GetFontBaked();
        const ImFontGlyph* g = baked ? baked->FindGlyphNoFallback((ImWchar)cp) : nullptr;

        // Fall back to advance-box centring rather than drawing nothing: a
        // slightly off-centre icon beats a missing one, and this is the path a
        // machine without the symbol face takes.
        if (!g || !g->Visible) {
            const ImVec2 ts = ImGui::CalcTextSize(icon);
            dl->AddText(ImVec2(c.x - ts.x * 0.5f, c.y - ts.y * 0.5f), col, icon);
            return;
        }
        dl->AddText(ImVec2(c.x - (g->X0 + g->X1) * 0.5f,
            c.y - (g->Y0 + g->Y1) * 0.5f), col, icon);
    }

    // ---- status chip ----------------------------------------------------
    enum class ChipKind { On, Off, Pending, Advanced, Unavailable };

    // `fontSize` of 0 means "the current font's size". A chip is a label ABOUT
    // a row, never the row's own content, so inside a dense list it is drawn
    // below the body size - at full size the pills competed with the titles
    // they were annotating.
    inline void DrawChip(ImDrawList* dl, ImVec2 pos, const char* text, ChipKind kind,
        float fontSize = 0.0f) {
        ImU32 fg = kDim, bg = 0, br = kLine;
        switch (kind) {
        case ChipKind::On:          fg = kAccentHot; bg = Fade(kAccent, 0.16f); br = Fade(kAccentHot, 0.45f); break;
        case ChipKind::Pending:     fg = kWarn;      bg = Fade(kWarn, 0.10f);   br = Fade(kWarn, 0.45f);      break;
        case ChipKind::Advanced:    fg = kRed;       bg = Fade(kRed, 0.09f);    br = Fade(kRed, 0.40f);       break;
        case ChipKind::Unavailable: fg = kDimmer;    bg = 0;                    br = kLine;                   break;
        case ChipKind::Off:         fg = kDim;       bg = 0;                    br = kLine;                   break;
        }
        ImFont* font = ImGui::GetFont();
        const float fs = (fontSize > 0.0f) ? fontSize : ImGui::GetFontSize();
        const ImVec2 ts = font->CalcTextSizeA(fs, FLT_MAX, 0.0f, text);
        const ImVec2 pad(9.0f, 3.0f);
        const ImVec2 a = pos, b = ImVec2(pos.x + ts.x + pad.x * 2, pos.y + ts.y + pad.y * 2);
        const float r = (b.y - a.y) * 0.5f;   // true pill: radius is half the height
        if (bg) dl->AddRectFilled(a, b, bg, r);
        dl->AddRect(a, b, br, r);
        dl->AddText(font, fs, ImVec2(a.x + pad.x, a.y + pad.y), fg, text);
    }

    inline float ChipWidth(const char* text, float fontSize = 0.0f) {
        ImFont* font = ImGui::GetFont();
        const float fs = (fontSize > 0.0f) ? fontSize : ImGui::GetFontSize();
        return font->CalcTextSizeA(fs, FLT_MAX, 0.0f, text).x + 18.0f;
    }

    inline float ChipHeight(float fontSize = 0.0f) {
        ImFont* font = ImGui::GetFont();
        const float fs = (fontSize > 0.0f) ? fontSize : ImGui::GetFontSize();
        return font->CalcTextSizeA(fs, FLT_MAX, 0.0f, "M").y + 6.0f;
    }

    // ---- toggle-row metrics ---------------------------------------------
    // 🔴 ONE SOURCE FOR THE ROW HEIGHT, because there were three.
    //
    // ToggleRow hardcoded 54, and BOTH callers sized their card as
    // rows * 56 - a widget constant and a caller constant that have to agree
    // and had no way to. That is the shape of the bug that already lost the
    // dashboard's fourth row once. The callers now ask.
    //
    // The row draws its own text BELOW the body size. At the 23px base these
    // rows are a dense list, not prose: two full-size lines plus padding made
    // each row taller than the icon tile beside it, which is what made the
    // controls card look oversized.
    inline float ToggleRowTitleSize() { return ImGui::GetFontSize() * 0.85f; }
    inline float ToggleRowSubSize() { return ImGui::GetFontSize() * 0.74f; }
    inline float ToggleRowHeight() { return ToggleRowTitleSize() + ToggleRowSubSize() + 15.0f; }
    inline float ToggleRowPitch() { return ToggleRowHeight() + 2.0f; }

    // ---- KPI tile -------------------------------------------------------
    // icon + label + big value + delta line + sparkline along the bottom.
    // fontNumber may be null; it falls back to the current font rather than
    // skipping the value, because a missing font should cost emphasis and not
    // the reading.
    inline void KpiTile(const char* icon, const char* label,
        const char* value, const char* unit,
        const char* delta, ImU32 deltaCol,
        const float* hist, int histCount, float histMax,
        ImVec2 size, ImFont* fontNumber, bool warnTint = false,
        ImU32 lineCol = 0) {
        ImGuiWindow* win = ImGui::GetCurrentWindow();
        ImDrawList* dl = win->DrawList;
        const ImVec2 p = win->DC.CursorPos;
        const ImRect bb(p, p + size);

        ImGui::ItemSize(size);
        const ImGuiID id = win->GetID(label);
        if (!ImGui::ItemAdd(bb, id)) return;

        const bool hov = ImGui::IsItemHovered();
        const float rounding = ImGui::GetStyle().ChildRounding;

        dl->AddRectFilled(bb.Min, bb.Max, hov ? kCardHi : kCard, rounding);
        dl->AddRect(bb.Min, bb.Max, hov ? kLineHi : kLine, rounding);

        // Sparkline first, so text sits on top of it. Clipped to the tile so the
        // fill cannot bleed past the rounded corner.
        // 🔴 LAYOUT IS COMPUTED FROM FONT METRICS, NOT FROM CONSTANTS.
        //
        // The first version placed the label at a fixed padding, the value a
        // fixed offset below it, and the sparkline in a fixed 38px band at the
        // bottom - all tuned against the mockup's 13.5px type. This app runs a
        // 20px base and a 34px number face, so the text block was half again as
        // tall as assumed: the delta line landed inside the sparkline band, the
        // graph was squashed into whatever was left, and the whole tile read as
        // top-heavy with dead space under it.
        //
        // Now: the sparkline gets a reserved band, and the text block is measured
        // and centred in the space that remains. Changing font size cannot break
        // it again.
        // The band was capped at 34px, which on a 148px tile is 23% of it - and
        // with the trace only reaching part way up that band, the graph ended up
        // as a few pixels hugging the card's bottom border. Given a proper share
        // of the tile it reads as a chart again.
        const float padL = 15.0f;
        const float sparkBand = ImMin(52.0f, size.y * 0.34f);
        const float textTop = bb.Min.y;
        const float textBot = bb.Max.y - sparkBand;

        const float lineH = ImGui::GetTextLineHeight();
        if (fontNumber) ImGui::PushFont(fontNumber);
        const ImVec2 vs = ImGui::CalcTextSize(value);
        if (fontNumber) ImGui::PopFont();

        const bool hasDelta = (delta && delta[0]);
        const float blockH = lineH + 2.0f + vs.y + (hasDelta ? (2.0f + lineH) : 0.0f);

        // TOP-ALIGNED, matching the mockup. Centring the block vertically was my
        // idea and it was wrong twice over: it left the label floating in the
        // middle of the card, and it dragged the icon tile down with it because the
        // icon was positioned off the label rather than off the card. The mockup
        // pins both to the top edge and lets the sparkline own the bottom.
        const float padT = 14.0f;
        float y = textTop + padT;
        // If the type is ever large enough that the block would collide with the
        // sparkline band, give it the room rather than overlapping - the graph is
        // the thing that can afford to lose a few pixels.
        const float overflow = (y + blockH) - textBot;
        if (overflow > 0.0f) y = ImMax(textTop + 6.0f, y - overflow);

        // Icon pinned to the card's top-right corner, not to the label.
        {
            const float s = 31.0f;
            const ImVec2 a(bb.Max.x - padL - s, bb.Min.y + padT - 1.0f);
            const ImVec2 b(a.x + s, a.y + s);
            const ImU32 ib = warnTint ? Fade(kWarn, hov ? 0.28f : 0.14f) : Fade(kAccent, hov ? 0.32f : 0.16f);
            const ImU32 ibr = warnTint ? Fade(kWarn, 0.35f) : (hov ? kAccentHot : Fade(kAccentBr, 0.30f));
            dl->AddRectFilled(a, b, ib, 10.0f);
            dl->AddRect(a, b, ibr, 10.0f);
            DrawIconCentered(dl, a, b, icon, warnTint ? kWarn : kAccentHot);
        }

        dl->AddText(ImVec2(bb.Min.x + padL, y), kDim, label);
        y += lineH + 2.0f;

        if (fontNumber) ImGui::PushFont(fontNumber);
        dl->AddText(ImVec2(bb.Min.x + padL, y), kInk, value);
        if (fontNumber) ImGui::PopFont();
        if (unit && unit[0]) {
            // Sat on the value's baseline, not its top - a unit hanging off the
            // cap height is what made "9 %" look detached from the 9.
            dl->AddText(ImVec2(bb.Min.x + padL + vs.x + 3.0f, y + vs.y - lineH - 1.0f), kDim, unit);
        }
        y += vs.y + 2.0f;
        if (hasDelta) dl->AddText(ImVec2(bb.Min.x + padL, y), deltaCol, delta);

        // Sparkline last, in its reserved band, inset by the corner radius.
        // PushClipRect is rectangular - ImGui has no rounded clip - so a plot
        // drawn to the card's edge fills the square area behind the rounded
        // corner and pokes through it. CSS gets this free with overflow:hidden.
        if (hist && histCount > 1) {
            const float inset = rounding;
            const float w = size.x - inset * 2.0f;
            if (w > 8.0f) {
                dl->PushClipRect(ImVec2(bb.Min.x + inset, textBot),
                    ImVec2(bb.Max.x - inset, bb.Max.y - 1.5f), true);
                // Fill kept faint and the line prominent. At 0.28 alpha a steady
                // metric looked like a filled block; the line is what carries the
                // information, the wash is only there to give it a floor.
                //
                // Per-metric line colour, because five purple traces in a row read
                // as decoration. Giving each metric its own hue makes the tile
                // identifiable at a glance and lets the same colour label that
                // metric anywhere else it appears - the palette becomes a legend
                // rather than a theme.
                const ImU32 base = lineCol ? lineCol : (warnTint ? kWarn : kAccentBr);
                const ImU32 st = base;
                const ImU32 fl = Fade(base, hov ? 0.20f : 0.13f);
                Sparkline(dl, ImVec2(bb.Min.x + inset, textBot), ImVec2(w, sparkBand),
                    hist, histCount, histMax, st, fl, 1.9f);
                dl->PopClipRect();
            }
        }
    }

    // ---- segmented ring dial --------------------------------------------
    // 🎯 ONE ARC PER ITEM, NOT ONE SWEEP FOR ALL OF THEM.
    //
    // The plain ring showed a percentage and nothing else, so next to a legend
    // that carried every actual fact it was decoration - a second rendering of
    // a number already printed in its middle. Segmenting it makes the ring the
    // legend: N arcs for N rows, the same colour per state, and hovering an
    // arc names the tweak it stands for. The percentage stays in the centre
    // because that is the one thing the arcs cannot say at a glance.
    //
    // This is also what "more interactive, more hoverable" actually looks like
    // on a dial - the ring gained a reason to be hovered rather than a hover
    // effect bolted onto it.
    struct RingSegment {
        const char* label;
        const char* state;   // "On" / "Off" / "Reboot" - shown in the tooltip
        ImU32       col;
    };

    inline void SegmentedRing(const char* id, const RingSegment* segs, int count,
        const char* value, const char* unit,
        float radius, ImFont* fontValue) {
        if (count <= 0) return;
        ImGuiWindow* win = ImGui::GetCurrentWindow();
        ImDrawList* dl = win->DrawList;

        const float thick = radius * 0.19f;
        const ImVec2 total(radius * 2.0f, radius * 2.0f);
        const ImVec2 p = win->DC.CursorPos;
        const ImRect bb(p, ImVec2(p.x + total.x, p.y + total.y));

        ImGui::ItemSize(total);
        const ImGuiID wid = win->GetID(id);
        if (!ImGui::ItemAdd(bb, wid)) return;

        const ImVec2 c(p.x + radius, p.y + radius);
        const float PI = 3.14159265f;
        const float rMid = radius - thick * 0.5f;
        const float step = PI * 2.0f / (float)count;

        // Which arc is under the mouse, from angle and distance rather than
        // from N invisible buttons: ImGui's hit boxes are rectangles and these
        // are wedges, so a button per segment would claim the wrong areas at
        // the corners and miss the thin ends.
        int hovIdx = -1;
        if (ImGui::IsItemHovered()) {
            const ImVec2 m = ImGui::GetIO().MousePos;
            const float dx = m.x - c.x, dy = m.y - c.y;
            const float d = sqrtf(dx * dx + dy * dy);
            if (d >= radius - thick * 1.7f && d <= radius + thick * 0.4f) {
                float a = atan2f(dy, dx) + PI * 0.5f;   // rebased so 0 is 12 o'clock
                while (a < 0.0f)         a += PI * 2.0f;
                while (a >= PI * 2.0f)   a -= PI * 2.0f;
                hovIdx = ImClamp((int)(a / step), 0, count - 1);
            }
        }

        // Gap between arcs, capped so a ring of many segments does not become
        // more gap than arc.
        const float gap = ImMin(step * 0.18f, 0.12f);

        for (int i = 0; i < count; ++i) {
            const float a0 = -PI * 0.5f + step * (float)i + gap * 0.5f;
            const float a1 = -PI * 0.5f + step * (float)(i + 1) - gap * 0.5f;

            // Per-segment hover lerp, the same StateStorage pattern
            // PremiumToggle uses - ImGui has no CSS transition.
            const ImGuiID sid = wid ^ (ImGuiID)((i + 1) * 7919);
            float* t = win->StateStorage.GetFloatRef(sid, 0.0f);
            const float target = (i == hovIdx) ? 1.0f : 0.0f;
            const float sp = ImGui::GetIO().DeltaTime * 10.0f;
            if (*t < target) *t = ImMin(target, *t + sp);
            else if (*t > target) *t = ImMax(target, *t - sp);

            dl->PathArcTo(c, rMid, a0, a1, 24);
            dl->PathStroke(segs[i].col, 0, thick + (*t) * thick * 0.5f);
        }

        if (hovIdx >= 0) {
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(13, 11));
            ImGui::BeginTooltip();
            ImGui::TextUnformatted(segs[hovIdx].label);
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(segs[hovIdx].col));
            ImGui::TextUnformatted(segs[hovIdx].state);
            ImGui::PopStyleColor();
            ImGui::EndTooltip();
            ImGui::PopStyleVar();
        }

        // Centre value. Sized as one block so the unit hangs off the number's
        // baseline rather than floating beside its cap height.
        if (fontValue) ImGui::PushFont(fontValue, fontValue->LegacySize);
        const ImVec2 vs = ImGui::CalcTextSize(value);
        if (fontValue) ImGui::PopFont();
        const ImVec2 us = (unit && unit[0]) ? ImGui::CalcTextSize(unit) : ImVec2(0.0f, 0.0f);
        const float wTot = vs.x + us.x + 2.0f;
        const float iy = c.y - vs.y * 0.5f;

        if (fontValue) ImGui::PushFont(fontValue, fontValue->LegacySize);
        dl->AddText(ImVec2(c.x - wTot * 0.5f, iy), kInk, value);
        if (fontValue) ImGui::PopFont();
        if (us.x > 0.0f) {
            dl->AddText(ImVec2(c.x - wTot * 0.5f + vs.x + 2.0f, iy + vs.y - us.y - 2.0f), kDim, unit);
        }
    }

    // ---- metric dial ----------------------------------------------------
    // The Live Monitoring dial: ring, the reading inside it, then the metric's
    // name and a plain-language caption underneath.
    //
    // Separate from RingDial because the captions go OUTSIDE. RingDial puts its
    // name INSIDE the ring under the value, which is right for the single
    // optimisation dial it was built for and wrong for a row of five - inside
    // captions at five different lengths make five differently-crowded rings.
    //
    // arcCol is per metric and deliberately matches the same metric's sparkline
    // on the Dashboard and its trace in the history plot below. Five identical
    // purple rings read as decoration; a colour that means "this is CPU"
    // wherever it appears turns the palette into a legend.
    inline void MetricDial(const char* id, float fraction,
        const char* value, const char* unit,
        const char* name, const char* sub,
        float radius, ImFont* fontValue, ImU32 arcCol) {
        ImGuiWindow* win = ImGui::GetCurrentWindow();
        ImDrawList* dl = win->DrawList;
        ImFont* font = ImGui::GetFont();

        const float nameSz = ImGui::GetFontSize();
        const float subSz = ImGui::GetStyle().FontSizeBase * 0.80f;
        const float capGap = 10.0f;
        const float capH = nameSz + 2.0f + (sub && sub[0] ? subSz : 0.0f);

        const ImVec2 total(radius * 2.0f, radius * 2.0f + capGap + capH);
        const ImVec2 p = win->DC.CursorPos;
        const ImRect bb(p, ImVec2(p.x + total.x, p.y + total.y));

        ImGui::ItemSize(total);
        const ImGuiID wid = win->GetID(id);
        if (!ImGui::ItemAdd(bb, wid)) return;
        const bool hov = ImGui::IsItemHovered();

        // Animate the sweep, same StateStorage pattern PremiumToggle uses.
        float* t = win->StateStorage.GetFloatRef(wid, fraction);
        const float speed = ImGui::GetIO().DeltaTime * 6.0f;
        if (*t < fraction) *t = ImMin(fraction, *t + speed);
        else if (*t > fraction) *t = ImMax(fraction, *t - speed);
        const float f = ImClamp(*t, 0.0f, 1.0f);

        const ImVec2 c(p.x + radius, p.y + radius);
        const float PI = 3.14159265f;
        const float thick = radius * 0.17f + (hov ? 1.5f : 0.0f);

        dl->PathArcTo(c, radius - thick * 0.5f, 0.0f, PI * 2.0f, 64);
        dl->PathStroke(kLine, 0, thick);

        if (f > 0.002f) {
            dl->PathArcTo(c, radius - thick * 0.5f, -PI * 0.5f, -PI * 0.5f + PI * 2.0f * f, 64);
            dl->PathStroke(hov ? Fade(arcCol, 1.0f) : arcCol, 0, thick);
        }

        // Value inside, sized as one block so the unit hangs off the number's
        // baseline instead of floating beside its cap height.
        if (fontValue) ImGui::PushFont(fontValue, fontValue->LegacySize);
        const ImVec2 vs = ImGui::CalcTextSize(value);
        if (fontValue) ImGui::PopFont();
        const ImVec2 us = (unit && unit[0]) ? ImGui::CalcTextSize(unit) : ImVec2(0.0f, 0.0f);
        const float wTot = vs.x + us.x + 2.0f;
        const float iy = c.y - vs.y * 0.5f;

        if (fontValue) ImGui::PushFont(fontValue, fontValue->LegacySize);
        dl->AddText(ImVec2(c.x - wTot * 0.5f, iy), kInk, value);
        if (fontValue) ImGui::PopFont();
        if (us.x > 0.0f) {
            dl->AddText(ImVec2(c.x - wTot * 0.5f + vs.x + 2.0f, iy + vs.y - us.y - 2.0f), kDim, unit);
        }

        // Captions below the ring, each centred on the ring's own axis.
        float cy = p.y + radius * 2.0f + capGap;
        {
            const ImVec2 ns = font->CalcTextSizeA(nameSz, FLT_MAX, 0.0f, name);
            dl->AddText(font, nameSz, ImVec2(c.x - ns.x * 0.5f, cy), hov ? kInk : kDim, name);
            cy += nameSz + 2.0f;
        }
        if (sub && sub[0]) {
            const ImVec2 ss = font->CalcTextSizeA(subSz, FLT_MAX, 0.0f, sub);
            dl->AddText(font, subSz, ImVec2(c.x - ss.x * 0.5f, cy), kDimmer, sub);
        }
    }

    // ---- history plot ---------------------------------------------------
    // Several metrics over the same window, overlaid.
    //
    // ⚠️ MIXED UNITS ON ONE AXIS IS A LIE UNLESS YOU SAY SO. CPU and memory are
    // both percentages and share a real 0-100 axis, so comparing their heights
    // is meaningful. DPC latency is microseconds and cannot share that axis, so
    // it is scaled to its own peak - which means its height is NOT comparable
    // with the other two. The caller must say so in the legend; there is no way
    // for this widget to make an un-labelled mixed axis honest.
    struct HistorySeries {
        const float* vals = nullptr;
        int          count = 0;
        float        vmax = 0.0f;    // 0 = scale to this series' own peak
        ImU32        col = 0;
        bool         fill = false;   // area under the line, for the primary series
        const char* label = nullptr;
        const char* unit = nullptr;
    };

    inline void HistoryPlot(const char* id, const HistorySeries* series, int n,
        ImVec2 size) {
        ImGuiWindow* win = ImGui::GetCurrentWindow();
        ImDrawList* dl = win->DrawList;
        const ImVec2 p = win->DC.CursorPos;
        const ImRect bb(p, ImVec2(p.x + size.x, p.y + size.y));

        ImGui::ItemSize(size);
        const ImGuiID wid = win->GetID(id);
        if (!ImGui::ItemAdd(bb, wid)) return;
        const bool hov = ImGui::IsItemHovered();

        // Dashed gridlines at 25/50/75. ImGui has no dash pattern, so they are
        // drawn as short segments - cheap, and it keeps the rules from reading
        // as data.
        for (int g = 1; g <= 3; ++g) {
            const float y = bb.Min.y + size.y * (float)g * 0.25f;
            for (float x = bb.Min.x; x < bb.Max.x; x += 9.0f) {
                dl->AddLine(ImVec2(x, y), ImVec2(ImMin(x + 4.0f, bb.Max.x), y), kLine, 1.0f);
            }
        }

        // Longest series decides the x mapping, so a series that has not filled
        // its buffer yet still lines up in time with one that has.
        int maxCount = 0;
        for (int s = 0; s < n; ++s) maxCount = ImMax(maxCount, series[s].count);
        if (maxCount < 2) return;

        int hovIdx = -1;
        if (hov) {
            const float rel = (ImGui::GetIO().MousePos.x - bb.Min.x) / ImMax(size.x, 1.0f);
            hovIdx = ImClamp((int)(rel * (float)(maxCount - 1) + 0.5f), 0, maxCount - 1);
        }

        for (int s = 0; s < n; ++s) {
            const HistorySeries& hs = series[s];
            if (!hs.vals || hs.count < 2) continue;

            float scale = hs.vmax;
            if (scale <= 0.0f) {
                float mx = 0.0f;
                for (int i = 0; i < hs.count; ++i) mx = ImMax(mx, hs.vals[i]);
                scale = ImMax(mx * 1.15f, 1.0f);
            }

            // Right-aligned in time: the newest sample sits at the right edge
            // regardless of how full the buffer is, so "now" does not wander.
            const float dx = size.x / (float)(maxCount - 1);
            const int off = maxCount - hs.count;
            const float usable = ImMax(size.y - 2.0f, 1.0f);
            auto X = [&](int i) { return bb.Min.x + dx * (float)(off + i); };
            auto Y = [&](int i) {
                const float v = ImClamp(hs.vals[i] / scale, 0.0f, 1.0f);
                return bb.Max.y - 1.0f - v * usable;
                };

            if (hs.fill) {
                for (int i = 0; i < hs.count - 1; ++i) {
                    dl->AddQuadFilled(ImVec2(X(i), Y(i)), ImVec2(X(i + 1), Y(i + 1)),
                        ImVec2(X(i + 1), bb.Max.y), ImVec2(X(i), bb.Max.y),
                        Fade(hs.col, 0.16f));
                }
            }
            ImVec2 pts[64];
            const int cnt = ImMin(hs.count, 64);
            for (int i = 0; i < cnt; ++i) pts[i] = ImVec2(X(i), Y(i));
            dl->AddPolyline(pts, cnt, hs.col, 0, 2.0f);

            if (hovIdx >= off && hovIdx - off < hs.count) {
                dl->AddCircleFilled(ImVec2(X(hovIdx - off), Y(hovIdx - off)), 3.5f, hs.col);
            }
        }

        if (hovIdx >= 0) {
            const float hx = bb.Min.x + size.x * ((float)hovIdx / (float)(maxCount - 1));
            dl->AddLine(ImVec2(hx, bb.Min.y), ImVec2(hx, bb.Max.y), kLineHi, 1.0f);

            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(13, 11));
            ImGui::BeginTooltip();
            const int secondsAgo = (maxCount - 1) - hovIdx;
            if (secondsAgo == 0) ImGui::TextUnformatted("now");
            else ImGui::Text("%ds ago", secondsAgo);
            ImGui::Separator();
            for (int s = 0; s < n; ++s) {
                const HistorySeries& hs = series[s];
                const int off = maxCount - hs.count;
                if (!hs.vals || hovIdx < off || hovIdx - off >= hs.count) continue;
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(hs.col));
                ImGui::Text("%s  %.0f%s", hs.label ? hs.label : "",
                    hs.vals[hovIdx - off], hs.unit ? hs.unit : "");
                ImGui::PopStyleColor();
            }
            ImGui::EndTooltip();
            ImGui::PopStyleVar();
        }
    }

    // Colour swatch + label, for a plot legend. Advances the cursor so callers
    // can chain them with SameLine.
    inline void LegendKey(const char* label, ImU32 col) {
        ImGuiWindow* win = ImGui::GetCurrentWindow();
        ImDrawList* dl = win->DrawList;
        const float h = ImGui::GetTextLineHeight();
        const float sw = 10.0f;
        const ImVec2 p = win->DC.CursorPos;
        dl->AddRectFilled(ImVec2(p.x, p.y + (h - sw) * 0.5f),
            ImVec2(p.x + sw, p.y + (h + sw) * 0.5f), col, 2.0f);
        const ImVec2 ts = ImGui::CalcTextSize(label);
        dl->AddText(ImVec2(p.x + sw + 6.0f, p.y), kDim, label);
        ImGui::Dummy(ImVec2(sw + 6.0f + ts.x, h));
    }

    // ---- toggle row -----------------------------------------------------
    // icon tile, title, subtitle, state chip, switch - plus a hover tooltip
    // carrying the full explanation.
    //
    // The tooltip is the point of this widget, not decoration. Every tweak card
    // in the app currently holds a paragraph, which is what makes the Debloat
    // page a wall; moving the prose into a tooltip makes rows shorter AND lets
    // them say more, including the caveats that otherwise only exist in release
    // notes nobody reads.
    //
    // Returns true when the switch was clicked. `enabled == false` draws the row
    // greyed and swallows clicks - used for genuinely unavailable tweaks, which
    // must look different from merely-off ones.
    inline bool ToggleRow(const char* icon, const char* title, const char* subtitle,
        const char* chipText, ChipKind chipKind,
        bool* value, const char* tooltip, bool enabled = true,
        ImU32 subtitleCol = 0) {
        ImGuiWindow* win = ImGui::GetCurrentWindow();
        ImDrawList* dl = win->DrawList;

        const float rowH = ToggleRowHeight();
        const float titleSz = ToggleRowTitleSize();
        const float subSz = ToggleRowSubSize();
        ImFont* font = ImGui::GetFont();
        const float availW = ImGui::GetContentRegionAvail().x;
        const ImVec2 p = win->DC.CursorPos;
        const ImRect bb(p, ImVec2(p.x + availW, p.y + rowH));

        // IsMouseHoveringRect, not an item id: the switch below needs to keep
        // its own clicks, and claiming the whole row would eat them.
        const bool hov = ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(bb.Min, bb.Max);
        if (hov) dl->AddRectFilled(bb.Min, bb.Max, kCardHi, 12.0f);

        const float padL = 10.0f;
        // Tied to the row rather than fixed at 34, so the tile and the text
        // block it sits beside cannot drift apart at a different font size.
        const float icoS = ImFloor(rowH * 0.66f);
        const ImVec2 ia(bb.Min.x + padL, bb.Min.y + (rowH - icoS) * 0.5f);
        const ImVec2 ib(ia.x + icoS, ia.y + icoS);
        dl->AddRectFilled(ia, ib, Fade(kAccent, hov ? 0.30f : 0.15f), 11.0f);
        dl->AddRect(ia, ib, hov ? kAccentHot : Fade(kAccentBr, 0.30f), 11.0f);
        DrawIconCentered(dl, ia, ib, icon, enabled ? kAccentHot : kDimmer);

        const bool hasSub = (subtitle && subtitle[0]);
        const float textX = ib.x + 13.0f;
        const float blockH = hasSub ? (titleSz + subSz + 2.0f) : titleSz;
        const float textY = bb.Min.y + (rowH - blockH) * 0.5f;
        dl->AddText(font, titleSz, ImVec2(textX, textY), enabled ? kInk : kDim, title);
        if (hasSub) {
            dl->AddText(font, subSz, ImVec2(textX, textY + titleSz + 2.0f),
                subtitleCol ? subtitleCol : (hov ? kDim : kDimmer), subtitle);
        }

        // Switch on the right, chip immediately left of it.
        const float swW = 42.0f, swH = 22.0f;
        const ImVec2 sa(bb.Max.x - 10.0f - swW, bb.Min.y + (rowH - swH) * 0.5f);

        if (chipText && chipText[0]) {
            const float cw = ChipWidth(chipText, subSz);
            const float ch = ChipHeight(subSz);
            DrawChip(dl, ImVec2(sa.x - 12.0f - cw, bb.Min.y + (rowH - ch) * 0.5f),
                chipText, chipKind, subSz);
        }

        bool clicked = false;
        {
            ImGui::SetCursorScreenPos(sa);
            ImGui::InvisibleButton(title, ImVec2(swW, swH));
            const bool swHov = ImGui::IsItemHovered();
            if (enabled && ImGui::IsItemClicked()) { *value = !*value; clicked = true; }

            const ImGuiID sid = win->GetID(title);
            float* t = win->StateStorage.GetFloatRef(sid, *value ? 1.0f : 0.0f);
            const float target = *value ? 1.0f : 0.0f;
            const float sp = ImGui::GetIO().DeltaTime * 12.0f;
            if (*t < target) *t = ImMin(target, *t + sp);
            else if (*t > target) *t = ImMax(target, *t - sp);

            const float r = swH * 0.5f;
            const ImU32 track = (*t > 0.5f) ? kAccent : kLine;
            if (swHov && enabled) {
                // The glow the mockup shows as a box-shadow: one wider rounded
                // rect behind the control, which is what ImGui can actually do.
                dl->AddRectFilled(ImVec2(sa.x - 3, sa.y - 3), ImVec2(sa.x + swW + 3, sa.y + swH + 3),
                    Fade(kAccent, 0.18f), r + 3.0f);
            }
            dl->AddRectFilled(sa, ImVec2(sa.x + swW, sa.y + swH),
                enabled ? track : Fade(kLine, 0.45f), r);
            const float knobX = sa.x + r + (*t) * (swW - r * 2.0f);
            dl->AddCircleFilled(ImVec2(knobX, sa.y + r), r - 3.0f,
                enabled ? ((*t > 0.5f) ? IM_COL32(255, 255, 255, 255) : kDimmer)
                : Fade(kDimmer, 0.5f));
        }

        // Tooltip anchored to the row, not the switch, so hovering anywhere on
        // the row explains the tweak.
        if (hov && tooltip && tooltip[0]) {
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(13, 11));
            ImGui::BeginTooltip();
            ImGui::PushTextWrapPos(340.0f);
            ImGui::TextUnformatted(title);
            ImGui::Separator();
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(kDim));
            ImGui::TextUnformatted(tooltip);
            ImGui::PopStyleColor();
            ImGui::PopTextWrapPos();
            ImGui::EndTooltip();
            ImGui::PopStyleVar();
        }

        // 🔴 THE ROW MUST END WITH A REAL ITEM, NOT A CURSOR MOVE.
        //
        // The first version finished with SetCursorScreenPos alone. ImGui then
        // warns "Code uses SetCursorPos()/SetCursorScreenPos() to extend
        // window/parent boundaries. Please submit an item e.g. Dummy()
        // afterwards" - and it is not a nag: the parent child-window never grew
        // to fit the rows, so the last row rendered outside its container and
        // landed on top of the page header.
        //
        // Reset to the row's own origin first, then submit a Dummy the full size
        // of the row. That both advances the cursor for the next row and tells
        // the parent how tall its content actually is.
        ImGui::SetCursorScreenPos(p);
        ImGui::Dummy(ImVec2(availW, rowH + 2.0f));
        return clicked;
    }

    // ---- stall timeline -------------------------------------------------
    // The signature panel. Height encodes duration against reference rules,
    // colour says whether a cause was found, hover gives the whole sentence.
    // No new data: the caller passes what SnapshotStalls() already returns.
    struct StallMark {
        ULONGLONG whenUtc = 0;
        ULONGLONG lateMs = 0;
        bool attributed = false;
        std::string timeText;   // pre-formatted, so this widget owns no clock
        std::string cause;
    };

    inline void StallTimeline(const std::vector<StallMark>& marks,
        ULONGLONG spanStartUtc, ULONGLONG spanEndUtc,
        const char* axisLabels[], int axisCount,
        float height = 80.0f) {
        ImGuiWindow* win = ImGui::GetCurrentWindow();
        ImDrawList* dl = win->DrawList;

        const float labelGutter = 52.0f;
        const float availW = ImGui::GetContentRegionAvail().x;
        const ImVec2 p = win->DC.CursorPos;
        const float plotW = availW - labelGutter;
        const ImVec2 base(p.x, p.y + height);

        // Reference rules at 250 / 1000 / 3000ms, log-ish so a 7s spike does not
        // flatten every 300ms one into the baseline.
        auto HeightFor = [&](ULONGLONG ms) {
            const float f = logf(1.0f + (float)ms / 250.0f) / logf(1.0f + 3000.0f / 250.0f);
            return ImClamp(f, 0.04f, 1.0f) * (height - 6.0f);
            };
        const struct { ULONGLONG ms; const char* txt; } rules[] = {
            { 3000, "3000ms" }, { 1000, "1000ms" }, { 250, "250ms" }
        };
        for (const auto& r : rules) {
            const float y = base.y - HeightFor(r.ms);
            // Dashes drawn by hand: ImGui has no dashed stroke.
            for (float x = p.x; x < p.x + plotW; x += 9.0f)
                dl->AddLine(ImVec2(x, y), ImVec2(ImMin(x + 4.0f, p.x + plotW), y), Fade(kLine, 0.95f), 1.0f);
            dl->AddText(ImVec2(p.x + plotW + 6.0f, y - ImGui::GetTextLineHeight() * 0.5f), kDimmer, r.txt);
        }

        const double span = (spanEndUtc > spanStartUtc) ? (double)(spanEndUtc - spanStartUtc) : 1.0;
        const float barW = 7.0f;

        for (size_t i = 0; i < marks.size(); ++i) {
            const StallMark& m = marks[i];
            const double rel = ImClamp((double)(m.whenUtc - spanStartUtc) / span, 0.0, 1.0);
            const float cx = p.x + (float)rel * plotW;
            const float h = HeightFor(m.lateMs);
            const ImVec2 a(cx - barW * 0.5f, base.y - h), b(cx + barW * 0.5f, base.y);

            const bool hov = ImGui::IsWindowHovered() &&
                ImGui::IsMouseHoveringRect(ImVec2(a.x - 2, a.y - 4), ImVec2(b.x + 2, b.y));
            ImU32 col = m.attributed ? kWarn : kDim;
            if (hov) col = m.attributed ? IM_COL32(255, 200, 100, 255) : kInk;
            dl->AddRectFilled(a, b, col, barW * 0.5f);

            if (hov) {
                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12, 10));
                ImGui::BeginTooltip();
                ImGui::PushTextWrapPos(240.0f);
                ImGui::Text("%s  -  %llums", m.timeText.c_str(), (unsigned long long)m.lateMs);
                ImGui::Separator();
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(kDim));
                ImGui::TextUnformatted(m.cause.c_str());
                ImGui::PopStyleColor();
                ImGui::PopTextWrapPos();
                ImGui::EndTooltip();
                ImGui::PopStyleVar();
            }
        }

        dl->AddLine(ImVec2(p.x, base.y), ImVec2(p.x + plotW, base.y), kLine, 1.0f);
        ImGui::Dummy(ImVec2(availW, height + 4.0f));

        if (axisLabels && axisCount > 1) {
            const float step = plotW / (float)(axisCount - 1);
            const ImVec2 ap = win->DC.CursorPos;
            for (int i = 0; i < axisCount; ++i) {
                const ImVec2 ts = ImGui::CalcTextSize(axisLabels[i]);
                float x = p.x + step * i;
                if (i == 0) x += 0.0f;
                else if (i == axisCount - 1) x -= ts.x;
                else x -= ts.x * 0.5f;
                dl->AddText(ImVec2(x, ap.y), kDimmer, axisLabels[i]);
            }
            ImGui::Dummy(ImVec2(availW, ImGui::GetTextLineHeight() + 2.0f));
        }
    }

    // ---- eyebrow --------------------------------------------------------
    inline void Eyebrow(const char* text) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(kDimmer));
        ImGui::TextUnformatted(text);
        ImGui::PopStyleColor();
        ImGui::Dummy(ImVec2(0, 3));
    }

    // ======================================================================
    // THE TWEAKS PAGE MODEL
    //
    // Four pages (Power & Timers, Services & Cleanup, Experimental, Debloat)
    // merged into one. They were named after the part of Windows they touch,
    // so finding a tweak required knowing how it was implemented - "is MSI
    // mode under Power & Timers or Experimental?" is unguessable. The sections
    // below are named after what the user is trying to achieve instead.
    //
    // 🔴 WHY A DESCRIPTOR LIST AND NOT FOUR RENDER BLOCKS CONCATENATED.
    // Search, the state filters and the per-section counts all have to agree
    // with what is actually on screen. Anything derived by a second pass over
    // a different list eventually disagrees with the first - and a count that
    // says "4 of 9 on" beside five visible rows is the same class of defect as
    // a toggle reporting success for something that did not happen. One list
    // is built per frame, filtered once, and both rendered and counted from
    // the result, so they cannot drift.
    //
    // The handlers stay lambdas owned by the caller rather than moving in
    // here: they need app-level state (the anti-cheat flag, the game library,
    // the throttling confirmation) that lower headers must not reach into.
    // ======================================================================

    enum class TweakSection { Performance = 0, Interference, InputDisplay, PrivacyCleanup, COUNT };

    struct TweakDesc {
        const char* icon = OMX_ICON_GEAR;
        std::string title;
        std::string subtitle;
        std::string tooltip;
        TweakSection section = TweakSection::Performance;

        bool state = false;         // what the machine/flag says right now
        bool available = true;      // false -> N/A, row greyed and clicks swallowed
        bool advanced = false;      // risky or driver-level; hidden behind "show advanced"
        bool pending = false;       // written but only read at boot -> REBOOT, not ON

        // Called with the state the user asked for. It must do the work and
        // commit the flag itself - the row deliberately holds no state of its
        // own, so a refused write snaps the switch back on the next frame.
        std::function<void(bool)> apply;
    };

    // Chip precedence: unavailability beats everything, then state, and
    // ADVANCED only shows on a row that is off - once a risky tweak is ON,
    // saying so matters more than repeating that it was risky.
    inline ChipKind ChipKindFor(const TweakDesc& t, const char** textOut) {
        if (!t.available) { *textOut = "N/A";      return ChipKind::Unavailable; }
        if (t.state && t.pending) { *textOut = "REBOOT";   return ChipKind::Pending; }
        if (t.state) { *textOut = "ON";       return ChipKind::On; }
        if (t.advanced) { *textOut = "ADVANCED"; return ChipKind::Advanced; }
        *textOut = "OFF";      return ChipKind::Off;
    }

    // ---- section header --------------------------------------------------
    // Title, the goal in plain words, and the count of what is on within it.
    // Collapsible, because four sections of six rows is still a long page.
    inline bool SectionHeader(const char* title, const char* why,
        int onCount, int total, bool* open) {
        ImGuiWindow* win = ImGui::GetCurrentWindow();
        ImDrawList* dl = win->DrawList;

        const float lineH = ImGui::GetTextLineHeight();
        const float rowH = lineH * 2.0f + 20.0f;
        const float availW = ImGui::GetContentRegionAvail().x;
        const ImVec2 p = win->DC.CursorPos;
        const ImRect bb(p, ImVec2(p.x + availW, p.y + rowH));

        ImGui::SetCursorScreenPos(p);
        ImGui::InvisibleButton(title, ImVec2(availW, rowH));
        const bool hov = ImGui::IsItemHovered();
        if (ImGui::IsItemClicked()) *open = !*open;

        if (hov) dl->AddRectFilled(bb.Min, bb.Max, kCardHi, 12.0f);

        // Caret drawn rather than a glyph: the icon font covers the arrows we
        // merged for the dashboard, but a triangle is three points and cannot
        // go missing on a machine whose Segoe UI Symbol differs.
        const ImVec2 c(bb.Min.x + 16.0f, bb.Min.y + rowH * 0.5f);
        const float s = 4.5f;
        if (*open) dl->AddTriangleFilled(ImVec2(c.x - s, c.y - s * 0.6f), ImVec2(c.x + s, c.y - s * 0.6f),
            ImVec2(c.x, c.y + s * 0.9f), hov ? kAccentHot : kDim);
        else       dl->AddTriangleFilled(ImVec2(c.x - s * 0.6f, c.y - s), ImVec2(c.x - s * 0.6f, c.y + s),
            ImVec2(c.x + s * 0.9f, c.y), hov ? kAccentHot : kDim);

        const float textX = bb.Min.x + 32.0f;
        const float textY = bb.Min.y + (rowH - (lineH * 2.0f + 2.0f)) * 0.5f;
        dl->AddText(ImVec2(textX, textY), kInk, title);
        dl->AddText(ImVec2(textX, textY + lineH + 2.0f), kDimmer, why);

        {
            char cnt[48];
            sprintf_s(cnt, "%d OF %d ON", onCount, total);
            const float cw = ChipWidth(cnt);
            const ImVec2 ts = ImGui::CalcTextSize(cnt);
            DrawChip(dl, ImVec2(bb.Max.x - 12.0f - cw, bb.Min.y + (rowH - (ts.y + 6.0f)) * 0.5f),
                cnt, onCount > 0 ? ChipKind::On : ChipKind::Off);
        }

        ImGui::SetCursorScreenPos(p);
        ImGui::Dummy(ImVec2(availW, rowH + 2.0f));
        return *open;
    }

    // ---- filter chip -----------------------------------------------------
    // A pill that reads as a filter rather than a button, with its count
    // inline. Returns true on click; the caller owns which one is selected.
    inline bool FilterChip(const char* label, int count, bool selected) {
        char buf[64];
        sprintf_s(buf, "%s  %d", label, count);

        const ImVec2 ts = ImGui::CalcTextSize(buf);
        const ImVec2 pad(13.0f, 6.0f);
        const ImVec2 sz(ts.x + pad.x * 2, ts.y + pad.y * 2);

        ImGuiWindow* win = ImGui::GetCurrentWindow();
        ImDrawList* dl = win->DrawList;
        const ImVec2 p = win->DC.CursorPos;

        ImGui::InvisibleButton(label, sz);
        const bool hov = ImGui::IsItemHovered();
        const bool clicked = ImGui::IsItemClicked();

        const ImVec2 a = p, b = ImVec2(p.x + sz.x, p.y + sz.y);
        const float r = sz.y * 0.5f;
        dl->AddRectFilled(a, b, selected ? Fade(kAccent, 0.24f) : kCard, r);
        dl->AddRect(a, b, selected ? kAccentBr : (hov ? kLineHi : kLine), r);
        dl->AddText(ImVec2(a.x + pad.x, a.y + pad.y),
            selected ? kInk : (hov ? kInk : kDim), buf);
        return clicked;
    }

} // namespace OptiMaxUi
