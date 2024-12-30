#include "ImRichText.h"
#include "imrichtextfont.h"
#include "imrichtextcolor.h"
#include "imgui_internal.h"

#include <unordered_map>
#include <cstring>
#include <cctype>
#include <optional>
#include <cstdlib>
#include <string>
#include <tuple>
#include <chrono>

#ifdef _DEBUG
#include <cstdio>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#define ERROR(FMT, ...) { \
    CONSOLE_SCREEN_BUFFER_INFO cbinfo; \
    auto h = GetStdHandle(STD_ERROR_HANDLE); \
    GetConsoleScreenBufferInfo(h, &cbinfo); \
    SetConsoleTextAttribute(h, FOREGROUND_RED | FOREGROUND_INTENSITY); \
    std::fprintf(stderr, FMT, __VA_ARGS__); \
    SetConsoleTextAttribute(h, cbinfo.wAttributes); }
#else
#define ERROR(FMT, ...) std::fprintf(stderr, "\x1B[31m" FMT "\x1B[0m", __VA_ARGS__)
#endif
#ifdef IM_RICHTEXT_ENABLE_PARSER_LOGS
#define DashedLine "-----------------------------------------"
const char* TabLine = "\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t";
#define LOG(FMT, ...)  std::fprintf(stdout, "%.*s" FMT, CurrentStackPos+1, TabLine, __VA_ARGS__)
#define HIGHLIGHT(FMT, ...) std::fprintf(stdout, DashedLine FMT "\n" DashedLine "\n", __VA_ARGS__)
#else
#define LOG(FMT, ...)
#define HIGHLIGHT(FMT, ...)
#endif
#else
#define ERROR(FMT, ...)
#define LOG(FMT, ...)
#define HIGHLIGHT(FMT, ...)
#endif

namespace ImRichText
{
    struct BlockquoteDrawData
    {
        std::vector<std::pair<ImVec2, ImVec2>> bounds;
        float linewidth = 0.f;
    };

    struct RichTextData
    {
        Drawables drawables;
        RenderConfig* config = nullptr;
        std::string richText;
        bool contentChanged = false;
    };

    struct TooltipData
    {
        ImVec2 pos;
        std::string_view content;
    };

    struct AnimationData
    {
        std::vector<float> xoffsets;
        long long lastBlinkTime;
        long long lastMarqueeTime;
        bool isVisible = true;
    };

    static std::unordered_map<std::string_view, std::size_t> RichTextStrings;
    static std::unordered_map<std::size_t, RichTextData> RichTextMap;
    static std::pair<std::string_view, SegmentStyle> TagStack[IM_RICHTEXT_MAXDEPTH];
    static std::unordered_map<std::string_view, AnimationData> AnimationMap;

    static int CurrentStackPos = -1;
    static int ListItemCountByDepths[IM_RICHTEXT_MAX_LISTDEPTH];
    static BlockquoteDrawData BlockquoteStack[IM_RICHTEXT_MAXDEPTH];

    static std::unordered_map<ImGuiContext*, std::deque<RenderConfig>> RenderConfigs;
    static RenderConfig DefaultRenderConfig;
    static bool DefaultRenderConfigInit = false;

    static std::vector<std::string> NumbersAsStr;

    static const std::pair<std::string_view, std::string_view> EscapeCodes[10] = {
        { "Tab", "\t" }, { "NewLine", "\n" }, { "nbsp", " " },
        { "gt", ">" }, { "lt", "<" },
        { "amp", "&" }, { "copy", "©" }, { "reg", "®" },
        { "deg", "°" }, { "micro", "μ" }
    };

    static const char* LineSpaces = "                                ";

    template <typename T>
    [[nodiscard]] T Clamp(T val, T min, T max)
    {
        return val < min ? min : val > max ? max : val;
    }

#pragma optimize( "", on )
    [[nodiscard]] int SkipSpace(const char* text, int idx, int end)
    {
        while ((idx < end) && std::isspace(text[idx])) idx++;
        return idx;
    }

    [[nodiscard]] int SkipSpace(const std::string_view text, int from = 0)
    {
        auto end = (int)text.size();
        while ((from < end) && (std::isspace(text[from]))) from++;
        return from;
    }

    [[nodiscard]] int WholeWord(const std::string_view text, int from = 0)
    {
        auto end = (int)text.size();
        while ((from < end) && (!std::isspace(text[from]))) from++;
        return from;
    }

    [[nodiscard]] int SkipDigits(const std::string_view text, int from = 0)
    {
        auto end = (int)text.size();
        while ((from < end) && (std::isdigit(text[from]))) from++;
        return from;
    }

    [[nodiscard]] int SkipFDigits(const std::string_view text, int from = 0)
    {
        auto end = (int)text.size();
        while ((from < end) && ((std::isdigit(text[from])) || (text[from] == '.'))) from++;
        return from;
    }
#pragma optimize( "", off )

#ifndef IMGUI_DEFINE_MATH_OPERATORS
    [[nodiscard]] ImVec2 operator*(ImVec2 lhs, float rhs)
    {
        return ImVec2{ lhs.x * rhs, lhs.y * rhs };
    }

    [[nodiscard]] ImVec2 operator+(ImVec2 lhs, ImVec2 rhs)
    {
        return ImVec2{ lhs.x + rhs.x, lhs.y + rhs.y };
    }

    [[nodiscard]] ImVec2 operator-(ImVec2 lhs, ImVec2 rhs)
    {
        return ImVec2{ lhs.x - rhs.x, lhs.y - rhs.y };
    }
#endif

    [[nodiscard]] bool AreSame(const std::string_view lhs, const char* rhs)
    {
        auto rlimit = (int)std::strlen(rhs);
        auto llimit = (int)lhs.size();
        if (rlimit != llimit)
            return false;

        for (auto idx = 0; idx < rlimit; ++idx)
            if (std::tolower(lhs[idx]) != std::tolower(rhs[idx]))
                return false;

        return true;
    }

    [[nodiscard]] bool AreSame(const std::string_view lhs, const std::string_view rhs)
    {
        auto rlimit = (int)rhs.size();
        auto llimit = (int)lhs.size();
        if (rlimit != llimit)
            return false;

        for (auto idx = 0; idx < rlimit; ++idx)
            if (std::tolower(lhs[idx]) != std::tolower(rhs[idx]))
                return false;

        return true;
    }

#ifdef _DEBUG
    [[nodiscard]] const char* GetTokenTypeString(const Token& token)
    {
        switch (token.Type)
        {
        case TokenType::Text: return "Text";
        case TokenType::HorizontalRule: return "HorizontalRule";
        case TokenType::ListItemBullet: return "ListItemBullet";
        case TokenType::ListItemNumbered: return "ListItemNumbered";
        default: return "InvalidToken";
        }
    }
#endif

    [[nodiscard]] int ExtractInt(std::string_view input, int defaultVal)
    {
        int result = defaultVal;
        int base = 1;
        auto idx = (int)input.size() - 1;
        while (!std::isdigit(input[idx]) && idx >= 0) idx--;

        for (; idx >= 0; --idx)
        {
            result += (input[idx] - '0') * base;
            base *= 10;
        }

        return result;
    }

    [[nodiscard]] int ExtractIntFromHex(std::string_view input, int defaultVal)
    {
        int result = defaultVal;
        int base = 1;
        auto idx = (int)input.size() - 1;
        while (!std::isdigit(input[idx]) && !std::isalpha(input[idx]) && idx >= 0) idx--;

        for (; idx >= 0; --idx)
        {
            result += std::isdigit(input[idx]) ? (input[idx] - '0') * base : 
                std::islower(input[idx]) ? ((input[idx] - 'a') + 10) * base : 
                ((input[idx] - 'A') + 10) * base;
            base *= 16;
        }

        return result;
    }

    struct IntOrFloat
    {
        float value = 0.f;
        bool isFloat = false;
    };

    [[nodiscard]] IntOrFloat ExtractNumber(std::string_view input, float defaultVal)
    {
        float result = 0.f, base = 1.f;
        bool isInt = false;
        auto idx = (int)input.size() - 1;

        while (idx >= 0 && input[idx] != '.') idx--;
        auto decimal = idx;

        if (decimal != -1)
        {
            for (auto midx = decimal; midx >= 0; --midx)
            {
                result += (input[midx] - '0') * base;
                base *= 10.f;
            }

            base = 0.1f;
            for (auto midx = decimal + 1; midx < (int)input.size(); ++midx)
            {
                result += (input[midx] - '0') * base;
                base *= 0.1f;
            }
        }
        else
        {
            for (auto midx = (int)input.size() - 1; midx >= 0; --midx)
            {
                result += (input[midx] - '0') * base;
                base *= 10.f;
            }

            isInt = true;
        }

        return { result, !isInt };
    }

    [[nodiscard]] float ExtractFloatWithUnit(std::string_view input, float defaultVal, float ems, float parent, float scale)
    {
        float result = defaultVal, base = 1.f;
        auto idx = (int)input.size() - 1;
        while (!std::isdigit(input[idx]) && idx >= 0) idx--;

        if (AreSame(input.substr(idx + 1), "pt")) scale = 1.3333f;
        else if (AreSame(input.substr(idx + 1), "em")) scale = ems;
        else if (input[idx + 1] == '%') scale = parent * 0.01f;

        auto num = ExtractNumber(input.substr(0, idx + 1), defaultVal);
        result = num.value;

        return result * scale;
    }

    [[nodiscard]] std::tuple<IntOrFloat, IntOrFloat, IntOrFloat, IntOrFloat> GetCommaSeparatedNumbers(std::string_view stylePropVal, int& curr)
    {
        std::tuple<IntOrFloat, IntOrFloat, IntOrFloat, IntOrFloat> res;
        auto hasFourth = curr == 4;
        curr = SkipSpace(stylePropVal, curr);
        assert(stylePropVal[curr] == '(');
        curr++;

        auto valstart = curr;
        curr = SkipFDigits(stylePropVal, curr);
        std::get<0>(res) = ExtractNumber(stylePropVal.substr(valstart, curr - valstart), 0);
        curr = SkipSpace(stylePropVal, curr);
        assert(stylePropVal[curr] == ',');
        curr++;
        curr = SkipSpace(stylePropVal, curr);

        valstart = curr;
        curr = SkipFDigits(stylePropVal, curr);
        std::get<1>(res) = ExtractNumber(stylePropVal.substr(valstart, curr - valstart), 0);
        curr = SkipSpace(stylePropVal, curr);
        assert(stylePropVal[curr] == ',');
        curr++;
        curr = SkipSpace(stylePropVal, curr);

        valstart = curr;
        curr = SkipFDigits(stylePropVal, curr);
        std::get<2>(res) = ExtractNumber(stylePropVal.substr(valstart, curr - valstart), 0);
        curr = SkipSpace(stylePropVal, curr);

        if (hasFourth)
        {
            assert(stylePropVal[curr] == ',');
            curr++;
            curr = SkipSpace(stylePropVal, curr);

            valstart = curr;
            curr = SkipFDigits(stylePropVal, curr);
            std::get<3>(res) = ExtractNumber(stylePropVal.substr(valstart, curr - valstart), 0);
        }

        return res;
    }

