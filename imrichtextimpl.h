#pragma once

#ifdef IM_RICHTEXT_TARGET_IMGUI
#include "imgui_internal.h"
#endif
#ifdef IM_RICHTEXT_TARGET_BLEND2D
#include <blend2d.h>
#endif
#include "imrichtextutils.h"

namespace ImRichText
{
    struct RenderConfig;

#ifdef IM_RICHTEXT_TARGET_IMGUI

    struct ImGuiRenderer final : public IRenderer
    {
        RenderConfig& config;

        ImGuiRenderer(RenderConfig&);

        void SetClipRect(ImVec2 startpos, ImVec2 endpos);
        void ResetClipRect();

        void DrawLine(ImVec2 startpos, ImVec2 endpos, uint32_t color, float thickness = 1.f);
        void DrawPolyline(ImVec2* points, int sz, uint32_t color, float thickness);
        void DrawTriangle(ImVec2 pos1, ImVec2 pos2, ImVec2 pos3, uint32_t color, bool filled, bool thickness = 1.f);
        void DrawRect(ImVec2 startpos, ImVec2 endpos, uint32_t color, bool filled, float thickness = 1.f, float radius = 0.f, int corners = 0);
        void DrawRectGradient(ImVec2 startpos, ImVec2 endpos, uint32_t topleftcolor, uint32_t toprightcolor, uint32_t bottomrightcolor, uint32_t bottomleftcolor);
        void DrawPolygon(ImVec2* points, int sz, uint32_t color, bool filled, float thickness = 1.f);
        void DrawPolyGradient(ImVec2* points, uint32_t* colors, int sz);
        void DrawCircle(ImVec2 center, float radius, uint32_t color, bool filled, bool thickness = 1.f);
        void DrawRadialGradient(ImVec2 center, float radius, uint32_t in, uint32_t out);

        bool SetCurrentFont(std::string_view family, float sz, FontType type) override;
        bool SetCurrentFont(void* fontptr) override;
        void ResetFont() override;
        ImVec2 GetTextSize(std::string_view text, void* fontptr);
        void DrawText(std::string_view text, ImVec2 pos, uint32_t color);
        void DrawText(std::string_view text, std::string_view family, ImVec2 pos, float sz, uint32_t color, FontType type);
        void DrawTooltip(ImVec2 pos, std::string_view text);
        float EllipsisWidth(void* fontptr) override;
    };

    struct ImGuiPlatform final : public IPlatform
    {
        void (*HyperlinkClicked)(std::string_view);

        ImGuiPlatform(void (*hc)(std::string_view) = nullptr)
            : HyperlinkClicked(hc) {}

        ImVec2 GetCurrentMousePos();
        bool IsMouseClicked();
        void HandleHyperlink(std::string_view);
        void RequestFrame() {}
        void HandleHover(bool hovered);
    };

#endif
#ifdef IM_RICHTEXT_TARGET_BLEND2D

    struct Blend2DRenderer final : public IRenderer
    {
        BLContext& context;
        BLFont* currentFont = nullptr;

        Blend2DRenderer(BLContext& ctx);

        void SetClipRect(ImVec2 startpos, ImVec2 endpos);
        void ResetClipRect();

        void DrawLine(ImVec2 startpos, ImVec2 endpos, uint32_t color, float thickness = 1.f);
        void DrawPolyline(ImVec2* points, int sz, uint32_t color, float thickness);
        void DrawTriangle(ImVec2 pos1, ImVec2 pos2, ImVec2 pos3, uint32_t color, bool filled, bool thickness = 1.f);
        void DrawRect(ImVec2 startpos, ImVec2 endpos, uint32_t color, bool filled, float thickness = 1.f, float radius = 0.f, int corners = 0);
        void DrawRectGradient(ImVec2 startpos, ImVec2 endpos, uint32_t topleftcolor, uint32_t toprightcolor, uint32_t bottomrightcolor, uint32_t bottomleftcolor);
        void DrawPolygon(ImVec2* points, int sz, uint32_t color, bool filled, float thickness = 1.f);
        void DrawPolyGradient(ImVec2* points, uint32_t* colors, int sz);
        void DrawCircle(ImVec2 center, float radius, uint32_t color, bool filled, bool thickness = 1.f);
        void DrawRadialGradient(ImVec2 center, float radius, uint32_t in, uint32_t out);

        bool SetCurrentFont(std::string_view family, float sz, FontType type) override;
        bool SetCurrentFont(void* fontptr) override;
        void ResetFont() override;
        ImVec2 GetTextSize(std::string_view text, void* fontptr);
        void DrawText(std::string_view text, ImVec2 pos, uint32_t color);
        void DrawText(std::string_view text, std::string_view family, ImVec2 pos, float sz, uint32_t color, FontType type);
        void DrawTooltip(ImVec2 pos, std::string_view text);
        float EllipsisWidth(void* fontptr);
    };

#endif
}
