#pragma once

#include <string_view>
#include <optional>
#include <vector>

#include "imgui.h"

#ifndef IM_RICHTEXT_MAX_COLORSTOPS
#define IM_RICHTEXT_MAX_COLORSTOPS 8
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
        ImColor from, to;
        float pos = -1.f;
    };

    struct ColorGradient
    {
        ColorStop colorStops[IM_RICHTEXT_MAX_COLORSTOPS];
        int totalStops = 0;
        float angleDegrees = 0.f;
        ImGuiDir dir = ImGuiDir::ImGuiDir_Down;
        bool reverseOrder = false;
        bool repeating = false;
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
        CheckBox,
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

    enum LineType
    {
        Dashed, Dotted, DashDot
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
    [[nodiscard]] ImColor ExtractColor(std::string_view stylePropVal, ImColor(*NamedColor)(const char*, void*), void* userData);
    [[nodiscard]] ColorGradient ExtractLinearGradient(std::string_view input, ImColor(*NamedColor)(const char*, void*), void* userData);

    [[nodiscard]] ImColor GetColor(const char* name, void*);
    void DrawPolyFilledMultiColor(ImDrawList* drawList, const ImVec2* points, const ImU32* col, const int points_count);
    void DrawBullet(ImDrawList* drawList, ImVec2 initpos, const BoundedBox& box, BulletType type, ImColor color, float size);

    // Parse rich text and invoke appropriate visitor methods
    void ParseRichText(const char* text, const char* textend, char TagStart, char TagEnd, ITagVisitor& visitor);
}