    [[nodiscard]] ImColor ExtractColor(std::string_view stylePropVal, ImColor(*NamedColor)(const char*, void*), void* userData)
    {
        if (stylePropVal.size() >= 3u && AreSame(stylePropVal.substr(0, 3), "rgb"))
        {
            IntOrFloat r, g, b, a;
            auto hasAlpha = stylePropVal[3] == 'a' || stylePropVal[3] == 'A';
            int curr = hasAlpha ? 4 : 3;
            std::tie(r, g, b, a) = GetCommaSeparatedNumbers(stylePropVal, curr);
            auto isRelative = r.isFloat && g.isFloat && b.isFloat;
            a.value = isRelative ? hasAlpha ? a.value : 1.f :
                hasAlpha ? a.value : 255;

            assert(stylePropVal[curr] == ')');
            return isRelative ? ImColor{ r.value, g.value, b.value, a.value } : 
                ImColor{ (int)r.value, (int)g.value, (int)b.value, (int)a.value };
        }
        else if (stylePropVal.size() >= 3u && AreSame(stylePropVal.substr(0, 3), "hsv"))
        {
            IntOrFloat h, s, v;
            auto curr = 3;
            std::tie(h, s, v, std::ignore) = GetCommaSeparatedNumbers(stylePropVal, curr);
            
            assert(stylePropVal[curr] == ')');
            return ImColor::HSV(h.value, s.value, v.value);
        }
        else if (stylePropVal.size() >= 3u && AreSame(stylePropVal.substr(0, 3), "hsl"))
        {
            IntOrFloat h, s, l;
            auto curr = 3;
            std::tie(h, s, l, std::ignore) = GetCommaSeparatedNumbers(stylePropVal, curr);
            auto v = l.value + s.value * std::min(l.value, 1.f - l.value);
            s.value = v == 0.f ? 0.f : 2.f * (1.f - (l.value / v));

            assert(stylePropVal[curr] == ')');
            return ImColor::HSV(h.value, s.value, v);
        }
        else if (stylePropVal.size() >= 1u && stylePropVal[0] == '#')
        {
            ImU32 color32 = (ImU32)ExtractIntFromHex(stylePropVal.substr(1), 0);
            return ImColor{ color32 };
        }
        else if (NamedColor != nullptr)
        {
            static char buffer[32] = { 0 };
            std::memset(buffer, 0, 32);
            std::memcpy(buffer, stylePropVal.data(), std::min((int)stylePropVal.size(), 31));
            return NamedColor(buffer, userData);
        }
        else
        {
            return IM_COL32_BLACK;
        }
    }

    enum StyleProperty
    {
        StyleError = -1,
        NoStyleChange = 0,
        StyleBgColor = 1,
        StyleFgColor = 2,
        StyleFontSize = 4,
        StyleFontFamily = 8,
        StyleFontWeight = 16,
        StyleFontStyle = 32,
        StyleHeight = 64,
        StyleWidth = 128,
        StyleListBulletType = 256,
        StyleHAlignment = 512,
        StyleVAlignment = 1024,
        StylePaddingTop = 2048,
        StylePaddingBottom = 4096,
        StylePaddingLeft = 8192,
        StylePaddingRight = 16384
    };

    int PopulateSegmentStyle(SegmentStyle& style,
        const SegmentStyle& parentStyle,
        std::string_view stylePropName,
        std::string_view stylePropVal,
        const RenderConfig& config)
    {
        int prop = NoStyleChange;

        if (AreSame(stylePropName, "font-size"))
        {
            if (AreSame(stylePropVal, "xx-small")) style.font.size = config.DefaultFontSize * 0.6f * config.FontScale;
            else if (AreSame(stylePropVal, "x-small")) style.font.size = config.DefaultFontSize * 0.75f * config.FontScale;
            else if (AreSame(stylePropVal, "small")) style.font.size = config.DefaultFontSize * 0.89f * config.FontScale;
            else if (AreSame(stylePropVal, "medium")) style.font.size = config.DefaultFontSize * config.FontScale;
            else if (AreSame(stylePropVal, "large")) style.font.size = config.DefaultFontSize * 1.2f * config.FontScale;
            else if (AreSame(stylePropVal, "x-large")) style.font.size = config.DefaultFontSize * 1.5f * config.FontScale;
            else if (AreSame(stylePropVal, "xx-large")) style.font.size = config.DefaultFontSize * 2.f * config.FontScale;
            else if (AreSame(stylePropVal, "xxx-large")) style.font.size = config.DefaultFontSize * 3.f * config.FontScale;
            else
                style.font.size = ExtractFloatWithUnit(stylePropVal, config.DefaultFontSize * config.FontScale,
                    config.DefaultFontSize * config.FontScale, parentStyle.font.size, config.FontScale);
            prop = StyleFontSize;

        }
        else if (AreSame(stylePropName, "font-weight"))
        {
            auto idx = SkipDigits(stylePropVal);

            if (idx == 0)
            {
                if (AreSame(stylePropVal, "bold")) style.font.bold = true;
                else if (AreSame(stylePropVal, "light")) style.font.light = true;
                else ERROR("Invalid font-weight property value... [%.*s]\n",
                    (int)stylePropVal.size(), stylePropVal.data());
            }
            else
            {
                int weight = ExtractInt(stylePropVal.substr(0u, idx), 400);
                style.font.bold = weight >= 600;
                style.font.light = weight < 400;
            }

            prop = StyleFontWeight;
        }
        else if (AreSame(stylePropName, "background-color") || AreSame(stylePropName, "background"))
        {
            style.bgcolor = ExtractColor(stylePropVal, config.NamedColor, config.UserData);
            prop = StyleBgColor;
        }
        else if (AreSame(stylePropName, "color"))
        {
            style.fgcolor = ExtractColor(stylePropVal, config.NamedColor, config.UserData);
            prop = StyleFgColor;
        }
        else if (AreSame(stylePropName, "width"))
        {
            style.width = ExtractFloatWithUnit(stylePropVal, 0, config.DefaultFontSize * config.FontScale, parentStyle.width, config.Scale);
            prop = StyleWidth;
        }
        else if (AreSame(stylePropName, "height"))
        {
            style.height = ExtractFloatWithUnit(stylePropVal, 0, config.DefaultFontSize * config.FontScale, parentStyle.height, config.Scale);
            prop = StyleHeight;
        }
        else if (AreSame(stylePropName, "alignment") || AreSame(stylePropName, "text-align"))
        {
            style.alignmentH = AreSame(stylePropVal, "justify") ? HorizontalAlignment::Justify :
                AreSame(stylePropVal, "right") ? HorizontalAlignment::Right :
                AreSame(stylePropVal, "center") ? HorizontalAlignment::Center :
                HorizontalAlignment::Left;
            prop = StyleHAlignment;
        }
        else if (AreSame(stylePropName, "vertical-align"))
        {
            style.alignmentV = AreSame(stylePropVal, "top") ? VerticalAlignment::Top :
                AreSame(stylePropVal, "bottom") ? VerticalAlignment::Bottom :
                VerticalAlignment::Center;
            prop = StyleVAlignment;
        }
        else if (AreSame(stylePropName, "font-family"))
        {
            style.font.family = stylePropVal;
            prop = StyleFontFamily;
        }
        else if (AreSame(stylePropName, "padding"))
        {
            auto val = ExtractFloatWithUnit(stylePropVal, 0.f, config.DefaultFontSize * config.FontScale, parentStyle.height, config.Scale);
            style.padding.top = style.padding.right = style.padding.left = style.padding.bottom = val;
            prop = StylePaddingBottom | StylePaddingLeft | StylePaddingRight |
                StylePaddingTop;
        }
        else if (AreSame(stylePropName, "padding-top"))
        {
            auto val = ExtractFloatWithUnit(stylePropVal, 0.f, config.DefaultFontSize * config.FontScale, parentStyle.height, config.Scale);
            style.padding.top = val;
            prop = StylePaddingTop;
        }
        else if (AreSame(stylePropName, "padding-bottom"))
        {
            auto val = ExtractFloatWithUnit(stylePropVal, 0.f, config.DefaultFontSize * config.FontScale, parentStyle.height, config.Scale);
            style.padding.bottom = val;
            prop = StylePaddingBottom;
        }
        else if (AreSame(stylePropName, "padding-left"))
        {
            auto val = ExtractFloatWithUnit(stylePropVal, 0.f, config.DefaultFontSize * config.FontScale, parentStyle.height, config.Scale);
            style.padding.left = val;
            prop = StylePaddingLeft;
        }
        else if (AreSame(stylePropName, "padding-right"))
        {
            auto val = ExtractFloatWithUnit(stylePropVal, 0.f, config.DefaultFontSize * config.FontScale, parentStyle.height, config.Scale);
            style.padding.right = val;
            prop = StylePaddingRight;
        }
        else if (AreSame(stylePropName, "font-style"))
        {
            if (AreSame(stylePropVal, "normal")) style.font.italics = false;
            else if (AreSame(stylePropVal, "italic") || AreSame(stylePropVal, "oblique"))
                style.font.italics = true;
            else ERROR("Invalid font-style property value [%.*s]\n",
                (int)stylePropVal.size(), stylePropVal.data());
            prop = StyleFontStyle;
        }
        else if (AreSame(stylePropName, "list-style-type"))
        {
            if (AreSame(stylePropVal, "circle")) style.list.itemStyle = BulletType::Circle;
            else if (AreSame(stylePropVal, "disk")) style.list.itemStyle = BulletType::Disk;
            else if (AreSame(stylePropVal, "square")) style.list.itemStyle = BulletType::Square;
            else if (AreSame(stylePropVal, "tickmark")) style.list.itemStyle = BulletType::CheckMark;
            else if (AreSame(stylePropVal, "checkbox")) style.list.itemStyle = BulletType::CheckBox;
            else if (AreSame(stylePropVal, "arrow")) style.list.itemStyle = BulletType::Arrow;
            else if (AreSame(stylePropVal, "triangle")) style.list.itemStyle = BulletType::Triangle;
            prop = StyleListBulletType;
        }
        else
        {
            ERROR("Invalid style property... [%.*s]\n", (int)stylePropName.size(), stylePropName.data());
        }

        return prop;
    }

