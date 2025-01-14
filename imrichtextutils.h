#pragma once

#include <string_view>
#include <optional>

#include "imgui.h"

#ifndef IM_RICHTEXT_MAX_COLORSTOPS
#define IM_RICHTEXT_MAX_COLORSTOPS 4
#endif

#ifdef IM_RICHTEXT_NO_IMGUI
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

#if !defined(IMGUI_DEFINE_MATH_OPERATORS) || defined(IM_RICHTEXT_NO_IMGUI)
[[nodiscard]] ImVec2 operator+(ImVec2 lhs, ImVec2 rhs) { return ImVec2{ lhs.x + rhs.x, lhs.y + rhs.y }; }
[[nodiscard]] ImVec2 operator*(ImVec2 lhs, float rhs) { return ImVec2{ lhs.x * rhs, lhs.y * rhs }; }
[[nodiscard]] ImVec2 operator-(ImVec2 lhs, ImVec2 rhs) { return ImVec2{ lhs.x - rhs.x, lhs.y - rhs.y }; }
#endif

namespace ImRichText
{
    // Implement this interface to hndle parsed rich text
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
        int rounding = BoxCorner::AllCorners;

        float h() const { return left.thickness + right.thickness; }
        float v() const { return top.thickness + bottom.thickness; }
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

#ifndef IM_RICHTEXT_NO_IMGUI
    void DrawPolyFilledMultiColor(ImDrawList* drawList, const ImVec2* points, const ImU32* col, const int points_count);
    void DrawBullet(ImDrawList* drawList, ImVec2 initpos, const BoundedBox& box, BulletType type, uint32_t color, float size);
#endif

    // Parse rich text and invoke appropriate visitor methods
    void ParseRichText(const char* text, const char* textend, char TagStart, char TagEnd, ITagVisitor& visitor);
}
