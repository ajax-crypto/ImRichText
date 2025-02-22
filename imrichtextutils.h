#pragma once

#include <string_view>
#include <optional>

#ifdef IM_RICHTEXT_TARGET_IMGUI
#include "imgui.h"
#endif

#if __has_include("imrichtextfont.h")
#include "imrichtextfont.h"
#define IM_RICHTEXT_BUNDLED_FONTLOADER
#else
namespace ImRichText
{
    enum FontType
    {
        FT_Normal, FT_Light, FT_Bold, FT_Italics, FT_BoldItalics, FT_Total
    };
}
#endif

#ifndef IM_RICHTEXT_MAX_COLORSTOPS
#define IM_RICHTEXT_MAX_COLORSTOPS 4
#endif

#ifndef IM_RICHTEXT_TARGET_IMGUI
#define IM_COL32_BLACK       ImRichText::ToRGBA(0, 0, 0, 255)
#define IM_COL32_BLACK_TRANS ImRichText::ToRGBA(0, 0, 0, 0)

struct ImVec2
{
    float x = 0.f, y = 0.f;
};

enum ImGuiDir : int
{
    ImGuiDir_None = -1,
    ImGuiDir_Left = 0,
    ImGuiDir_Right = 1,
    ImGuiDir_Up = 2,
    ImGuiDir_Down = 3,
    ImGuiDir_COUNT
};

#endif

#if !defined(IMGUI_DEFINE_MATH_OPERATORS)
[[nodiscard]] ImVec2 operator+(ImVec2 lhs, ImVec2 rhs) { return ImVec2{ lhs.x + rhs.x, lhs.y + rhs.y }; }
[[nodiscard]] ImVec2 operator*(ImVec2 lhs, float rhs) { return ImVec2{ lhs.x * rhs, lhs.y * rhs }; }
[[nodiscard]] ImVec2 operator-(ImVec2 lhs, ImVec2 rhs) { return ImVec2{ lhs.x - rhs.x, lhs.y - rhs.y }; }
#endif

namespace ImRichText
{
    enum class BulletType
    {
        Circle,
        FilledCircle,
        Disk = FilledCircle,
        Square,
        Triangle,
        Arrow,
        CheckMark,
        CheckBox, // Needs fixing
        Concentric,
        Custom
    };

    struct BoundedBox
    {
        float top = 0.f, left = 0.f;
        float width = 0.f, height = 0.f;

        ImVec2 start(ImVec2 origin) const { return ImVec2{ left, top } + origin; }
        ImVec2 end(ImVec2 origin) const { return ImVec2{ left + width, top + height } + origin; }
        ImVec2 center(ImVec2 origin) const { return ImVec2{ left + (0.5f * width), top + (0.5f * height) } + origin; }
    };

    // Implement this to draw primitives in your favorite graphics API
    struct IRenderer
    {
        void* UserData = nullptr;

        virtual void SetClipRect(ImVec2 startpos, ImVec2 endpos) = 0;
        virtual void ResetClipRect() = 0;

        virtual void DrawLine(ImVec2 startpos, ImVec2 endpos, uint32_t color, float thickness = 1.f) = 0;
        virtual void DrawPolyline(ImVec2* points, int sz, uint32_t color, float thickness) = 0;
        virtual void DrawTriangle(ImVec2 pos1, ImVec2 pos2, ImVec2 pos3, uint32_t color, bool filled, bool thickness = 1.f) = 0;
        virtual void DrawRect(ImVec2 startpos, ImVec2 endpos, uint32_t color, bool filled, float thickness = 1.f, float radius = 0.f, int corners = 0) = 0;
        virtual void DrawRectGradient(ImVec2 startpos, ImVec2 endpos, uint32_t topleftcolor, uint32_t toprightcolor, uint32_t bottomrightcolor, uint32_t bottomleftcolor) = 0;
        virtual void DrawPolygon(ImVec2* points, int sz, uint32_t color, bool filled, float thickness = 1.f) = 0;
        virtual void DrawPolyGradient(ImVec2* points, uint32_t* colors, int sz) = 0;
        virtual void DrawCircle(ImVec2 center, float radius, uint32_t color, bool filled, bool thickness = 1.f) = 0;
        virtual void DrawRadialGradient(ImVec2 center, float radius, uint32_t in, uint32_t out) = 0;
        virtual void DrawBullet(ImVec2 startpos, ImVec2 endpos, uint32_t color, int index, int depth) {};
        
        virtual bool SetCurrentFont(std::string_view family, float sz, FontType type) { return false; };
        virtual bool SetCurrentFont(void* fontptr) { return false; };
        virtual void ResetFont() {};
        virtual ImVec2 GetTextSize(std::string_view text, void* fontptr) = 0;
        virtual void DrawText(std::string_view text, ImVec2 pos, uint32_t color) = 0;
        virtual void DrawText(std::string_view text, std::string_view family, ImVec2 pos, float sz, uint32_t color, FontType type) = 0;
        virtual void DrawTooltip(ImVec2 pos, std::string_view text) = 0;
        virtual float EllipsisWidth(void* fontptr);

        void DrawDefaultBullet(BulletType type, ImVec2 initpos, const BoundedBox& bounds, uint32_t color, float bulletsz);
    };