    [[nodiscard]] RenderConfig* GetRenderConfig(RenderConfig* config = nullptr)
    {
        if (config == nullptr)
        {
            auto ctx = ImGui::GetCurrentContext();
            auto it = RenderConfigs.find(ctx);

            if (it == RenderConfigs.end())
            {
                if (!DefaultRenderConfigInit)
                {
                    LOG("Default config not available, forgot to push RenderConfig?");
                    GetDefaultConfig({ 200.f, 200.f }, 24.f);
                }

                config = &DefaultRenderConfig;
            }
            else config = &(it->second.back());
        }

        return config;
    }

    [[nodiscard]] ImVec2 GetTextSize(std::string_view content, ImFont* font)
    {
        ImGui::PushFont(font);
        auto sz = ImGui::CalcTextSize(content.data(), content.data() + content.size());
        ImGui::PopFont();
        return sz;
    }

    [[nodiscard]] std::optional<std::string_view> GetQuotedString(const char* text, int& idx, int end)
    {
        auto insideQuotedString = false;
        auto begin = idx;

        if ((idx < end) && text[idx] == '\'')
        {
            idx++;

            while (idx < end)
            {
                if (text[idx] == '\\' && ((idx + 1 < end) && text[idx + 1] == '\''))
                {
                    insideQuotedString = !insideQuotedString;
                    idx++;
                }
                else if (!insideQuotedString && text[idx] == '\'') break;
                idx++;
            }
        }
        else if ((idx < end) && text[idx] == '"')
        {
            idx++;

            while (idx < end)
            {
                if (text[idx] == '\\' && ((idx + 1 < end) && text[idx + 1] == '"'))
                {
                    insideQuotedString = !insideQuotedString;
                    idx++;
                }
                else if (!insideQuotedString && text[idx] == '"') break;
                idx++;
            }
        }

        if ((idx < end) && (text[idx] == '"' || text[idx] == '\''))
        {
            std::string_view res{ text + begin + 1, (std::size_t)(idx - begin - 1) };
            idx++;
            return res;
        }
        else if (idx > begin)
        {
            ERROR("Quoted string invalid... [%.*s] at %d\n", (int)(idx - begin), text + begin, idx);
        }

        return std::nullopt;
    }

    [[nodiscard]] SegmentStyle CreateDefaultStyle(const RenderConfig& config)
    {
        assert(config.GetFont != nullptr);

        SegmentStyle result;
        result.font.family = config.DefaultFontFamily;
        result.font.size = config.DefaultFontSize * config.FontScale;
        result.font.font = config.GetFont(result.font.family, result.font.size, false, false, false, config.UserData);
        result.bgcolor = config.DefaultBgColor;
        result.fgcolor = config.DefaultFgColor;
        result.list.itemStyle = config.ListItemBullet;
        return result;
    }

    [[nodiscard]] ImVec2 GetSegmentSize(const SegmentDetails& segment, const RenderConfig& config)
    {
        auto height = 0.f, width = 0.f;

        for (const auto& token : segment.Tokens)
        {
            height = std::max(height, token.Bounds.height);
            width += token.Bounds.width;
        }

        return { width, height };
    }

    [[nodiscard]] ImVec2 GetLineSize(const DrawableLine& line, const RenderConfig& config)
    {
        auto height = 0.f, width = 0.f;

        for (const auto& segment : line.Segments)
        {
            auto sz = GetSegmentSize(segment, config);
            auto offseth = segment.Style.subscriptOffset + segment.Style.superscriptOffset +
                segment.Style.padding.top + segment.Style.padding.bottom;
            height = std::max(height, sz.y + offseth);
            width += sz.x + segment.Style.padding.left + segment.Style.padding.right;
        }

        return { width, height };
    }

    void AddToken(DrawableLine& line, Token token, int propsChanged, const RenderConfig& config)
    {
        auto& segment = line.Segments.back();

        if (token.Type == TokenType::Text)
        {
            auto sz = config.GetTextSize(token.Content, segment.Style.font.font);
            token.Bounds.width = sz.x;
            token.Bounds.height = sz.y;
        }
        else if (token.Type == TokenType::HorizontalRule)
        {
            token.Bounds.width = config.Bounds.x - line.Content.left - (line.Offset.right + line.Offset.left) - 
                (segment.Style.padding.left + segment.Style.padding.right);
            token.Bounds.height = segment.Style.height;
        }
        else if (token.Type == TokenType::ListItemBullet)
        {
            auto bulletscale = Clamp(config.BulletSizeScale, 1.f, 4.f);
            auto bulletsz = (segment.Style.font.size) / bulletscale;
            token.Bounds.width = token.Bounds.height = bulletsz;
        }
        else if (token.Type == TokenType::ListItemNumbered)
        {
            if (NumbersAsStr.empty())
            {
                NumbersAsStr.reserve(IM_RICHTEXT_MAX_LISTITEM);

                for (auto num = 1; num <= IM_RICHTEXT_MAX_LISTITEM; ++num)
                    NumbersAsStr.emplace_back(std::to_string(num));
            }

            std::memset(token.NestedListItemIndex, 0, IM_RICHTEXT_NESTED_ITEMCOUNT_STRSZ);
            auto currbuf = 0;

            for (auto depth = 0; depth <= token.ListDepth && currbuf < IM_RICHTEXT_NESTED_ITEMCOUNT_STRSZ; ++depth)
            {
                auto itemcount = ListItemCountByDepths[depth] - 1;
                auto itemlen = itemcount > 99 ? 3 : itemcount > 9 ? 2 : 1;
                std::memcpy(token.NestedListItemIndex + currbuf, NumbersAsStr[itemcount].data(), itemlen);
                currbuf += itemlen;

                token.NestedListItemIndex[currbuf] = '.';
                currbuf += 1;
            }

            std::string_view input{ token.NestedListItemIndex, (size_t)currbuf };
            auto sz = config.GetTextSize(input, segment.Style.font.font);
            token.Bounds.width = sz.x;
            token.Bounds.height = sz.y;
        }
        else if (token.Type == TokenType::Meter)
        {
            if ((propsChanged & StyleWidth) == 0) token.Bounds.width = config.MeterDefaultSize.x;
            if ((propsChanged & StyleHeight) == 0) token.Bounds.height = config.MeterDefaultSize.y;
        }

        segment.Tokens.emplace_back(token);
        segment.HasText = segment.HasText || (!token.Content.empty());
        line.HasText = line.HasText || segment.HasText;
        line.HasSubscript = line.HasSubscript || segment.SubscriptDepth > 0;
        line.HasSuperscript = line.HasSuperscript || segment.SuperscriptDepth > 0;

        LOG("Added token: %.*s [itemtype: %s][font-size: %f][size: (%f, %f)]\n",
            (int)token.Content.size(), token.Content.data(),
            GetTokenTypeString(token), segment.Style.font.size,
            token.Bounds.width, token.Bounds.height);
    }

    SegmentDetails& AddSegment(DrawableLine& line, const SegmentStyle& styleprops, const RenderConfig& config)
    {
        if (!line.Segments.empty())
        {
            auto& lastSegment = line.Segments.back();
            auto sz = GetSegmentSize(lastSegment, config);
            lastSegment.Bounds.width = sz.x;
            lastSegment.Bounds.height = sz.y;
        }

        auto& segment = line.Segments.emplace_back();
        segment.Style = styleprops;
        return segment;
    }

    [[nodiscard]] DrawableLine CreateNewLine(const SegmentStyle& styleprops, const RenderConfig& config)
    {
        DrawableLine line;
        line.Segments.emplace_back();
        line.Segments.back().Style = styleprops;
        line.BlockquoteDepth = -1;
        return line;
    }

    enum class TagType
    {
        Unknown,
        Bold,
        Italics,
        Underline,
        Strikethrough,
        Mark,
        Small,
        Span,
        List,
        ListItem,
        Paragraph,
        Header,
        RawText,
        Blockquote,
        Subscript,
        Superscript,
        Quotation,
        Hr,
        Abbr,
        LineBreak,
        CodeBlock,
        Hyperlink,
        Blink,
        Marquee,
        Meter
    };

    std::vector<int> PerformWordWrap(std::deque<DrawableLine>& lines, TagType tagType, int index, int listDepth, int blockquoteDepth,
        const RenderConfig& config)
    {
        std::vector<int> result;
        if (!lines[index].HasText || !config.WordWrap || !config.GetTextSize)
        {
            result.push_back((int)lines.size() - 1);
            return result;
        }

        std::deque<DrawableLine> newlines;
        auto currline = CreateNewLine(lines.front().Segments.front().Style, config);
        auto currentx = 0.f;
        auto availwidth = lines[index].Content.width;

        for (const auto& segment : lines[index].Segments)
        {
            for (const auto& token : segment.Tokens)
            {
                if (token.Type == TokenType::Text)
                {
                    auto sz = config.GetTextSize(token.Content, segment.Style.font.font);

                    if (currentx + sz.x > availwidth)
                    {
                        newlines.push_back(currline);
                        currline.Segments.clear();
                        currline.Segments.emplace_back();
                        currentx = 0.f;
                    }

                    currline.Segments.back().Style = segment.Style;
                    currline.Segments.back().Tokens.emplace_back(token);
                    currentx += sz.x;
                }
            }
        }

        auto it = lines.erase(lines.begin() + index);
        auto sz = (int)lines.size();
        lines.insert(it, newlines.begin(), newlines.end());
        for (auto idx = 0; idx < (int)newlines.size(); ++idx) result.push_back(sz + idx);
        return result;
    }

    float CalcVerticalOffset(int maxSuperscriptDepth, float baseFontSz, float scale)
    {
        float sum = 0.f, multiplier = scale;
        for (auto idx = 1; idx <= maxSuperscriptDepth; ++idx)
        {
            sum += multiplier;
            multiplier *= multiplier;
        }
        return sum * (baseFontSz * 0.5f);
    }

    float GetMaxSuperscriptOffset(const DrawableLine& line, float scale)
    {
        auto topOffset = 0.f;
        auto baseFontSz = 0.f;

        for (auto idx = 0; idx < (int)line.Segments.size();)
        {
            const auto& segment = line.Segments[idx];
            baseFontSz = segment.Style.font.size;
            auto depth = 0, begin = idx;

            while ((idx < (int)line.Segments.size()) && (line.Segments[idx].SuperscriptDepth > 0))
            {
                depth = std::max(depth, segment.SuperscriptDepth);
                idx++;
            }

            topOffset = std::max(topOffset, CalcVerticalOffset(depth, baseFontSz, scale));
            if (idx == begin) idx++;
        }

        return topOffset;
    }

    float GetMaxSubscriptOffset(const DrawableLine& line, float scale)
    {
        auto topOffset = 0.f;
        auto baseFontSz = 0.f;

        for (auto idx = 0; idx < (int)line.Segments.size();)
        {
            const auto& segment = line.Segments[idx];
            baseFontSz = segment.Style.font.size;
            auto depth = 0, begin = idx;

            while ((idx < (int)line.Segments.size()) && (line.Segments[idx].SubscriptDepth > 0))
            {
                depth = std::max(depth, segment.SubscriptDepth);
                idx++;
            }

            topOffset = std::max(topOffset, CalcVerticalOffset(depth, baseFontSz, scale));
            if (idx == begin) idx++;
        }

        return topOffset;
    }

    void AdjustForSuperSubscripts(const std::vector<int>& indexes, std::deque<DrawableLine>& lines, const RenderConfig& config)
    {
        for (auto idx : indexes)
        {
            auto& line = lines[idx];
            if (!line.HasSubscript && !line.HasSuperscript) continue;

            auto maxTopOffset = GetMaxSuperscriptOffset(line, config.ScaleSuperscript);
            auto maxBottomOffset = GetMaxSubscriptOffset(line, config.ScaleSubscript);
            auto lastFontSz = config.DefaultFontSize * config.FontScale;
            auto lastSuperscriptDepth = 0, lastSubscriptDepth = 0;

            for (auto& segment : line.Segments)
            {
                if (segment.SuperscriptDepth > lastSuperscriptDepth)
                {
                    segment.Style.font.size = lastFontSz * config.ScaleSuperscript;
                    maxTopOffset -= segment.Style.font.size * 0.5f;
                }
                else if (segment.SuperscriptDepth < lastSuperscriptDepth)
                {
                    maxTopOffset += lastFontSz * 0.5f;
                    segment.Style.font.size = lastFontSz / config.ScaleSuperscript;
                }

                if (segment.SubscriptDepth > lastSubscriptDepth)
                {
                    segment.Style.font.size = lastFontSz * config.ScaleSubscript;
                    maxBottomOffset += (lastFontSz - segment.Style.font.size * 0.5f);
                }
                else if (segment.SubscriptDepth < lastSubscriptDepth)
                {
                    segment.Style.font.size = lastFontSz / config.ScaleSubscript;
                    maxBottomOffset -= segment.Style.font.size * 0.5f;
                }

                segment.Style.superscriptOffset = maxTopOffset;
                segment.Style.subscriptOffset = maxBottomOffset;
                segment.Bounds.height += maxTopOffset + maxBottomOffset;

                lastSuperscriptDepth = segment.SuperscriptDepth;
                lastSubscriptDepth = segment.SubscriptDepth;
                lastFontSz = segment.Style.font.size;
            }
        }
    }

    [[nodiscard]] bool IsLineEmpty(const DrawableLine& line)
    {
        bool isEmpty = true;

        for (const auto& segment : line.Segments)
            isEmpty = isEmpty && segment.Tokens.empty();

        return isEmpty;
    }

    void ComputeLineBounds(std::deque<DrawableLine>& result, const std::vector<int>& linesModified, 
        const RenderConfig& config)
    {
        for (auto index : linesModified)
        {
            auto& line = result[index];
            auto currx = line.Content.left + line.Offset.left;
            auto sz = GetLineSize(line, config);
            line.Content.width = sz.x;
            line.Content.height = sz.y;

            if (index > 0) line.Content.top = result[index - 1].Content.top + result[index - 1].height() + config.LineGap;

            for (auto& segment : line.Segments)
            {
                // This will align the segment as vertical centre, TODO: Handle other alignments
                segment.Bounds.top = line.Content.top + line.Offset.top + ((line.Content.height - segment.height()) * 0.5f);
                segment.Bounds.left = currx; // This will align left, TODO: Handle other alignments

                currx += segment.Style.padding.left;

                for (auto tokidx = 0; tokidx < (int)segment.Tokens.size(); ++tokidx)
                {
                    auto& token = segment.Tokens[tokidx];
                    token.Bounds.top = segment.Bounds.top + segment.Style.padding.top +
                        +segment.Style.superscriptOffset + segment.Style.subscriptOffset +
                        ((segment.Bounds.height - token.Bounds.height) * 0.5f);

                    // TODO: Fix bullet positioning w.r.t. first text block (baseline aligned?)
                    /*if ((token.Type == TokenType::ListItemBullet) && ((tokidx + 1) < (int)segment.Tokens.size()))
                         segment.Tokens[tokidx + 1]*/
                    token.Bounds.left = currx;
                    currx += token.Bounds.width;
                }

                currx += segment.Style.padding.right;
            }

            HIGHLIGHT("\nCreated line #%d at (%f, %f) of size (%f, %f) with %d segments", index,
                line.Content.left, line.Content.top, line.Content.width, line.Content.height,
                (int)line.Segments.size());
        }
    }

    DrawableLine MoveToNextLine(const SegmentStyle& styleprops, TagType tagType, int listDepth, int blockquoteDepth,
        DrawableLine& line, std::deque<DrawableLine>& result, const RenderConfig& config, bool isTagStart)
    {
        auto isEmpty = IsLineEmpty(line);
        result.emplace_back(line);
        std::vector<int> linesModified;

        if (line.Segments.size() == 1u && line.Segments.front().Tokens.size() == 1u &&
            line.Segments.front().Tokens.front().Type == TokenType::HorizontalRule)
        {
            linesModified.push_back((int)result.size() - 1);
        }
        else
        {
            if (!line.Marquee && config.Bounds.x > 0.f)
                linesModified = PerformWordWrap(result, tagType, (int)result.size() - 1, listDepth, blockquoteDepth, config);
            else linesModified.push_back((int)result.size() - 1);
            AdjustForSuperSubscripts(linesModified, result, config);
        }

        auto& lastline = result.back();
        auto newline = CreateNewLine(styleprops, config);
        newline.BlockquoteDepth = blockquoteDepth;
        if (isTagStart) newline.Marquee = tagType == TagType::Marquee;

        if (blockquoteDepth > 0) newline.Offset.left = newline.Offset.right = config.BlockquotePadding;
        if (blockquoteDepth > lastline.BlockquoteDepth) newline.Offset.top = config.BlockquotePadding;
        else if (blockquoteDepth < lastline.BlockquoteDepth) lastline.Offset.bottom = config.BlockquotePadding;

        ComputeLineBounds(result, linesModified, config);

        newline.Content.left = ((float)(listDepth + 1) * config.ListItemIndent) + 
            ((float)(blockquoteDepth + 1) * config.BlockquoteOffset);
        newline.Content.top = lastline.Content.top + lastline.height() + (isEmpty ? 0.f : config.LineGap);
        return newline;
    }

    void GenerateTextToken(DrawableLine& line, std::string_view content,
        int curridx, int start, const RenderConfig& config)
    {
        Token token;
        token.Content = content.substr(start, curridx - start);
        AddToken(line, token, NoStyleChange, config);
    }

    [[nodiscard]] std::pair<bool, bool> AddEscapeSequences(const std::string_view content,
        int curridx, const std::vector<std::pair<std::string_view, std::string_view>>& escapeCodes,
        char escapeStart, char escapeEnd, DrawableLine& line, int& start)
    {
        Token token;
        auto hasEscape = false;
        auto isNewLine = false;

        for (const auto& pair : escapeCodes)
        {
            auto sz = (int)pair.first.size();
            if ((curridx + sz) < (int)content.size() &&
                AreSame(content.substr(curridx, sz), pair.first) &&
                content[curridx + sz] == escapeEnd)
            {
                if (pair.second == "\n") isNewLine = true;
                else
                {
                    token.Content = pair.second;
                    line.Segments.back().Tokens.emplace_back(token);
                }

                curridx += sz + 1;
                start = curridx;
                hasEscape = true;
                break;
            }
        }

        return { hasEscape, isNewLine };
    }

    bool IsStyleSupported(TagType type)
    {
        switch (type)
        {
            case TagType::Unknown:
            case TagType::Bold:
            case TagType::Italics:
            case TagType::Underline:
            case TagType::Strikethrough:
            case TagType::Small:
            case TagType::LineBreak:
                return false;
            default: return true;
        }
    }

    int ExtractAttributes(const char* text, int end, std::string_view currTag, TagType tagType, const RenderConfig& config, SegmentStyle& style, const SegmentStyle& initialStyle, int& idx)
    {
        const auto& parentStyle = CurrentStackPos > 0 ? TagStack[CurrentStackPos - 1].second : initialStyle;
        int result = NoStyleChange;

        // Extract all attributes
        while ((idx < end) && (text[idx] != config.TagEnd) && (text[idx] != '/'))
        {
            auto begin = idx;
            while ((idx < end) && (text[idx] != '=') && !std::isspace(text[idx]) && (text[idx] != '/')) idx++;

            if (text[idx] != '/')
            {
                auto attribName = std::string_view{ text + begin, (std::size_t)(idx - begin) };
                LOG("Attribute: %.*s\n", (int)attribName.size(), attribName.data());

                idx = SkipSpace(text, idx, end);
                if (text[idx] == '=') idx++;
                idx = SkipSpace(text, idx, end);
                auto attribValue = GetQuotedString(text, idx, end);

                if (AreSame(attribName, "style") && IsStyleSupported(tagType))
                {
                    if (!attribValue.has_value())
                    {
                        ERROR("Style attribute value not specified...");
                        return false;
                    }
                    else
                    {
                        auto sidx = 0;
                        auto styleProps = attribValue.value();

                        while (sidx < (int)styleProps.size())
                        {
                            sidx = SkipSpace(styleProps, sidx);
                            auto stbegin = sidx;
                            while ((sidx < (int)styleProps.size()) && (styleProps[sidx] != ':') &&
                                !std::isspace(styleProps[sidx])) sidx++;
                            auto stylePropName = styleProps.substr(stbegin, sidx - stbegin);

                            sidx = SkipSpace(styleProps, sidx);
                            if (styleProps[sidx] == ':') sidx++;
                            sidx = SkipSpace(styleProps, sidx);

                            auto stylePropVal = GetQuotedString(styleProps.data(), sidx, end);
                            if (!stylePropVal.has_value() || stylePropVal.value().empty())
                            {
                                stbegin = sidx;
                                while ((sidx < (int)styleProps.size()) && styleProps[sidx] != ';') sidx++;
                                stylePropVal = styleProps.substr(stbegin, sidx - stbegin);

                                if (styleProps[sidx] == ';') sidx++;
                            }

                            if (stylePropVal.has_value())
                            {
                                auto prop = PopulateSegmentStyle(style, parentStyle, stylePropName, stylePropVal.value(), config);
                                result = result | prop;
                            }
                        }
                    }
                }
                else if (tagType == TagType::Abbr && AreSame(attribName, "title") && attribValue.has_value())
                    style.tooltip = attribValue.value();
                else if (tagType == TagType::Hyperlink && AreSame(attribName, "href") && attribValue.has_value())
                    style.link = attribValue.value();
                else if (tagType == TagType::Meter)
                {
                    if (AreSame(attribName, "value") && attribValue.has_value()) style.value = ExtractInt(attribValue.value(), 0);
                    if (AreSame(attribName, "min") && attribValue.has_value()) style.range.first = ExtractInt(attribValue.value(), 0);
                    if (AreSame(attribName, "max") && attribValue.has_value()) style.range.second = ExtractInt(attribValue.value(), 0);
                }
                else if (config.HandleAttribute)
                    config.HandleAttribute(currTag, attribName, attribValue.value_or(std::string_view{}), config.UserData);
            }
        }

        if (text[idx] == config.TagEnd) idx++;
        if (text[idx] == '/' && ((idx + 1) < end) && text[idx + 1] == config.TagEnd) idx += 2;
        return result;
    }