    // Implement this interface to handle parsed rich text
    struct ITagVisitor
    {
        virtual bool TagStart(std::string_view tag) = 0;
        virtual bool Attribute(std::string_view name, std::optional<std::string_view> value) = 0;
        virtual bool TagStartDone() = 0;
        virtual bool Content(std::string_view content) = 0;
        virtual bool PreFormattedContent(std::string_view content) = 0;
        virtual bool TagEnd(std::string_view tag, bool selfTerminating) = 0;
        virtual void Finalize() = 0;

        virtual void Error(std::string_view tag) = 0;

        virtual bool IsSelfTerminating(std::string_view tag) const = 0;
        virtual bool IsPreformattedContent(std::string_view tag) const = 0;
    };

    // Implement this to handle platform interactions
    struct IPlatform
    {
        virtual ImVec2 GetCurrentMousePos() = 0;
        virtual bool IsMouseClicked() = 0;

        virtual void HandleHyperlink(std::string_view) = 0;
        virtual void RequestFrame() = 0;
        virtual void HandleHover(bool) = 0;
    };

    struct IntOrFloat
    {
        float value = 0.f;
        bool isFloat = false;
    };

    struct ColorStop
    {
        uint32_t from, to;
        float pos = -1.f;
    };

    struct ColorGradient
    {
        ColorStop colorStops[IM_RICHTEXT_MAX_COLORSTOPS];
        int totalStops = 0;
        float angleDegrees = 0.f;
        ImGuiDir dir = ImGuiDir::ImGuiDir_Down;
    };

    struct FourSidedMeasure
    {
        float top = 0.f, left = 0.f, right = 0.f, bottom = 0.f;

        float h() const { return left + right; }
        float v() const { return top + bottom; }
    };

    enum class LineType
    {
        Solid, Dashed, Dotted, DashDot
    };

    struct Border
    {
        uint32_t color = IM_COL32_BLACK_TRANS;
        float thickness = 0.f;
        LineType lineType = LineType::Solid; // Unused for rendering
    };

    enum BoxCorner
    {
        NoCorners = 0,
        TopLeftCorner = 1,
        TopRightCorner = 2,
        BottomRightCorner = 4,
        BottomLeftCorner = 8,
        AllCorners = TopLeftCorner | TopRightCorner | BottomRightCorner | BottomLeftCorner
    };

    struct FourSidedBorder
    {
        Border top, left, bottom, right;
        float radius = 0.f;
        int rounding = BoxCorner::NoCorners;
        bool isUniform = false;

        float h() const { return left.thickness + right.thickness; }
        float v() const { return top.thickness + bottom.thickness; }

        FourSidedBorder& setColor(uint32_t color);
        FourSidedBorder& setThickness(float thickness);
        //FourSidedBorder& setLineType(uint32_t color);
    };

    struct BoxShadow
    {
        ImVec2 offset{ 0.f, 0.f };
        float spread = 0.f;
        float blur = 1.f;
        uint32_t color = IM_COL32_BLACK;
    };

    // Generic string helpers, case-insensitive matches
    [[nodiscard]] bool AreSame(const std::string_view lhs, const char* rhs);
    [[nodiscard]] bool AreSame(const std::string_view lhs, const std::string_view rhs);
    [[nodiscard]] bool StartsWith(const std::string_view lhs, const char* rhs);
    [[nodiscard]] int SkipDigits(const std::string_view text, int from = 0);
    [[nodiscard]] int SkipFDigits(const std::string_view text, int from = 0);
    [[nodiscard]] int SkipSpace(const std::string_view text, int from = 0);
    [[nodiscard]] int SkipSpace(const char* text, int idx, int end);
    [[nodiscard]] std::optional<std::string_view> GetQuotedString(const char* text, int& idx, int end);

    [[nodiscard]] int ExtractInt(std::string_view input, int defaultVal);
    [[nodiscard]] int ExtractIntFromHex(std::string_view input, int defaultVal);
    [[nodiscard]] IntOrFloat ExtractNumber(std::string_view input, float defaultVal);
    [[nodiscard]] float ExtractFloatWithUnit(std::string_view input, float defaultVal, float ems, float parent, float scale);

    [[nodiscard]] uint32_t ToRGBA(int r, int g, int b, int a = 255);
    [[nodiscard]] uint32_t ToRGBA(float r, float g, float b, float a = 1.f);
    [[nodiscard]] uint32_t ExtractColor(std::string_view stylePropVal, uint32_t(*NamedColor)(const char*, void*), void* userData);
    [[nodiscard]] ColorGradient ExtractLinearGradient(std::string_view input, uint32_t(*NamedColor)(const char*, void*), void* userData);
    [[nodiscard]] uint32_t GetColor(const char* name, void*);

    [[nodiscard]] Border ExtractBorder(std::string_view input, float ems, float percent, 
        uint32_t(*NamedColor)(const char*, void*), void* userData);
    [[nodiscard]] BoxShadow ExtractBoxShadow(std::string_view input, float ems, float percent,
        uint32_t(*NamedColor)(const char*, void*), void* userData);

    // Parse rich text and invoke appropriate visitor methods
    void ParseRichText(const char* text, const char* textend, char TagStart, char TagEnd, ITagVisitor& visitor);
}