    std::pair<std::string_view, bool> ExtractTag(const char* text, int end, const RenderConfig& config,
        int& idx, bool& tagStart)
    {
        std::pair<std::string_view, bool> result;
        result.second = true;

        if (text[idx] == '/')
        {
            tagStart = false;
            idx++;
        }
        else if (!std::isalnum(text[idx]))
        {
            ERROR("Invalid tag at %d...\n", idx);
            result.second = false;
            return result;
        }

        auto begin = idx;
        while ((idx < end) && !std::isspace(text[idx]) && (text[idx] != config.TagEnd)) idx++;

        if (idx - begin == 0)
        {
            ERROR("Empty tag at %d...\n", begin);
            result.second = false;
            return result;
        }

        result.first = std::string_view{ text + begin, (std::size_t)(idx - begin) };
        if (result.first.back() == '/') result.first = result.first.substr(0, result.first.size() - 1u);

        if (!tagStart)
        {
            if (text[idx] == config.TagEnd) idx++;

            if (result.first.empty())
            {
                ERROR("Empty tag at %d...\n", begin);
                result.second = false;
                return result;
            }
            if (CurrentStackPos > -1 && result.first != TagStack[CurrentStackPos].first)
            {
                ERROR("Closing tag <%.*s> doesnt match opening tag <%.*s>...",
                    (int)result.first.size(), result.first.data(), (int)TagStack[CurrentStackPos].first.size(),
                    TagStack[CurrentStackPos].first.data());
                result.second = false;
                return result;
            }
        }

        idx = SkipSpace(text, idx, end);
        return result;
    }

    SegmentStyle& PushTag(std::string_view currTag, const SegmentStyle& initialStyle)
    {
        CurrentStackPos++;
        TagStack[CurrentStackPos].first = currTag;
        TagStack[CurrentStackPos].second = CurrentStackPos > 0 ?
            TagStack[CurrentStackPos - 1].second : initialStyle;
        return TagStack[CurrentStackPos].second;
    }

    void PopTag(bool reset)
    {
        if (reset) TagStack[CurrentStackPos] = std::make_pair(std::string_view{}, SegmentStyle{});
        --CurrentStackPos;
    }

    TagType GetTagType(std::string_view currTag)
    {
        if (AreSame(currTag, "b") || AreSame(currTag, "strong")) return TagType::Bold;
        else if (AreSame(currTag, "i") || AreSame(currTag, "em") || AreSame(currTag, "cite") || AreSame(currTag, "var")) 
            return TagType::Italics;
        else if (AreSame(currTag, "hr")) return TagType::Hr;
        else if (AreSame(currTag, "br")) return TagType::LineBreak;
        else if (AreSame(currTag, "span")) return TagType::Span;
        else if (AreSame(currTag, "a")) return TagType::Hyperlink;
        else if (AreSame(currTag, "sub")) return TagType::Subscript;
        else if (AreSame(currTag, "sup")) return TagType::Superscript;
        else if (AreSame(currTag, "mark")) return TagType::Mark;
        else if (AreSame(currTag, "small")) return TagType::Small;
        else if (AreSame(currTag, "ul") || AreSame(currTag, "ol")) return TagType::List;
        else if (AreSame(currTag, "p")) return TagType::Paragraph;
        else if (currTag.size() == 2u && (currTag[0] == 'h' || currTag[0] == 'H') && std::isdigit(currTag[1])) return TagType::Header;
        else if (AreSame(currTag, "li")) return TagType::ListItem;
        else if (AreSame(currTag, "q")) return TagType::Quotation;
        else if (AreSame(currTag, "pre") || AreSame(currTag, "samp")) return TagType::RawText;
        else if (AreSame(currTag, "u")) return TagType::Underline;
        else if (AreSame(currTag, "s") || AreSame(currTag, "del")) return TagType::Strikethrough;
        else if (AreSame(currTag, "blockquote")) return TagType::Blockquote;
        else if (AreSame(currTag, "code")) return TagType::CodeBlock;
        else if (AreSame(currTag, "abbr")) return TagType::Abbr;
        else if (AreSame(currTag, "blink")) return TagType::Blink;
        else if (AreSame(currTag, "marquee")) return TagType::Marquee;
        else if (AreSame(currTag, "meter")) return TagType::Meter;
        return TagType::Unknown;
    }

    SegmentDetails& UpdateSegmentStyle(TagType tagType, std::string_view currTag, 
        SegmentStyle& styleprops, const SegmentStyle& parentStyle,
        DrawableLine& line, int propsChanged, const RenderConfig& config)
    {
        assert(config.GetFont != nullptr);

        if (tagType == TagType::Header)
        {
            styleprops.font.size = config.HFontSizes[currTag[1] - '1'] * config.FontScale;
            styleprops.font.bold = true;
            styleprops.font.font = config.GetFont(styleprops.font.family, styleprops.font.size,
                styleprops.font.bold, styleprops.font.italics, styleprops.font.light, config.UserData);
        }
        else if (tagType == TagType::RawText || tagType == TagType::CodeBlock)
        {
            styleprops.font.family = IM_RICHTEXT_MONOSPACE_FONTFAMILY;
            propsChanged = propsChanged | StyleFontFamily;

            if (tagType == TagType::CodeBlock)
            {
                if ((propsChanged & StyleBgColor) == 0) styleprops.bgcolor = config.CodeBlockBg;
            }
        }
        else if (tagType == TagType::Italics)
        {
            styleprops.font.italics = true;
            propsChanged = propsChanged | StyleFontStyle;
        }
        else if (tagType == TagType::Bold)
        {
            styleprops.font.bold = true;
            propsChanged = propsChanged | StyleFontStyle;
        }
        else if (tagType == TagType::Mark)
        {
            if ((propsChanged & StyleBgColor) == 0) styleprops.bgcolor = config.MarkHighlight;
            propsChanged = propsChanged | StyleBgColor;
        }
        else if (tagType == TagType::Small)
        {
            styleprops.font.size = parentStyle.font.size * 0.8f;
            propsChanged = propsChanged | StyleFontSize;
        }
        else if (tagType == TagType::Superscript)
        {
            styleprops.font.size *= config.ScaleSuperscript;
            propsChanged = propsChanged | StyleFontSize;
        }
        else if (tagType == TagType::Subscript)
        {
            styleprops.font.size *= config.ScaleSubscript;
            propsChanged = propsChanged | StyleFontSize;
        }
        else if (tagType == TagType::Underline)
        {
            styleprops.font.underline = true;
            propsChanged = propsChanged | StyleFontStyle;
        }
        else if (tagType == TagType::Strikethrough)
        {
            styleprops.font.strike = true;
            propsChanged = propsChanged | StyleFontStyle;
        }
        else if (tagType == TagType::ListItem)
        {
            styleprops.padding.right = config.ListItemOffset;
        }
        else if (tagType == TagType::Hyperlink)
        {
            if ((propsChanged & StyleFontStyle) == 0) styleprops.font.underline = true;
            if ((propsChanged & StyleFgColor) == 0) styleprops.fgcolor = config.HyperlinkColor;
            propsChanged = propsChanged | StyleFontStyle;
            propsChanged = propsChanged | StyleFgColor;
        }
        else if (tagType == TagType::Blink) styleprops.blink = true;
        else if (tagType == TagType::Meter)
        {
            if ((propsChanged & StyleBgColor) == 0) styleprops.bgcolor = config.MeterBgColor;
            if ((propsChanged & StyleFgColor) == 0) styleprops.fgcolor = config.MeterFgColor;
            propsChanged = propsChanged | StyleBgColor;
            propsChanged = propsChanged | StyleFgColor;
        }
        
        if (propsChanged != NoStyleChange || tagType == TagType::Abbr ||
            tagType == TagType::Blink)
        {
            styleprops.font.font = config.GetFont(styleprops.font.family, styleprops.font.size,
                styleprops.font.bold, styleprops.font.italics, styleprops.font.light, config.UserData);
            AddSegment(line, styleprops, config);
        }

        return line.Segments.back();
    }

#ifdef _DEBUG
    inline void DrawDebugRect(DebugContentType type, ImDrawList* drawList, ImVec2 startpos, ImVec2 endpos, const RenderConfig& config)
    {
        if (config.DebugContents[type].Value != ImColor{ IM_COL32_BLACK_TRANS }.Value) 
            drawList->AddRect(startpos, endpos, config.DebugContents[type]);
    }
#else
#define DrawDebugRect(...)
#endif

    bool DrawToken(ImDrawList* drawList, int lineidx, const Token& token, ImVec2 initpos,
        ImVec2 bounds, const SegmentStyle& style, const RenderConfig& config, 
        TooltipData& tooltip, AnimationData& animation, bool isMarquee)
    {
        if (style.blink && !animation.isVisible) return true;

        if (token.Type == TokenType::HorizontalRule)
        {
            drawList->AddRectFilled(token.Bounds.start(initpos), token.Bounds.end(initpos), style.fgcolor);
            DrawDebugRect(ContentTypeToken, drawList, token.Bounds.start(initpos), token.Bounds.end(initpos), config);
        }
        else if (token.Type == TokenType::ListItemBullet)
        {
            auto bulletscale = Clamp(config.BulletSizeScale, 1.f, 4.f);
            auto bulletsz = (style.font.size) / bulletscale;

            switch (style.list.itemStyle)
            {
            case BulletType::Circle: {
                ImVec2 center = token.Bounds.center(initpos);
                drawList->AddCircle(center, bulletsz * 0.5f, style.fgcolor);
                break;
            }

            case BulletType::Disk: {
                ImVec2 center = token.Bounds.center(initpos);
                drawList->AddCircleFilled(center, bulletsz * 0.5f, style.fgcolor);
                break;
            }

            case BulletType::Square: {
                drawList->AddRectFilled(token.Bounds.start(initpos), token.Bounds.end(initpos), style.fgcolor);
                break;
            }

            case BulletType::Concentric: {
                ImVec2 center = token.Bounds.center(initpos);
                drawList->AddCircle(center, bulletsz * 0.5f, style.fgcolor);
                drawList->AddCircleFilled(center, bulletsz * 0.4f, style.fgcolor);
                break;
            }

            case BulletType::Triangle: {
                auto startpos = token.Bounds.start(initpos);
                auto offset = bulletsz * 0.25f;
                ImVec2 a{ startpos.x, startpos.y },
                       b{ startpos.x + bulletsz, startpos.y + (bulletsz * 0.5f) },
                       c{ startpos.x, startpos.y + bulletsz };
                drawList->AddTriangleFilled(a, b, c, style.fgcolor);
                break;
            }

            case BulletType::Arrow: {
                auto startpos = token.Bounds.start(initpos);
                auto bsz2 = bulletsz * 0.5f;
                auto bsz3 = bulletsz * 0.33333f;
                auto bsz6 = bsz3 * 0.5f;
                auto bsz38 = bulletsz * 0.375f;
                ImVec2 points[7];
                points[0] = { startpos.x, startpos.y + bsz38 };
                points[1] = { startpos.x + bsz2, startpos.y + bsz38 };
                points[2] = { startpos.x + bsz2, startpos.y + bsz6 };
                points[3] = { startpos.x + bulletsz, startpos.y + bsz2 };
                points[4] = { startpos.x + bsz2, startpos.y + bulletsz - bsz6 };
                points[5] = { startpos.x + bsz2, startpos.y + bulletsz - bsz38 };
                points[6] = { startpos.x, startpos.y + bulletsz - bsz38 };
                drawList->AddRectFilled(points[0], points[5], style.fgcolor);
                drawList->AddTriangleFilled(points[2], points[3], points[4], style.fgcolor);
                break;
            }

            case BulletType::CheckMark: {
                auto startpos = token.Bounds.start(initpos);
                auto bsz3 = (bulletsz * 0.25f);
                auto thickness = bulletsz * 0.2f;
                ImVec2 points[3];
                points[0] = { startpos.x, startpos.y + (2.5f * bsz3) };
                points[1] = { startpos.x + (bulletsz * 0.3333f), startpos.y + bulletsz};
                points[2] = { startpos.x + bulletsz, startpos.y + bsz3 };
                drawList->AddPolyline(points, 3, style.fgcolor, 0, thickness);
                break;
            }

            case BulletType::CheckBox: {
                auto startpos = token.Bounds.start(initpos);
                auto checkpos = ImVec2{ startpos.x + (bulletsz * 0.25f), startpos.y + (bulletsz * 0.25f) };
                bulletsz *= 0.75f;
                auto bsz3 = (bulletsz * 0.25f);
                auto thickness = bulletsz * 0.25f;
                ImVec2 points[3];
                points[0] = { checkpos.x, checkpos.y + (2.5f * bsz3) };
                points[1] = { checkpos.x + (bulletsz * 0.3333f), checkpos.y + bulletsz };
                points[2] = { checkpos.x + bulletsz, checkpos.y + bsz3 };
                drawList->AddPolyline(points, 3, style.fgcolor, 0, thickness);
                drawList->AddRect(startpos, token.Bounds.end(initpos), style.fgcolor, thickness);
                break;
            }

            case BulletType::Custom: {
                if (config.DrawBullet != nullptr) {
                    config.DrawBullet(token.Bounds.start(initpos), token.Bounds.end(initpos), style, token.ListItemIndex, 
                        token.ListDepth, config.UserData);
                }
                else {
                    ImVec2 center = token.Bounds.center(initpos);
                    drawList->AddCircleFilled(center, bulletsz * 0.5f, style.fgcolor);
                }
                break;
            }

            default:
                break;
            }

            DrawDebugRect(ContentTypeToken, drawList, token.Bounds.start(initpos), token.Bounds.end(initpos), config);
        }
        else if (token.Type == TokenType::ListItemNumbered)
        {
            drawList->AddText(token.Bounds.start(initpos), style.fgcolor, token.NestedListItemIndex);
            DrawDebugRect(ContentTypeToken, drawList, token.Bounds.start(initpos), token.Bounds.end(initpos), config);
        }
        else if (token.Type == TokenType::Meter)
        {
            auto start = token.Bounds.start(initpos);
            auto end = token.Bounds.end(initpos);
            auto border = ImVec2{ 1.f, 1.f };
            auto borderRadius = (end.y - start.y) * 0.5f;
            auto diff = style.range.second - style.range.first;
            auto progress = (style.value / diff) * token.Bounds.width;

            drawList->AddRectFilled(start, end, style.bgcolor, borderRadius, ImDrawFlags_RoundCornersAll);
            drawList->AddRect(start, end, config.MeterBorderColor, borderRadius, ImDrawFlags_RoundCornersAll);
            drawList->AddRectFilled(start + border, start - border + ImVec2{ progress, token.Bounds.height }, 
                style.fgcolor, borderRadius, ImDrawFlags_RoundCornersBottomLeft | ImDrawFlags_RoundCornersTopLeft);
        }
        else
        {
            auto textend = token.Content.data() + token.Content.size();
            auto startpos = token.Bounds.start(initpos);
            auto endpos = token.Bounds.end(initpos);
            auto halfh = token.Bounds.height * 0.5f;
            if (isMarquee) startpos.x += animation.xoffsets[lineidx];

            if (style.bgcolor.Value != config.DefaultBgColor.Value) drawList->AddRectFilled(startpos, endpos, style.bgcolor);
            drawList->AddText(startpos, style.fgcolor, token.Content.data(), textend);
            if (style.font.strike) drawList->AddLine(startpos + ImVec2{ 0.f, halfh }, endpos + ImVec2{ 0.f, -halfh }, style.fgcolor);
            if (style.font.underline) drawList->AddLine(startpos + ImVec2{ 0.f, token.Bounds.height }, endpos, style.fgcolor);

            if (!style.tooltip.empty())
            {
                if (!style.font.underline)
                {
                    auto posx = startpos.x;
                    while (posx < endpos.x)
                    {
                        drawList->AddCircleFilled(ImVec2{ posx, endpos.y }, 1.f, style.fgcolor);
                        posx += 3.f;
                    }
                }

                auto mousepos = ImGui::GetIO().MousePos;
                if (ImRect{ startpos, endpos }.Contains(mousepos))
                {
                    tooltip.pos = mousepos;
                    tooltip.content = style.tooltip;
                }
            }
            else if (!style.link.empty())
            {
                if (ImRect{ startpos, endpos }.Contains(ImGui::GetIO().MousePos))
                {
                    ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                    if (ImGui::GetIO().MouseReleased[0] && config.HandleHyperlink)
                        config.HandleHyperlink(style.link, config.UserData);
                }
                else
                    ImGui::SetMouseCursor(ImGuiMouseCursor_Arrow);
            }

            DrawDebugRect(ContentTypeToken, drawList, startpos, endpos, config);
        }

        if ((token.Bounds.left + token.Bounds.width) > (bounds.x + initpos.x)) return false;
        return true;
    }

    bool DrawSegment(ImDrawList* drawList, int lineidx, const SegmentDetails& segment,
        ImVec2 initpos, ImVec2 bounds, const RenderConfig& config, 
        TooltipData& tooltip, AnimationData& animation, bool isMarquee)
    {
        auto popFont = false;
        if (segment.Style.font.font != nullptr)
        {
            ImGui::PushFont(segment.Style.font.font);
            popFont = true;
        }

        const auto& style = segment.Style;
        auto drawTokens = true;

        for (const auto& token : segment.Tokens)
        {
            if (drawTokens && !DrawToken(drawList, lineidx, token, initpos, bounds, style, 
                config, tooltip, animation, isMarquee))
                drawTokens = false;
        }

        DrawDebugRect(ContentTypeSegment, drawList, segment.Bounds.start(initpos), segment.Bounds.end(initpos), config);
        if (popFont) ImGui::PopFont();
        return drawTokens;
    }

    void DrawForegroundLayer(ImDrawList* drawList, ImVec2 initpos, ImVec2 bounds, 
        const std::deque<DrawableLine>& lines, const RenderConfig& config, 
        TooltipData& tooltip, AnimationData& animation)
    {
        int listCountByDepth[IM_RICHTEXT_MAX_LISTDEPTH];
        int listDepth = -1;

        for (auto lineidx = 0; lineidx < (int)lines.size(); ++lineidx)
        {
            if (lines[lineidx].Segments.empty()) continue;

            for (const auto& segment : lines[lineidx].Segments)
            {
                if (!DrawSegment(drawList, lineidx, segment, initpos, bounds, config, tooltip, animation, lines[lineidx].Marquee))
                    break;
            }

            DrawDebugRect(ContentTypeLine, drawList, lines[lineidx].Content.start(initpos), lines[lineidx].Content.end(initpos), config);
            if ((lines[lineidx].Content.top + lines[lineidx].height()) > (bounds.y + initpos.y)) break;
        }
    }

    void DrawBackgroundLayer(ImDrawList* drawList, ImVec2 initpos, ImVec2 bounds, 
        const std::deque<BackgroundShape>& shapes)
    {
        for (const auto& shape : shapes)
        {
            drawList->AddRectFilled(shape.Start + initpos, shape.End + initpos, shape.Color);
            if (shape.End.y > (bounds.y + initpos.y)) break;
        }
    }

    void DrawTooltip(ImDrawList* drawList, const TooltipData& tooltip, const RenderConfig& config)
    {
        if (!tooltip.content.empty())
        {
            auto font = config.GetFont(config.DefaultFontFamily, config.DefaultFontSize, false, false, false, config.UserData);
            ImGui::PushFont(font);
            ImGui::SetTooltip("%.*s", (int)tooltip.content.size(), tooltip.content.data());
            ImGui::PopFont();
        }
    }

    void Draw(std::string_view richText, const Drawables& drawables, ImVec2 pos, ImVec2 bounds, RenderConfig* config)
    {
        using namespace std::chrono;

        config = GetRenderConfig(config);
        auto endpos = pos + bounds;
        auto drawList = ImGui::GetCurrentWindow()->DrawList;
        TooltipData tooltip;
        auto& animation = AnimationMap[richText];
        
        if (animation.xoffsets.empty())
        {
            animation.xoffsets.resize(drawables.Foreground.size());
            std::fill(animation.xoffsets.begin(), animation.xoffsets.end(), 0.f);
        }

        auto currFrameTime = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();

        ImGui::PushClipRect(pos, endpos, true);
        drawList->AddRectFilled(pos, endpos, config->DefaultBgColor);

        DrawBackgroundLayer(drawList, pos, bounds, drawables.Background);
        DrawForegroundLayer(drawList, pos, bounds, drawables.Foreground, *config, tooltip, animation);
        DrawTooltip(drawList, tooltip, *config);

        if (!config->IsStrictHTML5 && (currFrameTime - animation.lastBlinkTime > IM_RICHTEXT_BLINK_ANIMATION_INTERVAL))
        {
            animation.isVisible = !animation.isVisible;
            animation.lastBlinkTime = currFrameTime;
            if (config->NewFrameGenerated) config->NewFrameGenerated(config->UserData);
        }

        if (currFrameTime - animation.lastMarqueeTime > IM_RICHTEXT_MARQUEE_ANIMATION_INTERVAL)
        {
            for (auto lineidx = 0; lineidx < (int)animation.xoffsets.size(); ++lineidx)
            {
                animation.xoffsets[lineidx] += 1.f;
                auto linewidth = drawables.Foreground[lineidx].width();

                if (animation.xoffsets[lineidx] >= linewidth)
                    animation.xoffsets[lineidx] = -linewidth;

                if (config->NewFrameGenerated) config->NewFrameGenerated(config->UserData);
            }

            animation.lastMarqueeTime = currFrameTime;
        }

        ImGui::PopClipRect();
    }

    bool ShowDrawables(std::string_view content, const Drawables& drawables, RenderConfig* config)
    {
        ImGuiWindow* window = ImGui::GetCurrentWindow();
        if (window->SkipItems)
            return false;

        const auto& style = ImGui::GetCurrentContext()->Style;
        auto bounds = GetBounds(drawables, config->Bounds);
        auto id = window->GetID(content.data(), content.data() + content.size());
        auto pos = window->DC.CursorPos;
        ImGui::ItemSize(bounds);
        ImGui::ItemAdd(ImRect{ pos, pos + bounds }, id);
        Draw(content, drawables, pos + style.FramePadding, bounds, config);
        return true;
    }

    RenderConfig* GetDefaultConfig(ImVec2 Bounds, float defaultFontSize, float fontScale, bool skipDefaultFontLoading)
    {
        auto config = &DefaultRenderConfig;
        config->Bounds = Bounds;
        config->GetFont = &GetFont;
        config->NamedColor = &GetColor;
        config->GetTextSize = &GetTextSize;
        config->EscapeCodes.insert(config->EscapeCodes.end(), std::begin(EscapeCodes),
            std::end(EscapeCodes));
        config->FontScale = fontScale;
        config->DefaultFontSize = defaultFontSize;
        config->MeterDefaultSize = { defaultFontSize * 5.0f, defaultFontSize };

        DefaultRenderConfigInit = true;
        if (!skipDefaultFontLoading) LoadDefaultFonts(*config);
        return config;
    }

    RenderConfig* GetCurrentConfig()
    {
        auto ctx = ImGui::GetCurrentContext();
        auto it = RenderConfigs.find(ctx);
        return it != RenderConfigs.end() ? &(it->second.back()) : &DefaultRenderConfig;
    }

    void PushConfig(const RenderConfig& config)
    {
        auto ctx = ImGui::GetCurrentContext();
        RenderConfigs[ctx].push_back(config);
    }

    void PopConfig()
    {
        auto ctx = ImGui::GetCurrentContext();
        auto it = RenderConfigs.find(ctx);
        if (it != RenderConfigs.end()) it->second.pop_back();
    }

    Drawables GetDrawables(const char* text, const char* textend, const RenderConfig& config)
    {
        Drawables result;
        int end = (int)(textend - text), start = 0;
        start = SkipSpace(text, start, end);

        auto initialStyle = CreateDefaultStyle(config);
        CurrentStackPos = -1;
        std::string_view currTag;

        char TabSpaces[IM_RICHTEXT_MAXTABSTOP] = { 0 };
        std::memset(TabSpaces, ' ', Clamp(config.TabStop, 0, IM_RICHTEXT_MAXTABSTOP - 1));
        TabSpaces[config.TabStop] = 0;

        DrawableLine line = CreateNewLine(initialStyle, config);
        int listDepth = -1, blockquoteDepth = -1;
        int subscriptLevel = 0, superscriptLevel = 0;
        bool currentListIsNumbered = false;
        float maxWidth = config.Bounds.x;
        TagType tagType = TagType::Unknown;
        std::memset(ListItemCountByDepths, 0, IM_RICHTEXT_MAX_LISTDEPTH);

        for (auto idx = start; idx < end;)
        {
            if (text[idx] == config.TagStart)
            {
                idx++;
                auto tagStart = true, selfTerminatingTag = false;
                auto [currTag, status] = ExtractTag(text, end, config, idx, tagStart);
                if (!status) return result;

                tagType = GetTagType(currTag);
                if (tagType == TagType::Unknown)
                {
                    ERROR("Unknown Tag encountered: <%.*s>\n", (int)currTag.size(), currTag.data());
                    continue;
                }

                if (tagStart)
                {
                    LOG("Entering Tag: <%.*s>\n", (int)currTag.size(), currTag.data());
                    auto& styleprops = PushTag(currTag, initialStyle);
                    if (tagType == TagType::Superscript) superscriptLevel++;
                    else if (tagType == TagType::Subscript) subscriptLevel++;

                    auto propsChanged = ExtractAttributes(text, end, currTag, tagType, config, styleprops, initialStyle, idx);
                    const auto& parentStyle = CurrentStackPos >= 0 ? TagStack[CurrentStackPos - 1].second : initialStyle;
                    auto& segment = UpdateSegmentStyle(tagType, currTag, styleprops, parentStyle,
                        line, propsChanged, config);
                    segment.SubscriptDepth = subscriptLevel;
                    segment.SuperscriptDepth = superscriptLevel;

                    if (tagType == TagType::Marquee || tagType == TagType::Blink)
                        AnimationMap[std::string_view{ text, (size_t)(textend - text) }];

                    if (tagType == TagType::List)
                    {
                        listDepth++;
                        currentListIsNumbered = AreSame(currTag, "ol");
                    }
                    else if (tagType == TagType::Paragraph || tagType == TagType::Header ||
                        tagType == TagType::RawText || tagType == TagType::ListItem ||
                        tagType == TagType::CodeBlock || tagType == TagType::Marquee)
                    {
                        line = MoveToNextLine(styleprops, tagType, listDepth, blockquoteDepth, line, result.Foreground, config, tagStart);
                        maxWidth = std::max(maxWidth, result.Foreground.back().Content.width);

                        if (tagType == TagType::Paragraph && config.ParagraphStop > 0 && config.GetTextSize)
                            line.Offset.left += config.GetTextSize(std::string_view{ LineSpaces,
                                (std::size_t)std::min(config.ParagraphStop, IM_RICHTEXT_MAXTABSTOP) }, styleprops.font.font).x;
                        else if (tagType == TagType::ListItem)
                        {
                            ListItemCountByDepths[listDepth]++;

                            Token token;
                            token.Type = !currentListIsNumbered ? TokenType::ListItemBullet :
                                TokenType::ListItemNumbered;
                            token.ListDepth = listDepth;
                            token.ListItemIndex = ListItemCountByDepths[listDepth];
                            AddToken(line, token, propsChanged, config);
                            AddSegment(line, styleprops, config);
                        }
                    }
                    else if (tagType == TagType::Blockquote)
                    {
                        blockquoteDepth++;
                        line = MoveToNextLine(styleprops, tagType, listDepth, blockquoteDepth, line, result.Foreground, config, tagStart);
                        maxWidth = std::max(maxWidth, result.Foreground.back().Content.width);
                        auto& start = BlockquoteStack[blockquoteDepth].bounds.emplace_back();
                        start.first = ImVec2{ line.Content.left, line.Content.top };
                    }
                    else if (tagType == TagType::Quotation)
                    {
                        Token token;
                        token.Type = TokenType::Text;
                        token.Content = "\"";
                        AddToken(line, token, propsChanged, config);
                    }
                    else if (tagType == TagType::Meter)
                    {
                        Token token;
                        token.Type = TokenType::Meter;
                        AddToken(line, token, propsChanged, config);
                    }
                }

                selfTerminatingTag = (text[idx - 2] == '/' && text[idx - 1] == config.TagEnd) || (tagType == TagType::Hr) ||
                    (tagType == TagType::LineBreak);

                if (selfTerminatingTag || !tagStart)
                {
                    // pop stye properties and reset
                    PopTag(!selfTerminatingTag);
                    const auto& styleprops = CurrentStackPos >= 0 ?
                        TagStack[CurrentStackPos].second : initialStyle;
                    LOG("Exiting Tag: <%.*s>\n", (int)currTag.size(), currTag.data());

                    if (tagType == TagType::List || tagType == TagType::Paragraph || tagType == TagType::Header ||
                        tagType == TagType::RawText || tagType == TagType::Blockquote || tagType == TagType::LineBreak ||
                        tagType == TagType::CodeBlock || tagType == TagType::Marquee)
                    {
                        if (tagType == TagType::List)
                        {
                            ListItemCountByDepths[listDepth] = 0;
                            listDepth--;
                        }

                        line = MoveToNextLine(styleprops, tagType, listDepth, blockquoteDepth, line, result.Foreground, config, tagStart);
                        maxWidth = std::max(maxWidth, result.Foreground.back().Content.width);

                        if (tagType == TagType::Blockquote)
                        {
                            assert(!BlockquoteStack[blockquoteDepth].bounds.empty());
                            auto& bounds = BlockquoteStack[blockquoteDepth].bounds.back();
                            const auto& lastLine = result.Foreground[result.Foreground.size() - 2u];
                            bounds.second = ImVec2{ lastLine.width() + bounds.first.x, lastLine.Content.top + lastLine.height() };
                            blockquoteDepth--;
                        }
                        else if (tagType == TagType::Header)
                        {
                            // Add properties for horizontal line below header
                            auto& style = line.Segments.back().Style;
                            style.height = 1.f;
                            style.fgcolor = config.HeaderLineColor;
                            style.padding.top = style.padding.bottom = config.HrVerticalMargins;

                            Token token;
                            token.Type = TokenType::HorizontalRule;
                            AddToken(line, token, NoStyleChange, config);

                            // Move to next line for other content
                            line = MoveToNextLine(styleprops, tagType, listDepth, blockquoteDepth, line, result.Foreground, config, tagStart);
                            maxWidth = std::max(maxWidth, result.Foreground.back().Content.width);
                        }
                    }
                    else if (tagType == TagType::Hr)
                    {
                        // Since hr style is popped, refer to next item in stack
                        auto& prevstyle = TagStack[CurrentStackPos + 1].second;
                        prevstyle.padding.top = prevstyle.padding.bottom = config.HrVerticalMargins;
                        line = MoveToNextLine(prevstyle, tagType, listDepth, blockquoteDepth, line, result.Foreground, config, tagStart);
                        maxWidth = std::max(maxWidth, result.Foreground.back().Content.width);

                        Token token;
                        token.Type = TokenType::HorizontalRule;
                        AddToken(line, token, NoStyleChange, config);

                        line = MoveToNextLine(styleprops, tagType, listDepth, blockquoteDepth, line, result.Foreground, config, true);
                        maxWidth = std::max(maxWidth, result.Foreground.back().Content.width);
                    }
                    else if (tagType == TagType::Quotation)
                    {
                        Token token;
                        token.Type = TokenType::Text;
                        token.Content = "\"";
                        AddToken(line, token, NoStyleChange, config);
                    }
                    else
                    {
                        AddSegment(line, styleprops, config);

                        if (tagType == TagType::Superscript) superscriptLevel--;
                        else if (tagType == TagType::Subscript) subscriptLevel--;
                    }

                    if (selfTerminatingTag) TagStack[CurrentStackPos + 1] = std::make_pair(std::string_view{}, SegmentStyle{});
                    currTag = CurrentStackPos == -1 ? "" : TagStack[CurrentStackPos].first;
                    tagType = TagType::Unknown;
                }
            }
            else
            {
                Token token;
                auto& styleprops = CurrentStackPos == -1 ? initialStyle : TagStack[CurrentStackPos].second;
                auto isPreTag = AreSame(currTag, "pre");
                auto begin = idx;

                if (isPreTag)
                {
                    while (((idx + 6) < end) && AreSame(std::string_view{ text + idx, 6u }, "</pre>")) idx++;
                    std::string_view content{ text + begin, (std::size_t)(idx - begin) };
                    auto curridx = 0, start = 0;

                    while (curridx < (int)content.size())
                    {
                        if (content[curridx] == '\n')
                        {
                            if (!subscriptLevel && !superscriptLevel)
                            {
                                GenerateTextToken(line, content, curridx, start, config);
                                line = MoveToNextLine(styleprops, tagType, listDepth, blockquoteDepth, line, result.Foreground, config, true);
                                maxWidth = std::max(maxWidth, result.Foreground.back().Content.width);
                                start = curridx;
                            }
                            else
                            {
                                GenerateTextToken(line, content, curridx, start, config);
                                start = curridx;
                            }
                        }

                        ++curridx;
                    }

                    if (curridx > start)
                    {
                        GenerateTextToken(line, content, curridx, start, config);
                    }
                }
                else
                {
                    while ((idx < end) && (text[idx] != config.TagStart)) idx++;
                    std::string_view content{ text + begin, (std::size_t)(idx - begin) };
                    LOG("Processing content [%.*s]\n", (int)content.size(), content.data());

                    // Ignore newlines, tabs & consecutive spaces
                    auto curridx = 0, start = 0;

                    while (curridx < (int)content.size())
                    {
                        if (content[curridx] == '\n')
                        {
                            GenerateTextToken(line, content, curridx, start, config);
                            while (curridx < (int)content.size() && content[curridx] == '\n') curridx++;
                            start = curridx;
                        }
                        else if (content[curridx] == '\t')
                        {
                            GenerateTextToken(line, content, curridx, start, config);
                            while (curridx < (int)content.size() && content[curridx] == '\t') curridx++;
                            start = curridx;
                        }
                        else if (content[curridx] == config.EscapeSeqStart)
                        {
                            GenerateTextToken(line, content, curridx, start, config);

                            curridx++;
                            auto [hasEscape, isNewLine] = AddEscapeSequences(content, curridx, config.EscapeCodes,
                                config.EscapeSeqStart, config.EscapeSeqEnd, line, start);
                            curridx = start;

                            if (isNewLine && !subscriptLevel && !superscriptLevel)
                            {
                                line = MoveToNextLine(styleprops, tagType, listDepth, blockquoteDepth, line, result.Foreground, config, true);
                                maxWidth = std::max(maxWidth, result.Foreground.back().Content.width);
                            }

                            if (hasEscape) continue;
                        }
                        else if (content[curridx] == ' ')
                        {
                            curridx++;

                            if (curridx - start > 0) GenerateTextToken(line, content, curridx, start, config);
                            curridx = SkipSpace(content, curridx);
                            start = curridx;
                            continue;
                        }

                        curridx++;
                    }

                    if (curridx > start) GenerateTextToken(line, content, curridx, start, config);
                }
            }
        }

        MoveToNextLine(SegmentStyle{}, tagType, listDepth, blockquoteDepth, line, result.Foreground, config, true);
        maxWidth = std::max(maxWidth, result.Foreground.back().Content.width);

        for (auto& line : result.Foreground)
            if (line.Marquee) line.Content.width = maxWidth;

        for (auto depth = 0; depth < IM_RICHTEXT_MAXDEPTH; ++depth)
        {
            for (const auto& bound : BlockquoteStack[depth].bounds)
            {
                if (config.BlockquoteBarWidth > 1.f && config.DefaultBgColor.Value != config.BlockquoteBar.Value)
                    result.Background.emplace_back(BackgroundShape{ bound.first, ImVec2{ config.BlockquoteBarWidth, bound.second.y },
                        config.BlockquoteBar });

                if (config.DefaultBgColor.Value != config.BlockquoteBg.Value)
                    result.Background.emplace_back(BackgroundShape{ ImVec2{ bound.first.x + config.BlockquoteBarWidth, bound.first.y },
                        bound.second, config.BlockquoteBg });
            }
        }

        return result;
    }

    ImVec2 GetBounds(const Drawables& drawables, ImVec2 bounds)
    {
        ImVec2 result = bounds;
        const auto& style = ImGui::GetCurrentContext()->Style;

        if (bounds.x < 0.f)
        {
            float width = 0.f;
            for (const auto& line : drawables.Foreground)
                width = std::max(width, line.width() + line.Content.left);
            for (const auto& bg : drawables.Background)
                width = std::max(width, bg.End.x);
            result.x = width + (2.f * style.FramePadding.x);
        }

        if (bounds.y < 0.f)
        {
            auto fgheight = 0.f, bgheight = 0.f;

            if (!drawables.Foreground.empty())
            {
                const auto& lastFg = drawables.Foreground.back();
                fgheight = lastFg.height() + lastFg.Content.top;
            }
            
            if (!drawables.Background.empty())
                bgheight = drawables.Background.back().End.y;
           
            result.y = std::max(fgheight, bgheight) + (2.f * style.FramePadding.y);
        }

        return result;
    }

    bool Show(const char* text, const char* textend)
    {
        if (textend == nullptr) textend = text + std::strlen(text);
        if (textend - text == 0) return false;

        auto config = GetRenderConfig();
        if (config->Bounds.x == 0 || config->Bounds.y == 0) return false;

        auto drawables = GetDrawables(text, textend, *config);
        return ShowDrawables(std::string_view{ text, (size_t)(textend - text) }, drawables, config);
    }

    std::size_t CreateRichText(const char* text, const char* end)
    {
        if (end == nullptr) end = text + std::strlen(text);
        std::string_view key{ text, (size_t)(end - text) };
        auto it = RichTextStrings.find(key);

        if (it == RichTextStrings.end())
        {
            auto hash = std::hash<std::string_view>()(key);
            it = RichTextStrings.emplace(key, hash).first;
            RichTextMap[it->second].richText.assign(key.data(), key.size());
            RichTextMap[it->second].contentChanged = true;
        }

        return it->second;
    }

    bool UpdateRichText(std::size_t id, const char* text, const char* end)
    {
        auto rit = RichTextMap.find(id);

        if (rit != RichTextMap.end())
        {
            auto it = RichTextStrings.find(rit->second.richText);

            if (it != RichTextStrings.end())
            {
                if (end == nullptr) end = text + std::strlen(text);
                std::string_view key{ text, (size_t)(end - text) };
                it = RichTextStrings.emplace(key, id).first;
                RichTextMap[id].richText.assign(key.data(), key.size());
                RichTextMap[id].contentChanged = true;
                return true;
            }
        }

        return false;
    }

    bool RemoveRichText(std::size_t id)
    {
        auto it = RichTextMap.find(id);

        if (it != RichTextMap.end())
        {
            RichTextStrings.erase(it->second.richText);
            AnimationMap.erase(it->second.richText);
            RichTextMap.erase(it);
            return true;
        }

        return false;
    }

    void ClearAllRichTexts()
    {
        RichTextMap.clear();
        RichTextStrings.clear();
    }

    bool Show(std::size_t richTextId)
    {
        auto it = RichTextMap.find(richTextId);

        if (it != RichTextMap.end())
        {
            auto& drawdata = RichTextMap[richTextId];
            auto config = GetRenderConfig();

            if (config != drawdata.config || drawdata.contentChanged)
            {
                drawdata.contentChanged = false;
                drawdata.config = config;
                drawdata.drawables = GetDrawables(drawdata.richText.data(),
                    drawdata.richText.data() + drawdata.richText.size(), *config);
            }

            ShowDrawables(drawdata.richText, drawdata.drawables, config);
            return true;
        }

        return false;
    }
}