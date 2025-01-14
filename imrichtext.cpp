#include "ImRichText.h"
#if __has_include("imrichtextfont.h")
#define IM_RICHTEXT_DEFAULT_FONTS_AVAILABLE
#include "imrichtextfont.h"
#endif
#include "imgui_internal.h"
#include "imrichtextutils.h"
#include <unordered_map>
#include <cstring>
#include <cctype>
#include <optional>
#include <cstdlib>
#include <string>
#include <chrono>
#include <deque>

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
    };

    struct RichTextData
    {
        Drawables drawables;
        RenderConfig* config = nullptr;
        std::string richText;
        float scale = 1.f;
        float fontScale = 1.f;
        ImColor bgcolor;
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

    struct BackgroundSpanData
    {
        std::pair<int, int> start{ -1, -1 };
        std::pair<int, int> end{ -1, -1 };
    };

    struct StackData
    {
        std::string_view tag;
        int styleIdx = -1;
    };

    struct BackgroundData
    {
        BackgroundSpanData span;
        int styleIdx = -1;
    };

    static std::unordered_map<std::string_view, std::size_t> RichTextStrings;
    static std::unordered_map<std::size_t, RichTextData> RichTextMap;
    static std::unordered_map<std::string_view, AnimationData> AnimationMap;

    static StackData TagStack[IM_RICHTEXT_MAXDEPTH];
    static std::vector<BackgroundData> BackgroundSpans[IM_RICHTEXT_MAXDEPTH];
    static int CurrentStackPos = -1;

    static int ListItemCountByDepths[IM_RICHTEXT_MAX_LISTDEPTH];
    static BlockquoteDrawData BlockquoteStack[IM_RICHTEXT_MAXDEPTH];

    static std::unordered_map<ImGuiContext*, std::deque<RenderConfig>> RenderConfigs;
    static RenderConfig DefaultRenderConfig;
    static bool DefaultRenderConfigInit = false;
    static ListItemTokenDescriptor InvalidListItemToken;
    static TagPropertyDescriptor InvalidTagPropDesc;

    static std::vector<std::string> NumbersAsStr;
#ifdef _DEBUG
    static bool ShowOverlay = true;
#else
    static bool ShowOverlay = false;
#endif

    static const std::pair<std::string_view, std::string_view> EscapeCodes[10] = {
        { "Tab", "\t" }, { "NewLine", "\n" }, { "nbsp", " " },
        { "gt", ">" }, { "lt", "<" },
        { "amp", "&" }, { "copy", "©" }, { "reg", "®" },
        { "deg", "°" }, { "micro", "μ" }
    };

    static const char* LineSpaces = "                                ";

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

    template <typename T>
    [[nodiscard]] T Clamp(T val, T min, T max)
    {
        return val < min ? min : val > max ? max : val;
    }

    int PopulateSegmentStyle(StyleDescriptor& style,
        const StyleDescriptor& parentStyle,
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
                if (AreSame(stylePropVal, "bold")) style.font.flags |= FontStyleBold;
                else if (AreSame(stylePropVal, "light")) style.font.flags |= FontStyleLight;
                else ERROR("Invalid font-weight property value... [%.*s]\n",
                    (int)stylePropVal.size(), stylePropVal.data());
            }
            else
            {
                int weight = ExtractInt(stylePropVal.substr(0u, idx), 400);
                if (weight >= 600) style.font.flags |= FontStyleBold;
                if (weight < 400) style.font.flags |= FontStyleLight;
            }

            prop = StyleFontWeight;
        }
        else if (AreSame(stylePropName, "background-color") || AreSame(stylePropName, "background"))
        {
            if (StartsWith(stylePropVal, "linear-gradient")) 
               style.gradient = ExtractLinearGradient(stylePropVal, config.NamedColor, config.UserData);
            else style.bgcolor = ExtractColor(stylePropVal, config.NamedColor, config.UserData);
            prop = StyleBackground;
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
            style.alignment |= AreSame(stylePropVal, "justify") ? TextAlignJustify :
                AreSame(stylePropVal, "right") ? TextAlignRight :
                AreSame(stylePropVal, "center") ? TextAlignHCenter :
                TextAlignLeft;
            prop = StyleHAlignment;
        }
        else if (AreSame(stylePropName, "vertical-align"))
        {
            style.alignment |= AreSame(stylePropVal, "top") ? TextAlignTop :
                AreSame(stylePropVal, "bottom") ? TextAlignBottom :
                TextAlignVCenter;
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
        else if (AreSame(stylePropName, "white-space"))
        {
            if (AreSame(stylePropVal, "nowrap")) 
            {
                style.font.flags |= FontStyleNoWrap;
                prop = StyleNoWrap;
            }
        }
        else if (AreSame(stylePropName, "text-overflow"))
        {
            if (AreSame(stylePropVal, "ellipsis"))
            {
                style.font.flags |= FontStyleNoWrap | FontStyleOverflowEllipsis;
                prop = StyleNoWrap;
            }
        }
        else if (AreSame(stylePropName, "border"))
        {
            style.border.top = style.border.bottom = style.border.left = style.border.right = ExtractBorder(stylePropVal,
                config.DefaultFontSize * config.FontScale, parentStyle.height, config.NamedColor, config.UserData);
            prop = StyleBorder | StyleBorderUniform;
        }
        else if (AreSame(stylePropName, "border-top"))
        {
            style.border.top = ExtractBorder(stylePropVal, config.DefaultFontSize * config.FontScale, 
                parentStyle.height, config.NamedColor, config.UserData);
            prop = StyleBorder;
        }
        else if (AreSame(stylePropName, "border-left"))
        {
            style.border.left = ExtractBorder(stylePropVal, config.DefaultFontSize * config.FontScale,
                parentStyle.height, config.NamedColor, config.UserData);
            prop = StyleBorder;
        }
        else if (AreSame(stylePropName, "border-right"))
        {
            style.border.right = ExtractBorder(stylePropVal, config.DefaultFontSize * config.FontScale,
                parentStyle.height, config.NamedColor, config.UserData);
            prop = StyleBorder;
        }
        else if (AreSame(stylePropName, "border-bottom"))
        {
            style.border.bottom = ExtractBorder(stylePropVal, config.DefaultFontSize * config.FontScale,
                parentStyle.height, config.NamedColor, config.UserData);
            prop = StyleBorder;
        }
        else if (AreSame(stylePropName, "border-radius"))
        {
            style.border.radius = ExtractFloatWithUnit(stylePropVal, 0.f, config.DefaultFontSize * config.FontScale,
                parentStyle.height, 1.f);
            prop = StyleBorder;
        }
        else if (AreSame(stylePropName, "font-style"))
        {
            if (AreSame(stylePropVal, "normal")) style.font.flags |= FontStyleNormal;
            else if (AreSame(stylePropVal, "italic") || AreSame(stylePropVal, "oblique"))
                style.font.flags |= FontStyleItalics;
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

    [[nodiscard]] StyleDescriptor CreateDefaultStyle(const RenderConfig& config)
    {
        assert(config.GetFont != nullptr);
        assert(config.GetTextSize != nullptr);

        StyleDescriptor result;
        result.font.family = config.DefaultFontFamily;
        result.font.size = config.DefaultFontSize * config.FontScale;
        result.font.font = config.GetFont(result.font.family, result.font.size, false, false, false, config.UserData);
        result.fgcolor = config.DefaultFgColor;
        result.bgcolor = config.DefaultBgColor;
        result.list.itemStyle = config.ListItemBullet;
        return result;
    }

    [[nodiscard]] ImVec2 GetSegmentSize(const SegmentData& segment, const std::vector<StyleDescriptor>& styles, const RenderConfig& config)
    {
        auto height = 0.f, width = 0.f;
        const auto& style = styles[segment.StyleIdx + 1];

        for (const auto& token : segment.Tokens)
        {
            height = std::max(height, token.Bounds.height + token.Offset.v());
            width += token.Bounds.width + token.Offset.h();
        }

        return { width + style.padding.h() + style.border.h(), height + style.padding.v() + style.border.v() };
    }

    [[nodiscard]] ImVec2 GetLineSize(const DrawableLine& line, const std::vector<StyleDescriptor>& styles, const RenderConfig& config)
    {
        auto height = 0.f, width = 0.f;

        for (const auto& segment : line.Segments)
        {
            auto sz = GetSegmentSize(segment, styles, config);
            height = std::max(height, sz.y);
            width += sz.x;
        }

        return { width, height };
    }

    void AddToken(DrawableLine& line, Token token, int propsChanged, Drawables& result, const RenderConfig& config)
    {
        auto& segment = line.Segments.back();
        const auto& style = result.StyleDescriptors[segment.StyleIdx + 1];

        if (token.Type == TokenType::Text)
        {
            auto sz = config.GetTextSize(token.Content, style.font.font);
            token.VisibleTextSize = (int)token.Content.size();
            token.Bounds.width = sz.x;
            token.Bounds.height = sz.y;
        }
        else if (token.Type == TokenType::HorizontalRule)
        {
            token.Bounds.width = config.Bounds.x - line.Content.left - line.Offset.h() -
                style.padding.h();
            token.Bounds.height = style.height;
        }
        else if (token.Type == TokenType::ListItemBullet)
        {
            auto bulletscale = Clamp(config.BulletSizeScale, 1.f, 4.f);
            auto bulletsz = (style.font.size) / bulletscale;
            token.Bounds.width = token.Bounds.height = bulletsz;
            token.Offset.right = config.ListItemOffset;
        }
        else if (token.Type == TokenType::ListItemNumbered)
        {
            if (NumbersAsStr.empty())
            {
                NumbersAsStr.reserve(IM_RICHTEXT_MAX_LISTITEM);

                for (auto num = 1; num <= IM_RICHTEXT_MAX_LISTITEM; ++num)
                    NumbersAsStr.emplace_back(std::to_string(num));
            }

            auto& listItem = result.ListItemTokens[token.ListPropsIdx];
            std::memset(listItem.NestedListItemIndex, 0, IM_RICHTEXT_NESTED_ITEMCOUNT_STRSZ);
            auto currbuf = 0;

            for (auto depth = 0; depth <= listItem.ListDepth && currbuf < IM_RICHTEXT_NESTED_ITEMCOUNT_STRSZ; ++depth)
            {
                auto itemcount = ListItemCountByDepths[depth] - 1;
                auto itemlen = itemcount > 99 ? 3 : itemcount > 9 ? 2 : 1;
                std::memcpy(listItem.NestedListItemIndex + currbuf, NumbersAsStr[itemcount].data(), itemlen);
                currbuf += itemlen;

                listItem.NestedListItemIndex[currbuf] = '.';
                currbuf += 1;
            }

            std::string_view input{ listItem.NestedListItemIndex, (size_t)currbuf };
            auto sz = config.GetTextSize(input, style.font.font);
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
        segment.Bounds.width += token.Bounds.width;
        segment.Bounds.height = std::max(token.Bounds.height, segment.Bounds.height);
        line.HasText = line.HasText || segment.HasText;
        line.HasSubscript = line.HasSubscript || segment.SubscriptDepth > 0;
        line.HasSuperscript = line.HasSuperscript || segment.SuperscriptDepth > 0;

        LOG("Added token: %.*s [itemtype: %s][font-size: %f][size: (%f, %f)]\n",
            (int)token.Content.size(), token.Content.data(),
            GetTokenTypeString(token), style.font.size,
            token.Bounds.width, token.Bounds.height);
    }

    SegmentData& AddSegment(DrawableLine& line, int styleIdx, const std::vector<StyleDescriptor>& styles, const RenderConfig& config)
    {
        if (!line.Segments.empty())
        {
            auto& lastSegment = line.Segments.back();
            auto sz = GetSegmentSize(lastSegment, styles, config);
            lastSegment.Bounds.width = sz.x;
            lastSegment.Bounds.height = sz.y;
        }

        auto& segment = line.Segments.emplace_back();
        segment.StyleIdx = styleIdx;
        return segment;
    }

    [[nodiscard]] DrawableLine CreateNewLine(int styleIdx)
    {
        DrawableLine line;
        line.Segments.emplace_back();
        line.Segments.back().StyleIdx = styleIdx;
        line.BlockquoteDepth = -1;
        return line;
    }

    enum class TagType
    {
        Unknown, 
        Bold, Italics, Underline, Strikethrough, Mark, Small,
        Span, List, ListItem, Paragraph, Header, RawText, Blockquote, Quotation, Abbr, CodeBlock, Hyperlink,
        Subscript, Superscript,
        Hr, LineBreak,
        Blink, Marquee,
        Meter
    };

    std::vector<int> PerformWordWrap(std::vector<DrawableLine>& lines, TagType tagType, int index, int listDepth, int blockquoteDepth,
        const std::vector<StyleDescriptor>& styles, const RenderConfig& config)
    {
        std::vector<int> result;
        if (!lines[index].HasText || !config.WordWrap || !config.GetTextSize)
        {
            result.push_back((int)lines.size() - 1);
            return result;
        }

        std::vector<DrawableLine> newlines;
        auto currline = CreateNewLine(-1);
        auto currentx = 0.f;
        auto availwidth = lines[index].Content.width;

        for (const auto& segment : lines[index].Segments)
        {
            const auto& style = styles[segment.StyleIdx + 1];

            for (const auto& token : segment.Tokens)
            {
                if (token.Type == TokenType::Text)
                {
                    auto sz = config.GetTextSize(token.Content, style.font.font);

                    if (currentx + sz.x > availwidth)
                    {
                        newlines.push_back(currline);
                        currline.Segments.clear();
                        currline.Segments.emplace_back().StyleIdx = segment.StyleIdx;
                        currentx = 0.f;
                    }

                    currline.Segments.back().StyleIdx = segment.StyleIdx;
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

    float GetMaxSuperscriptOffset(const DrawableLine& line, const std::vector<StyleDescriptor>& styles, float scale)
    {
        auto topOffset = 0.f;
        auto baseFontSz = 0.f;

        for (auto idx = 0; idx < (int)line.Segments.size();)
        {
            const auto& segment = line.Segments[idx];
            baseFontSz = styles[segment.StyleIdx + 1].font.size;
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

    float GetMaxSubscriptOffset(const DrawableLine& line, const std::vector<StyleDescriptor>& styles, float scale)
    {
        auto topOffset = 0.f;
        auto baseFontSz = 0.f;

        for (auto idx = 0; idx < (int)line.Segments.size();)
        {
            const auto& segment = line.Segments[idx];
            baseFontSz = styles[segment.StyleIdx + 1].font.size;
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

    void AdjustForSuperSubscripts(const std::vector<int>& indexes, std::vector<DrawableLine>& lines,
        std::vector<StyleDescriptor>& styles, const RenderConfig& config)
    {
        for (auto idx : indexes)
        {
            auto& line = lines[idx];
            if (!line.HasSubscript && !line.HasSuperscript) continue;

            auto maxTopOffset = GetMaxSuperscriptOffset(line, styles, config.ScaleSuperscript);
            auto maxBottomOffset = GetMaxSubscriptOffset(line, styles, config.ScaleSubscript);
            auto lastFontSz = config.DefaultFontSize * config.FontScale;
            auto lastSuperscriptDepth = 0, lastSubscriptDepth = 0;

            for (auto& segment : line.Segments)
            {
                auto& style = styles[segment.StyleIdx + 1];

                if (segment.SuperscriptDepth > lastSuperscriptDepth)
                {
                    style.font.size = lastFontSz * config.ScaleSuperscript;
                    maxTopOffset -= style.font.size * 0.5f;
                }
                else if (segment.SuperscriptDepth < lastSuperscriptDepth)
                {
                    maxTopOffset += lastFontSz * 0.5f;
                    style.font.size = lastFontSz / config.ScaleSuperscript;
                }

                if (segment.SubscriptDepth > lastSubscriptDepth)
                {
                    style.font.size = lastFontSz * config.ScaleSubscript;
                    maxBottomOffset += (lastFontSz - style.font.size * 0.5f);
                }
                else if (segment.SubscriptDepth < lastSubscriptDepth)
                {
                    style.font.size = lastFontSz / config.ScaleSubscript;
                    maxBottomOffset -= style.font.size * 0.5f;
                }

                style.superscriptOffset = maxTopOffset;
                style.subscriptOffset = maxBottomOffset;
                segment.Bounds.height += maxTopOffset + maxBottomOffset;

                lastSuperscriptDepth = segment.SuperscriptDepth;
                lastSubscriptDepth = segment.SubscriptDepth;
                lastFontSz = style.font.size;
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

    void ComputeLineBounds(std::vector<DrawableLine>& result, const std::vector<int>& linesModified,
        const std::vector<StyleDescriptor>& styles, const RenderConfig& config)
    {
        for (auto index : linesModified)
        {
            auto& line = result[index];
            auto currx = line.Content.left + line.Offset.left;
            auto sz = GetLineSize(line, styles, config);
            line.Content.width = sz.x;
            line.Content.height = sz.y;

            if (index > 0) line.Content.top = result[index - 1].Content.top + result[index - 1].height() + config.LineGap;

            for (auto& segment : line.Segments)
            {
                if (segment.Tokens.empty()) continue;

                // This will align the segment as vertical centre, TODO: Handle other alignments
                segment.Bounds.top = line.Content.top + line.Offset.top + ((line.Content.height - segment.height()) * 0.5f);
                segment.Bounds.left = currx; // This will align left, TODO: Handle other alignments
                const auto& style = styles[segment.StyleIdx + 1];

                currx += style.padding.left + style.border.left.thickness;

                for (auto tokidx = 0; tokidx < (int)segment.Tokens.size(); ++tokidx)
                {
                    auto& token = segment.Tokens[tokidx];
                    token.Bounds.top = segment.Bounds.top + style.padding.top +
                        style.superscriptOffset + style.subscriptOffset + style.border.top.thickness +
                        ((segment.Bounds.height - token.Bounds.height) * 0.5f);

                    // TODO: Fix bullet positioning w.r.t. first text block (baseline aligned?)
                    /*if ((token.Type == TokenType::ListItemBullet) && ((tokidx + 1) < (int)segment.Tokens.size()))
                         segment.Tokens[tokidx + 1]*/
                    token.Bounds.left = currx + token.Offset.left;
                    currx += token.Bounds.width + token.Offset.h();
                }

                currx += style.padding.right + style.border.right.thickness;
            }

            HIGHLIGHT("\nCreated line #%d at (%f, %f) of size (%f, %f) with %d segments", index,
                line.Content.left, line.Content.top, line.Content.width, line.Content.height,
                (int)line.Segments.size());
        }
    }

    void CreateElidedTextToken(DrawableLine& line, const StyleDescriptor& style, const RenderConfig& config)
    {
        auto width = config.Bounds.x;
        width = (style.propsSpecified & StyleWidth) != 0 ? std::min(width, style.width) : width;
        auto sz = style.font.font->EllipsisWidth;
        sz = sz > 0.f ? sz : config.GetTextSize("...", style.font.font).x;
        width -= sz;

        if ((style.font.flags & FontStyleOverflowEllipsis) != 0 && width > 0.f)
        {
            auto startx = line.Content.left;

            for (auto& segment : line.Segments)
            {
                for (auto& token : segment.Tokens)
                {
                    startx += token.Bounds.width + token.Offset.h();

                    if (startx > width)
                    {
                        if (token.Type == TokenType::Text)
                        {
                            while (startx > width)
                            {
                                auto partial = token.Content.substr(token.VisibleTextSize - 2);
                                startx -= config.GetTextSize(partial, style.font.font).x;
                                token.VisibleTextSize -= 1;
                            }

                            //token.AddEllipsis = true;
                        }

                        break;
                    }
                }
            }
        }
    }

    DrawableLine MoveToNextLine(TagType tagType, int listDepth, int blockquoteDepth, int styleIdx,
        DrawableLine& line, std::vector<DrawableLine>& result, std::vector<StyleDescriptor>& styles,
        const RenderConfig& config, bool isTagStart)
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
            if (!line.Marquee && config.Bounds.x > 0.f && (styles[styleIdx + 1].font.flags & FontStyleNoWrap) == 0)
                linesModified = PerformWordWrap(result, tagType, (int)result.size() - 1, listDepth, blockquoteDepth, styles, config);
            else linesModified.push_back((int)result.size() - 1);
            AdjustForSuperSubscripts(linesModified, result, styles, config);
        }

        auto& lastline = result.back();
        auto newline = CreateNewLine(styleIdx);
        newline.BlockquoteDepth = blockquoteDepth;
        if (isTagStart) newline.Marquee = tagType == TagType::Marquee;

        if (blockquoteDepth > 0) newline.Offset.left = newline.Offset.right = config.BlockquotePadding;
        if (blockquoteDepth > lastline.BlockquoteDepth) newline.Offset.top = config.BlockquotePadding;
        else if (blockquoteDepth < lastline.BlockquoteDepth) lastline.Offset.bottom = config.BlockquotePadding;

        ComputeLineBounds(result, linesModified, styles, config);
        const auto& style = styles[styleIdx + 1];
        CreateElidedTextToken(result.back(), style, config);

        newline.Content.left = ((float)(listDepth + 1) * config.ListItemIndent) + 
            ((float)(blockquoteDepth + 1) * config.BlockquoteOffset);
        newline.Content.top = lastline.Content.top + lastline.height() + (isEmpty ? 0.f : config.LineGap);
        return newline;
    }

    void GenerateTextToken(DrawableLine& line, std::string_view content,
        int curridx, int start, Drawables& drawables, 
        const RenderConfig& config)
    {
        Token token;
        token.Content = content.substr(start, curridx - start);
        AddToken(line, token, NoStyleChange, drawables, config);
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

    std::pair<int, bool> RecordTagProperties(TagType tagType, std::string_view attribName, std::optional<std::string_view> attribValue, 
        StyleDescriptor& style, TagPropertyDescriptor& tagprops, const StyleDescriptor& parentStyle, 
        const RenderConfig& config)
    {
        int result = 0;
        bool nonStyleAttribute = false;

        if (AreSame(attribName, "style") && IsStyleSupported(tagType))
        {
            if (!attribValue.has_value())
            {
                ERROR("Style attribute value not specified...");
                return { 0, false };
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

                    auto stylePropVal = GetQuotedString(styleProps.data(), sidx, (int)styleProps.size());
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
        {
            tagprops.tooltip = attribValue.value();
            nonStyleAttribute = true;
        }
        else if (tagType == TagType::Hyperlink && AreSame(attribName, "href") && attribValue.has_value()) 
        {
            tagprops.link = attribValue.value();
            nonStyleAttribute = true;
        }
        else if (tagType == TagType::Meter)
        {
            if (AreSame(attribName, "value") && attribValue.has_value()) tagprops.value = ExtractInt(attribValue.value(), 0);
            if (AreSame(attribName, "min") && attribValue.has_value()) tagprops.range.first = ExtractInt(attribValue.value(), 0);
            if (AreSame(attribName, "max") && attribValue.has_value()) tagprops.range.second = ExtractInt(attribValue.value(), 0);
            nonStyleAttribute = true;
        }

        return { result, nonStyleAttribute };
    }

    int CreateNextStyle(std::vector<StyleDescriptor>& styles)
    {
        auto& newstyle = styles.emplace_back(styles.back());
        newstyle.bgcolor = IM_COL32_BLACK_TRANS;
        newstyle.gradient = ColorGradient{};
        return (int)styles.size() - 1;
    }

    void PushTag(std::string_view currTag)
    {
        CurrentStackPos++;
        TagStack[CurrentStackPos].tag = currTag;
    }

    void PopTag(bool reset)
    {
        if (reset) TagStack[CurrentStackPos] = StackData{};
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

    void SetImplicitStyleProps(TagType tagType, std::string_view currTag, 
        StyleDescriptor& style, const StyleDescriptor& parentStyle,
        DrawableLine& line, const RenderConfig& config)
    {
        assert(config.GetFont != nullptr);

        if (tagType == TagType::Header)
        {
            style.font.size = config.HFontSizes[currTag[1] - '1'] * config.FontScale;
            style.font.flags |= FontStyleBold;
            style.propsSpecified = style.propsSpecified | StyleFontStyle | StyleFontSize;
        }
        else if (tagType == TagType::RawText || tagType == TagType::CodeBlock)
        {
            style.font.family = IM_RICHTEXT_MONOSPACE_FONTFAMILY;
            style.propsSpecified = style.propsSpecified | StyleFontFamily;

            if (tagType == TagType::CodeBlock)
            {
                if ((style.propsSpecified & StyleBackground) == 0) style.bgcolor = config.CodeBlockBg;
            }
        }
        else if (tagType == TagType::Italics)
        {
            style.font.flags |= FontStyleItalics;
            style.propsSpecified = style.propsSpecified | StyleFontStyle;
        }
        else if (tagType == TagType::Bold)
        {
            style.font.flags |= FontStyleBold;
            style.propsSpecified = style.propsSpecified | StyleFontStyle;
        }
        else if (tagType == TagType::Mark)
        {
            if ((style.propsSpecified & StyleBackground) == 0) style.bgcolor = config.MarkHighlight;
            style.propsSpecified = style.propsSpecified | StyleBackground;
        }
        else if (tagType == TagType::Small)
        {
            style.font.size = parentStyle.font.size * 0.8f;
            style.propsSpecified = style.propsSpecified | StyleFontSize;
        }
        else if (tagType == TagType::Superscript)
        {
            style.font.size *= config.ScaleSuperscript;
            style.propsSpecified = style.propsSpecified | StyleFontSize;
        }
        else if (tagType == TagType::Subscript)
        {
            style.font.size *= config.ScaleSubscript;
            style.propsSpecified = style.propsSpecified | StyleFontSize;
        }
        else if (tagType == TagType::Underline)
        {
            style.font.flags |= FontStyleUnderline;
            style.propsSpecified = style.propsSpecified | StyleFontStyle;
        }
        else if (tagType == TagType::Strikethrough)
        {
            style.font.flags |= FontStyleStrikethrough;
            style.propsSpecified = style.propsSpecified | StyleFontStyle;
        }
        else if (tagType == TagType::Hyperlink)
        {
            if ((style.propsSpecified & StyleFontStyle) == 0) style.font.flags |= FontStyleUnderline;
            if ((style.propsSpecified & StyleFgColor) == 0) style.fgcolor = config.HyperlinkColor;
            style.propsSpecified = style.propsSpecified | StyleFontStyle | StyleFgColor;
        }
        else if (tagType == TagType::Blink)
        {
            style.blink = true;
            style.propsSpecified = style.propsSpecified | StyleBlink;
        }
        else if (tagType == TagType::Meter)
        {
            if ((style.propsSpecified & StyleBackground) == 0) style.bgcolor = config.MeterBgColor;
            if ((style.propsSpecified & StyleFgColor) == 0) style.fgcolor = config.MeterFgColor;
            style.propsSpecified = style.propsSpecified | StyleBackground | StyleFgColor;
        }
        
        if (style.propsSpecified != NoStyleChange)
        {
            auto isbold = style.font.flags & FontStyleBold;
            auto isitalics = style.font.flags & FontStyleItalics;
            auto islight = style.font.flags & FontStyleLight;
            style.font.font = config.GetFont(style.font.family, style.font.size,
                isbold, isitalics, islight, config.UserData);
        }
    }

#ifdef _DEBUG
    inline void DrawDebugRect(DebugContentType type, ImVec2 startpos, ImVec2 endpos, const RenderConfig& config)
    {
        auto drawList = ImGui::GetForegroundDrawList();
        if (config.DebugContents[type] != IM_COL32_BLACK_TRANS) 
            drawList->AddRect(startpos, endpos, config.DebugContents[type]);
    }

    inline void DrawDebugRect(ImColor color, ImVec2 startpos, ImVec2 endpos)
    {
        auto drawList = ImGui::GetForegroundDrawList();
        drawList->AddRect(startpos, endpos, color);
    }
#else
#define DrawDebugRect(...)
#endif

    template <typename ItrT>
    void DrawLinearGradient(ImDrawList* drawList, ImVec2 initpos, ImVec2 endpos, float angle, ImGuiDir dir, ItrT start, ItrT end)
    {
        auto width = endpos.x - initpos.x;
        auto height = endpos.y - initpos.y;

        if (dir == ImGuiDir::ImGuiDir_Left)
        {
            if (angle != 0.f)
            {
                ImVec2 points[4];
                ImU32 colors[4];

                for (auto it = start; it != end; ++it)
                {
                    auto extent = width * it->pos;
                    auto m = std::tanf(angle);

                    points[0] = initpos;
                    points[1] = points[0] + ImVec2{ extent, 0.f };
                    points[2] = initpos - ImVec2{ m * initpos.y, height };
                    points[3] = points[2] + ImVec2{ extent, 0.f };

                    colors[0] = colors[3] = it->from;
                    colors[1] = colors[2] = it->to;

                    DrawPolyFilledMultiColor(drawList, points, colors, 4);
                    initpos.x += extent;
                }
            }
            else
            {
                for (auto it = start; it != end; ++it)
                {
                    auto extent = width * it->pos;
                    drawList->AddRectFilledMultiColor(initpos, initpos + ImVec2{ extent, height },
                        it->from, it->to, it->to, it->from);
                    initpos.x += extent;
                }
            }
        }
        else if (dir == ImGuiDir::ImGuiDir_Down)
        {
            for (auto it = start; it != end; ++it)
            {
                auto extent = height * it->pos;
                drawList->AddRectFilledMultiColor(initpos, initpos + ImVec2{ width, extent },
                    it->from, it->from, it->to, it->to);
                initpos.y += extent;
            }
        }
    }

    void DrawBackground(ImDrawList* drawList, ImVec2 startpos, ImVec2 endpos,
        const ColorGradient& gradient, uint32_t color, const RenderConfig& config)
    {
        if (gradient.totalStops != 0)
            (gradient.dir == ImGuiDir_Down || gradient.dir == ImGuiDir_Left) ?
            DrawLinearGradient(drawList, startpos, endpos, gradient.angleDegrees, gradient.dir,
                std::begin(gradient.colorStops), std::begin(gradient.colorStops) + gradient.totalStops) :
            DrawLinearGradient(drawList, startpos, endpos, gradient.angleDegrees, gradient.dir,
                std::rbegin(gradient.colorStops), std::rbegin(gradient.colorStops) + gradient.totalStops);
        else if (color != config.DefaultBgColor && color != IM_COL32_BLACK_TRANS)
            drawList->AddRectFilled(startpos, endpos, color);
    }

    std::tuple<int, int, int> DecomposeToRGBChannels(uint32_t color)
    {
        auto mask = (uint32_t)-1;
        return std::make_tuple((int)(color & (mask >> 24)), 
            (int)(color & ((mask >> 16) & (mask << 8))) >> 8,
            (int)(color & ((mask >> 8) & (mask << 16))) >> 16);
    }

    bool DrawOverlay(ImVec2 startpos, ImVec2 endpos, const Token& token, 
        const StyleDescriptor& style, const TagPropertyDescriptor& tagprops)
    {
        const auto& io = ImGui::GetCurrentContext()->IO;
        if (ImRect{ startpos, endpos }.Contains(io.MousePos) && ShowOverlay)
        {
            auto overlay = ImGui::GetForegroundDrawList();
            startpos.y = 0.f;

            char props[2048] = { 0 };
            auto currpos = 0;
            for (auto exp = 0; exp <= 16; ++exp)
            {
                auto prop = 1 << exp;
                if ((style.propsSpecified & prop) != 0)
                {
                    switch (prop)
                    {
                    case NoStyleChange: currpos += std::snprintf(props + currpos, 2047 - currpos, "NoStyleChange,"); break;
                    case StyleBackground: currpos += std::snprintf(props + currpos, 2047 - currpos, "StyleBackground,"); break;
                    case StyleFgColor: currpos += std::snprintf(props + currpos, 2047 - currpos, "StyleFgColor,"); break;
                    case StyleFontSize: currpos += std::snprintf(props + currpos, 2047 - currpos, "StyleFontSize,"); break;
                    case StyleFontFamily: currpos += std::snprintf(props + currpos, 2047 - currpos, "StyleFontFamily,"); break;
                    case StyleFontWeight: currpos += std::snprintf(props + currpos, 2047 - currpos, "StyleFontWeight,"); break;
                    case StyleFontStyle: currpos += std::snprintf(props + currpos, 2047 - currpos, "StyleFontStyle,"); break;
                    case StyleHeight: currpos += std::snprintf(props + currpos, 2047 - currpos, "StyleHeight,"); break;
                    case StyleWidth: currpos += std::snprintf(props + currpos, 2047 - currpos, "StyleWidth,"); break;
                    case StyleListBulletType: currpos += std::snprintf(props + currpos, 2047 - currpos, "StyleListBulletType,"); break;
                    case StylePaddingTop: currpos += std::snprintf(props + currpos, 2047 - currpos, "StylePaddingTop,"); break;
                    case StylePaddingBottom: currpos += std::snprintf(props + currpos, 2047 - currpos, "StylePaddingBottom,"); break;
                    case StylePaddingLeft: currpos += std::snprintf(props + currpos, 2047 - currpos, "StylePaddingLeft,"); break;
                    case StylePaddingRight: currpos += std::snprintf(props + currpos, 2047 - currpos, "StylePaddingRight,"); break;
                    case StyleBlink: currpos += std::snprintf(props + currpos, 2047 - currpos, "StyleBlink,"); break;
                    case StyleNoWrap: currpos += std::snprintf(props + currpos, 2047 - currpos, "StyleNoWrap,"); break;
                    default: break;
                    }
                }
            }

            char buffer[1024 + 2048] = { 0 };
            auto yesorno = [](bool val) { return val ? "Yes" : "No"; };
            auto [fr, fg, fb] = DecomposeToRGBChannels(style.fgcolor);
            auto [br, bg, bb] = DecomposeToRGBChannels(style.bgcolor);

            currpos = std::snprintf(buffer, 1023 + 2048, "Position: (%.2f, %.2f)\nBounds: (%.2f, %.2f)\n", 
                startpos.x, startpos.y, token.Bounds.width, token.Bounds.height);

            currpos += std::snprintf(buffer + currpos, 1023 + 2048 - currpos, 
                "\nProperties Specified: %s\nForeground Color: (%d, %d, %d)\n",
                props, fr, fg, fb);

            if (style.backgroundIdx != -1)
            {
                if (style.gradient.totalStops == 0)
                    if (style.bgcolor != IM_COL32_BLACK_TRANS)
                        currpos += std::snprintf(buffer + currpos, 1023 + 2048 - currpos, "Background Color: (%d, %d, %d)\n", br, bg, bb);
                    else
                        currpos += std::snprintf(buffer + currpos, 1023 + 2048 - currpos, "Background Color: Transparent\n");
                else
                {
                    currpos += std::snprintf(buffer + currpos, 1023 + 2048 - currpos, "Linear Gradient:");

                    for (auto idx = 0; idx < style.gradient.totalStops; ++idx)
                    {
                        auto [r1, g1, b1] = DecomposeToRGBChannels(style.gradient.colorStops[idx].from);
                        auto [r2, g2, b2] = DecomposeToRGBChannels(style.gradient.colorStops[idx].to);
                        currpos += std::snprintf(buffer + currpos, 1023 + 2048 - currpos, "From (%d, %d, %d) To (%d, %d, %d) at %.2f\n",
                            r1, g1, b1, r2, g2, b2, style.gradient.colorStops[idx].pos);
                    }
                }
            }

            currpos += std::snprintf(buffer + currpos, 1023 + 2048 - currpos,
                "\nHeight: %.2fpx\nWidth: %.2fpx\nTooltip: %.*s\nLink: %.*s\nBlink: %s\n",
                style.width, style.height, (int)tagprops.tooltip.size(), tagprops.tooltip.data(),
                (int)tagprops.link.size(), tagprops.link.data(), yesorno(style.blink));

            if (token.Type == TokenType::Text)
            {
                std::snprintf(buffer + currpos, 1023, "\n\nFont.family: %.*s\nFont.size: %.2fpx\nFont.bold: %s\nFont.italics: %f\nFont.underline: %s\n"
                    "Font.strike: %s\nFont.wrap: %s", (int)style.font.family.size(), style.font.family.data(), style.font.size,
                    yesorno(style.font.flags & FontStyleBold), yesorno(style.font.flags & FontStyleItalics), 
                    yesorno(style.font.flags & FontStyleUnderline), yesorno(style.font.flags & FontStyleStrikethrough),
                    yesorno(!(style.font.flags & FontStyleNoWrap)));
            }
            else if (token.Type == TokenType::Meter)
            {
                std::snprintf(buffer + currpos, 1023, "\n\nRange: (%.2f, %.2f)\nValue: %.2f", 
                    tagprops.range.first, tagprops.range.second, tagprops.value);
            }

            auto font = GetOverlayFont();
            ImGui::PushFont(font);
            auto sz = ImGui::CalcTextSize(buffer, buffer + 1023 + 2048, false, 300.f);

            startpos.x = ImGui::GetCurrentWindow()->Size.x - sz.x;
            overlay->AddRectFilled(startpos, startpos + ImVec2{ ImGui::GetCurrentWindow()->Size.x, sz.y }, IM_COL32_WHITE);
            overlay->AddText(font, font->FontSize, startpos, IM_COL32_BLACK, buffer, NULL, 300.f);
            ImGui::PopFont();
            return true;
        }

        return false;
    }

    bool DrawToken(ImDrawList* drawList, int lineidx, const Token& token, ImVec2 initpos,
        ImVec2 bounds, const StyleDescriptor& style, const TagPropertyDescriptor& tagprops, 
        const ListItemTokenDescriptor& listItem, const RenderConfig& config, 
        TooltipData& tooltip, AnimationData& animation)
    {
        auto startpos = token.Bounds.start(initpos);
        auto endpos = token.Bounds.end(initpos);

        if ((style.blink && animation.isVisible) || !style.blink)
        {
            if (token.Type == TokenType::HorizontalRule)
            {
                drawList->AddRectFilled(startpos, endpos, style.fgcolor);
            }
            else if (token.Type == TokenType::ListItemBullet)
            {
                auto bulletscale = Clamp(config.BulletSizeScale, 1.f, 4.f);
                auto bulletsz = (style.font.size) / bulletscale;

                if (style.list.itemStyle == BulletType::Custom)
                {
                    if (config.DrawBullet != nullptr) {
                        config.DrawBullet(startpos, endpos, style, listItem.ListItemIndex,
                            listItem.ListDepth, config.UserData);
                    }
                    else DrawBullet(drawList, initpos, token.Bounds, BulletType::FilledCircle, style.fgcolor, bulletsz);
                }
                else DrawBullet(drawList, initpos, token.Bounds, style.list.itemStyle, style.fgcolor, bulletsz);
            }
            else if (token.Type == TokenType::ListItemNumbered)
            {
                drawList->AddText(startpos, style.fgcolor, listItem.NestedListItemIndex);
            }
            else if (token.Type == TokenType::Meter)
            {
                auto border = ImVec2{ 1.f, 1.f };
                auto borderRadius = (endpos.y - startpos.y) * 0.5f;
                auto diff = tagprops.range.second - tagprops.range.first;
                auto progress = (tagprops.value / diff) * token.Bounds.width;

                drawList->AddRectFilled(startpos, endpos, style.bgcolor, borderRadius, ImDrawFlags_RoundCornersAll);
                drawList->AddRect(startpos, endpos, config.MeterBorderColor, borderRadius, ImDrawFlags_RoundCornersAll);
                drawList->AddRectFilled(startpos + border, startpos - border + ImVec2{ progress, token.Bounds.height },
                    style.fgcolor, borderRadius, ImDrawFlags_RoundCornersBottomLeft | ImDrawFlags_RoundCornersTopLeft);
            }
            else
            {
                auto textend = token.Content.data() + token.VisibleTextSize;
                auto halfh = token.Bounds.height * 0.5f;
                drawList->AddText(startpos, style.fgcolor, token.Content.data(), textend);

                if (style.font.flags & FontStyleStrikethrough) drawList->AddLine(startpos + ImVec2{ 0.f, halfh }, endpos + ImVec2{ 0.f, -halfh }, style.fgcolor);
                if (style.font.flags & FontStyleUnderline) drawList->AddLine(startpos + ImVec2{ 0.f, token.Bounds.height }, endpos, style.fgcolor);

                if (!tagprops.tooltip.empty())
                {
                    if (!(style.font.flags & FontStyleUnderline))
                    {
                        // TODO: Refactor this out
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
                        tooltip.content = tagprops.tooltip;
                    }
                }
                else if (!tagprops.link.empty())
                {
                    if (ImRect{ startpos, endpos }.Contains(ImGui::GetIO().MousePos))
                    {
                        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                        if (ImGui::GetIO().MouseReleased[0] && config.HandleHyperlink)
                            config.HandleHyperlink(tagprops.link, config.UserData);
                    }
                    else
                        ImGui::SetMouseCursor(ImGuiMouseCursor_Arrow);
                }
            }
        }

        if (DrawOverlay(startpos, endpos, token, style, tagprops)) DrawDebugRect(ContentTypeToken, startpos, endpos, config);
        if ((token.Bounds.left + token.Bounds.width) > (bounds.x + initpos.x)) return false;
        return true;
    }

    bool DrawSegment(ImDrawList* drawList, int lineidx, const SegmentData& segment,
        ImVec2 initpos, ImVec2 bounds, const Drawables& result, const RenderConfig& config,
        TooltipData& tooltip, AnimationData& animation)
    {
        if (segment.Tokens.empty()) return true;

        auto popFont = false;
        const auto& style = result.StyleDescriptors[segment.StyleIdx + 1];

        if (style.font.font != nullptr)
        {
            ImGui::PushFont(style.font.font);
            popFont = true;
        }

        auto drawTokens = true;
        auto startpos = segment.Bounds.start(initpos), endpos = segment.Bounds.end(initpos);

        if ((style.blink && animation.isVisible) || !style.blink)
            DrawBackground(drawList, startpos, endpos, style.gradient, style.bgcolor, config);

        for (const auto& token : segment.Tokens)
        {
            const auto& listItem = token.ListPropsIdx == -1 ? InvalidListItemToken :
                result.ListItemTokens[token.ListPropsIdx];
            const auto& tagprops = token.PropertiesIdx == -1 ? InvalidTagPropDesc :
                result.TagDescriptors[token.PropertiesIdx];
            if (drawTokens && !DrawToken(drawList, lineidx, token, initpos, bounds, style,
                tagprops, listItem, config, tooltip, animation))
            {
                drawTokens = false; 
                break;
            }
        }

        // TODO: Take into account line type as well
        if ((style.propsSpecified & StyleBorderUniform) != 0 &&
            style.border.top.thickness > 0.f && style.border.top.color != style.bgcolor)
        {
            auto drawflags = 0;
            if ((style.border.rounding & TopLeftCorner) != 0) drawflags = ImDrawFlags_RoundCornersTopLeft;
            if ((style.border.rounding & TopRightCorner) != 0) drawflags = ImDrawFlags_RoundCornersTopRight;
            if ((style.border.rounding & BottomRightCorner) != 0) drawflags = ImDrawFlags_RoundCornersBottomRight;
            if ((style.border.rounding & BottomLeftCorner) != 0) drawflags = ImDrawFlags_RoundCornersBottomLeft;
            drawList->AddRect(startpos, endpos, style.border.top.color, style.border.radius, drawflags, style.border.top.thickness);
        }
        else
        {
            auto width = endpos.x - startpos.x, height = endpos.y - startpos.y;

            if (style.border.top.thickness > 0.f && style.border.top.color != style.bgcolor)
                drawList->AddRect(startpos, startpos + ImVec2{ width, 0.f }, style.border.top.color, 0.f, 
                    0, style.border.top.thickness);
            if (style.border.right.thickness > 0.f && style.border.right.color != style.bgcolor)
                drawList->AddRect(startpos + ImVec2{ width - style.border.right.thickness, 0.f }, endpos - 
                    ImVec2{ style.border.right.thickness, 0.f },
                    style.border.right.color, 0.f, 0, style.border.right.thickness);
            if (style.border.left.thickness > 0.f && style.border.left.color != style.bgcolor)
                drawList->AddRect(startpos, startpos + ImVec2{ 0.f, height }, style.border.left.color, 0.f,
                    0, style.border.left.thickness);
            if (style.border.bottom.thickness > 0.f && style.border.bottom.color != style.bgcolor)
                drawList->AddRect(startpos + ImVec2{ 0.f, height - style.border.bottom.thickness }, endpos - 
                    ImVec2{ 0.f, style.border.bottom.thickness }, style.border.bottom.color, 0.f,
                    0, style.border.bottom.thickness);
        }

        DrawDebugRect(ContentTypeSegment, startpos, endpos, config);

        if (popFont) ImGui::PopFont();
        return drawTokens;
    }

    void DrawForegroundLayer(ImDrawList* drawList, ImVec2 initpos, ImVec2 bounds, 
        const std::vector<DrawableLine>& lines, const Drawables& result,
        const RenderConfig& config, TooltipData& tooltip, AnimationData& animation)
    {
        int listCountByDepth[IM_RICHTEXT_MAX_LISTDEPTH];
        int listDepth = -1;

        for (auto lineidx = 0; lineidx < (int)lines.size(); ++lineidx)
        {
            if (lines[lineidx].Segments.empty()) continue;

            for (const auto& segment : lines[lineidx].Segments)
            {
                auto linestart = initpos;
                if (lines[lineidx].Marquee) linestart.x += animation.xoffsets[lineidx];
                if (!DrawSegment(drawList, lineidx, segment, linestart, bounds, result, config, tooltip, animation))
                    break;
            }
            
#ifdef _DEBUG
            auto linestart = lines[lineidx].Content.start(initpos) + ImVec2{ lines[lineidx].Offset.left, lines[lineidx].Offset.top };
            auto lineend = lines[lineidx].Content.end(initpos);
            DrawDebugRect(ContentTypeLine, linestart, lineend, config);
#endif
            if ((lines[lineidx].Content.top + lines[lineidx].height()) > (bounds.y + initpos.y)) break;
        }
    }

    void DrawBackgroundLayer(ImDrawList* drawList, ImVec2 initpos, ImVec2 bounds, 
        const std::vector<BackgroundShape>& shapes, const RenderConfig& config)
    {
        for (const auto& shape : shapes)
        {
            DrawBackground(drawList, shape.Start + initpos, shape.End + initpos,
                shape.Gradient, shape.Color, config);
            DrawDebugRect(ContentTypeBg, shape.Start + initpos, shape.End + initpos, config);
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
            animation.xoffsets.resize(drawables.ForegroundLines.size());
            std::fill(animation.xoffsets.begin(), animation.xoffsets.end(), 0.f);
        }

        auto currFrameTime = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();

        ImGui::PushClipRect(pos, endpos, true);
        drawList->AddRectFilled(pos, endpos, config->DefaultBgColor);

        DrawBackgroundLayer(drawList, pos, bounds, drawables.BackgroundShapes, *config);
        DrawForegroundLayer(drawList, pos, bounds, drawables.ForegroundLines, drawables, *config, tooltip, animation);
        DrawTooltip(drawList, tooltip, *config);

        if (!config->IsStrictHTML5 && (currFrameTime - animation.lastBlinkTime > IM_RICHTEXT_BLINK_ANIMATION_INTERVAL))
        {
            animation.isVisible = !animation.isVisible;
            animation.lastBlinkTime = currFrameTime;
            if (config->RequestFrame) config->RequestFrame(config->UserData);
        }

        if (currFrameTime - animation.lastMarqueeTime > IM_RICHTEXT_MARQUEE_ANIMATION_INTERVAL)
        {
            for (auto lineidx = 0; lineidx < (int)animation.xoffsets.size(); ++lineidx)
            {
                animation.xoffsets[lineidx] += 1.f;
                auto linewidth = drawables.ForegroundLines[lineidx].width();

                if (animation.xoffsets[lineidx] >= linewidth)
                    animation.xoffsets[lineidx] = -linewidth;

                if (config->RequestFrame) config->RequestFrame(config->UserData);
            }

            animation.lastMarqueeTime = currFrameTime;
        }

        ImGui::PopClipRect();
    }

    bool ShowDrawables(std::string_view content, Drawables& drawables, ImVec2 bounds, RenderConfig* config)
    {
        ImGuiWindow* window = ImGui::GetCurrentWindow();
        if (window->SkipItems)
            return false;

        const auto& style = ImGui::GetCurrentContext()->Style;
        auto id = window->GetID(content.data(), content.data() + content.size());
        auto pos = window->DC.CursorPos;
        ImGui::ItemSize(bounds);
        ImGui::ItemAdd(ImRect{ pos, pos + bounds }, id);
        Draw(content, drawables, pos + style.FramePadding, bounds, config);
        return true;
    }

    RenderConfig* GetDefaultConfig(ImVec2 Bounds, float defaultFontSize, float fontScale, bool skipDefaultFontLoading)
    {
        DefaultRenderConfigInit = true;

        auto config = &DefaultRenderConfig;
        config->Bounds = Bounds;
        config->NamedColor = &GetColor;
        config->EscapeCodes.insert(config->EscapeCodes.end(), std::begin(EscapeCodes),
            std::end(EscapeCodes));
        config->FontScale = fontScale;
        config->DefaultFontSize = defaultFontSize;
        config->MeterDefaultSize = { defaultFontSize * 5.0f, defaultFontSize };

#ifdef IM_RICHTEXT_DEFAULT_FONTS_AVAILABLE
        config->GetFont = &GetFont;
        config->GetTextSize = &GetTextSize;
        if (!skipDefaultFontLoading) LoadDefaultFonts(*config);
#endif
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

    bool IsContentMultiline(TagType type)
    {
        switch (type)
        {
            case ImRichText::TagType::Span: [[fallthrough]];
            case ImRichText::TagType::Subscript: [[fallthrough]];
            case ImRichText::TagType::Superscript: [[fallthrough]];
            case ImRichText::TagType::Marquee: return false;
            default: return true;
        }
    }

    struct DefaultTagVisitor final : public ITagVisitor
    {
        std::string_view currTag;
        char TabSpaces[IM_RICHTEXT_MAXTABSTOP] = { 0 };
        int StyleIndexStack[IM_RICHTEXT_MAXDEPTH] = { 0 };

        DrawableLine currline;
        StyleDescriptor tempstyle;
        TagPropertyDescriptor tagPropDesc;

        int styleIdx = -1;
        int listDepth = -1, blockquoteDepth = -1;
        int subscriptLevel = 0, superscriptLevel = 0;
        bool currentListIsNumbered = false, tagHasAttributes = false;
        float maxWidth = 0.f;
        TagType tagType = TagType::Unknown;
        
        const RenderConfig& config;
        Drawables& result;

        DefaultTagVisitor(const RenderConfig& cfg, Drawables& res) 
            : config{ cfg }, result{ res }
        {
            CurrentStackPos = -1;
            std::memset(TabSpaces, ' ', Clamp(config.TabStop, 0, IM_RICHTEXT_MAXTABSTOP - 1));
            std::memset(ListItemCountByDepths, 0, IM_RICHTEXT_MAX_LISTDEPTH);
            std::memset(StyleIndexStack, -2, IM_RICHTEXT_MAXDEPTH);
            TabSpaces[config.TabStop] = 0;
            result.StyleDescriptors.emplace_back(CreateDefaultStyle(config));
            tempstyle = result.StyleDescriptors.front();
            maxWidth = config.Bounds.x;
        }

        void Finalize()
        {
            MoveToNextLine(tagType, listDepth, blockquoteDepth, styleIdx, currline, result.ForegroundLines, result.StyleDescriptors, config, true);
            maxWidth = std::max(maxWidth, result.ForegroundLines.back().Content.width);

            for (auto& line : result.ForegroundLines)
            {
                if (line.Marquee) line.Content.width = maxWidth;
            }

            for (auto depth = 0; depth < IM_RICHTEXT_MAXDEPTH; ++depth)
            {
                for (const auto& bound : BlockquoteStack[depth].bounds)
                {
                    if (config.BlockquoteBarWidth > 1.f && config.DefaultBgColor != config.BlockquoteBar)
                        result.BackgroundShapes.emplace_back(BackgroundShape{ bound.first, ImVec2{ config.BlockquoteBarWidth, bound.second.y },
                            config.BlockquoteBar });

                    if (config.DefaultBgColor != config.BlockquoteBg)
                        result.BackgroundShapes.emplace_back(BackgroundShape{ ImVec2{ bound.first.x + config.BlockquoteBarWidth, bound.first.y },
                            bound.second, config.BlockquoteBg });
                }

                // Create multi-line background shapes for each depth and reset original specifications
                for (const auto& bgdata : BackgroundSpans[depth])
                {
                    if (bgdata.span.end.first == -1) continue;

                    BackgroundShape background;
                    auto& firstSegment = result.ForegroundLines[bgdata.span.start.first].Segments[bgdata.span.start.second];
                    const auto& lastSegment = result.ForegroundLines[bgdata.span.end.first].Segments[bgdata.span.end.second];
                    auto& firstStyle = result.StyleDescriptors[bgdata.styleIdx + 1];

                    background.Start = { firstSegment.Bounds.left, firstSegment.Bounds.top };
                    background.End = { lastSegment.Bounds.left + lastSegment.Bounds.width,
                        lastSegment.Bounds.top + result.ForegroundLines[bgdata.span.end.first].height() };
                    background.Start.x = std::min(background.Start.x, lastSegment.Bounds.left);
                    background.End.x = std::max(background.End.x, firstSegment.Bounds.left + firstSegment.Bounds.width);
                    background.Color = firstStyle.bgcolor;
                    background.Gradient = firstStyle.gradient;

                    firstStyle.bgcolor = IM_COL32_BLACK_TRANS;
                    firstStyle.gradient = ColorGradient{};
                    firstStyle.backgroundIdx = (int)result.BackgroundShapes.size();
                    result.BackgroundShapes.push_back(background);
                }
            }
        }

        StyleDescriptor& Style(int stackpos)
        {
            return stackpos < 0 ? result.StyleDescriptors.front() : result.StyleDescriptors[TagStack[stackpos].styleIdx + 1];
        }

        bool CreateNewStyle()
        {
            auto parentIdx = CurrentStackPos <= 0 ? -1 : StyleIndexStack[CurrentStackPos - 1];
            const auto& parentStyle = result.StyleDescriptors[parentIdx + 1];
            SetImplicitStyleProps(tagType, currTag, tempstyle, parentStyle, currline, config);
            auto hasUniqueStyle = tempstyle.propsSpecified != 0;

            if (hasUniqueStyle)
            {
                result.StyleDescriptors.emplace_back(tempstyle);
                styleIdx = ((int)result.StyleDescriptors.size() - 2);
                AddSegment(currline, styleIdx, result.StyleDescriptors, config);
            }

            styleIdx = ((int)result.StyleDescriptors.size() - 2);
            StyleIndexStack[CurrentStackPos] = styleIdx;
            TagStack[CurrentStackPos].styleIdx = styleIdx;
            return hasUniqueStyle;
        }

        bool TagStart(std::string_view tag)
        {
            if (!IsContentMultiline(tagType) && AreSame(tag, "br")) return true;

            LOG("Entering Tag: <%.*s>\n", (int)tag.size(), tag.data());
            currTag = tag;
            tagType = GetTagType(tag);
            
            PushTag(currTag);
            if (tagType == TagType::Superscript) superscriptLevel++;
            else if (tagType == TagType::Subscript) subscriptLevel++;

            if (CurrentStackPos >= 0 && TagStack[CurrentStackPos].tag != currTag)
                ERROR("Tag mismatch...");
            return true;
        }
        
        bool Attribute(std::string_view name, std::optional<std::string_view> value)
        {
            LOG("Reading attribute: %.*s\n", (int)name.size(), name.data());
            const auto& parentStyle = Style(CurrentStackPos - 1);
            std::tie(tempstyle.propsSpecified, tagHasAttributes) = RecordTagProperties(
                tagType, name, value, tempstyle, tagPropDesc, parentStyle, config);
            return true;
        }

        bool TagStartDone()
        {
            auto hasSegments = !currline.Segments.empty();
            auto hasUniqueStyle = CreateNewStyle();
            auto segmentAdded = hasUniqueStyle;
            auto& currentStyle = Style(CurrentStackPos);
            int16_t tagPropIdx = -1;

            if (tagHasAttributes)
            {
                tagPropIdx = (int16_t)result.TagDescriptors.size();
                result.TagDescriptors.emplace_back(tagPropDesc);
            }

            if (tagType == TagType::List)
            {
                listDepth++;
                currentListIsNumbered = AreSame(currTag, "ol");
            }
            else if (tagType == TagType::Paragraph || tagType == TagType::Header ||
                tagType == TagType::RawText || tagType == TagType::ListItem ||
                tagType == TagType::CodeBlock || tagType == TagType::Marquee)
            {
                if (hasSegments)
                    currline = MoveToNextLine(tagType, listDepth, blockquoteDepth, styleIdx, currline, result.ForegroundLines, result.StyleDescriptors, config, true);
                maxWidth = std::max(maxWidth, result.ForegroundLines.empty() ? 0.f : result.ForegroundLines.back().Content.width);

                if (tagType == TagType::Paragraph && config.ParagraphStop > 0)
                    currline.Offset.left += config.GetTextSize(std::string_view{ LineSpaces,
                        (std::size_t)std::min(config.ParagraphStop, IM_RICHTEXT_MAXTABSTOP) }, currentStyle.font.font).x;
                else if (tagType == TagType::ListItem)
                {
                    ListItemCountByDepths[listDepth]++;

                    Token token;
                    auto& listItem = result.ListItemTokens.emplace_back();
                    token.Type = !currentListIsNumbered ? TokenType::ListItemBullet :
                        TokenType::ListItemNumbered;
                    listItem.ListDepth = listDepth;
                    listItem.ListItemIndex = ListItemCountByDepths[listDepth];
                    token.ListPropsIdx = (int16_t)(result.ListItemTokens.size() - 1u);

                    AddToken(currline, token, currentStyle.propsSpecified, result, config);
                    AddSegment(currline, styleIdx, result.StyleDescriptors, config);
                    segmentAdded = true;
                }
            }
            else if (tagType == TagType::Blockquote)
            {
                blockquoteDepth++;
                if (!currline.Segments.empty())
                    currline = MoveToNextLine(tagType, listDepth, blockquoteDepth, styleIdx, currline, result.ForegroundLines, result.StyleDescriptors, config, true);
                maxWidth = std::max(maxWidth, result.ForegroundLines.empty() ? 0.f : result.ForegroundLines.back().Content.width);
                auto& start = BlockquoteStack[blockquoteDepth].bounds.emplace_back();
                start.first = ImVec2{ currline.Content.left, currline.Content.top };
            }
            else if (tagType == TagType::Quotation)
            {
                Token token;
                token.Type = TokenType::Text;
                token.Content = "\"";
                AddToken(currline, token, currentStyle.propsSpecified, result, config);
            }
            else if (tagType == TagType::Meter)
            {
                Token token;
                token.Type = TokenType::Meter;
                token.PropertiesIdx = tagPropIdx;
                AddToken(currline, token, currentStyle.propsSpecified, result, config);
            }

            if (currline.Segments.empty())
                AddSegment(currline, styleIdx, result.StyleDescriptors, config);

            auto& segment = currline.Segments.back();
            segment.SubscriptDepth = subscriptLevel;
            segment.SuperscriptDepth = superscriptLevel;

            if ((currentStyle.propsSpecified & StyleBackground) != 0 && IsContentMultiline(tagType))
            {
                // The current line is `currline` which is not yet added, hence record .size()
                auto& bgidx = BackgroundSpans[CurrentStackPos].emplace_back();
                bgidx.span.start.first = (int)result.ForegroundLines.size();
                bgidx.span.start.second = (int)currline.Segments.size() - 1;
                bgidx.styleIdx = styleIdx;
            }

            return true;
        }

        bool Content(std::string_view content)
        {
            // Ignore newlines, tabs & consecutive spaces
            auto curridx = 0, start = 0;
            auto& currentStyle = Style(CurrentStackPos);
            LOG("Processing content [%.*s]\n", (int)content.size(), content.data());

            if (currline.Segments.empty()) currline.Segments.emplace_back().StyleIdx = styleIdx;

            while (curridx < (int)content.size())
            {
                if (content[curridx] == '\n')
                {
                    GenerateTextToken(currline, content, curridx, start, result, config);
                    while (curridx < (int)content.size() && content[curridx] == '\n') curridx++;
                    start = curridx;
                }
                else if (content[curridx] == '\t')
                {
                    GenerateTextToken(currline, content, curridx, start, result, config);
                    while (curridx < (int)content.size() && content[curridx] == '\t') curridx++;
                    start = curridx;
                }
                else if (content[curridx] == config.EscapeSeqStart)
                {
                    GenerateTextToken(currline, content, curridx, start, result, config);

                    curridx++;
                    auto [hasEscape, isNewLine] = AddEscapeSequences(content, curridx, config.EscapeCodes,
                        config.EscapeSeqStart, config.EscapeSeqEnd, currline, start);
                    curridx = start;

                    if (isNewLine && !subscriptLevel && !superscriptLevel)
                    {
                        currline = MoveToNextLine(tagType, listDepth, blockquoteDepth, styleIdx, currline, result.ForegroundLines, result.StyleDescriptors, config, true);
                        maxWidth = std::max(maxWidth, result.ForegroundLines.back().Content.width);
                    }

                    if (hasEscape) continue;
                }
                else if (content[curridx] == ' ')
                {
                    curridx++;

                    if (curridx - start > 0) GenerateTextToken(currline, content, curridx, start, result, config);
                    curridx = SkipSpace(content, curridx);
                    start = curridx;
                    continue;
                }

                curridx++;
            }

            if (curridx > start) GenerateTextToken(currline, content, curridx, start, result, config);
            return true;
        }

        bool PreFormattedContent(std::string_view content)
        {
            auto curridx = 0, start = 0;
            auto& currentStyle = Style(CurrentStackPos);

            while (curridx < (int)content.size())
            {
                if (content[curridx] == '\n')
                {
                    if (!subscriptLevel && !superscriptLevel)
                    {
                        GenerateTextToken(currline, content, curridx, start, result, config);
                        currline = MoveToNextLine(tagType, listDepth, blockquoteDepth, styleIdx, currline, result.ForegroundLines, result.StyleDescriptors, config, true);
                        maxWidth = std::max(maxWidth, result.ForegroundLines.back().Content.width);
                        start = curridx;
                    }
                    else
                    {
                        GenerateTextToken(currline, content, curridx, start, result, config);
                        start = curridx;
                    }
                }

                ++curridx;
            }

            if (curridx > start) GenerateTextToken(currline, content, curridx, start, result, config);
            return true;
        }

        bool TagEnd(std::string_view tag, bool selfTerminatingTag)
        {
            if (!IsContentMultiline(tagType) && AreSame(tag, "br")) return true;

            // pop stye properties and reset
            auto hasBgSpecified = (Style(CurrentStackPos).propsSpecified & StyleBackground) != 0;
            StyleIndexStack[CurrentStackPos] = -2;
            PopTag(!selfTerminatingTag);
            styleIdx = CurrentStackPos >= 0 ? StyleIndexStack[CurrentStackPos] : -1;
            auto parentIdx = CurrentStackPos < 0 ? -1 : StyleIndexStack[CurrentStackPos];

            // Make tempstyle refer to parent style, reset non-inheritable property
            tempstyle = result.StyleDescriptors[parentIdx + 1];
            tempstyle.bgcolor = IM_COL32_BLACK_TRANS;
            tempstyle.gradient = ColorGradient{};
            tempstyle.propsSpecified = 0;
            tempstyle.superscriptOffset = tempstyle.subscriptOffset = 0.f;

            auto segmentAdded = false;
            LOG("Exited Tag: <%.*s>\n", (int)currTag.size(), currTag.data());

            if (tagType == TagType::List || tagType == TagType::Paragraph || tagType == TagType::Header ||
                tagType == TagType::RawText || tagType == TagType::Blockquote || tagType == TagType::LineBreak ||
                tagType == TagType::CodeBlock || tagType == TagType::Marquee)
            {
                if (tagType == TagType::List)
                {
                    ListItemCountByDepths[listDepth] = 0;
                    listDepth--;
                }

                currline.Marquee = tagType == TagType::Marquee;
                currline = MoveToNextLine(tagType, listDepth, blockquoteDepth, styleIdx, currline, result.ForegroundLines, result.StyleDescriptors, config, false);
                maxWidth = std::max(maxWidth, result.ForegroundLines.back().Content.width);

                if (tagType == TagType::Blockquote)
                {
                    assert(!BlockquoteStack[blockquoteDepth].bounds.empty());
                    auto& bounds = BlockquoteStack[blockquoteDepth].bounds.back();
                    const auto& lastLine = result.ForegroundLines[result.ForegroundLines.size() - 2u];
                    bounds.second = ImVec2{ lastLine.width() + bounds.first.x, lastLine.Content.top + lastLine.height() };
                    blockquoteDepth--;

                    /*AddSegment(currline, styleIdx, result.StyleDescriptors, config);
                    segmentAdded = true;*/
                }
                else if (tagType == TagType::Header)
                {
                    // Add properties for horizontal line below header
                    StyleDescriptor style = tempstyle;
                    style.height = 1.f;
                    style.fgcolor = config.HeaderLineColor;
                    style.padding.top = style.padding.bottom = config.HrVerticalMargins;
                    result.StyleDescriptors.emplace_back(style);
                    currline.Segments.back().StyleIdx = (int)result.StyleDescriptors.size() - 2;

                    Token token;
                    token.Type = TokenType::HorizontalRule;
                    AddToken(currline, token, NoStyleChange, result, config);

                    // Move to next line for other content
                    currline = MoveToNextLine(tagType, listDepth, blockquoteDepth, styleIdx, currline, result.ForegroundLines, result.StyleDescriptors, config, false);
                    maxWidth = std::max(maxWidth, result.ForegroundLines.back().Content.width);
                }
            }
            else if (tagType == TagType::Hr)
            {
                // Since hr style is popped, refer to next item in stack
                auto& prevstyle = Style(CurrentStackPos + 1);
                prevstyle.padding.top = prevstyle.padding.bottom = config.HrVerticalMargins;
                if (!currline.Segments.empty())
                    currline = MoveToNextLine(tagType, listDepth, blockquoteDepth, styleIdx, currline, result.ForegroundLines, result.StyleDescriptors, config, false);
                maxWidth = std::max(maxWidth, result.ForegroundLines.empty() ? 0.f : result.ForegroundLines.back().Content.width);

                Token token;
                token.Type = TokenType::HorizontalRule;
                AddToken(currline, token, NoStyleChange, result, config);

                currline = MoveToNextLine(tagType, listDepth, blockquoteDepth, styleIdx, currline, result.ForegroundLines, result.StyleDescriptors, config, true);
                maxWidth = std::max(maxWidth, result.ForegroundLines.back().Content.width);
            }
            else if (tagType == TagType::Quotation)
            {
                Token token;
                token.Type = TokenType::Text;
                token.Content = "\"";
                AddToken(currline, token, NoStyleChange, result, config);
            }
            else if (tagType != TagType::Unknown)
            {
                AddSegment(currline, styleIdx, result.StyleDescriptors, config);
                segmentAdded = true;

                if (tagType == TagType::Superscript) superscriptLevel--;
                else if (tagType == TagType::Subscript) subscriptLevel--;
            }

            // If background spans multiple lines, record it separately
            if (!selfTerminatingTag && !BackgroundSpans[CurrentStackPos + 1].empty() && hasBgSpecified
                && (int)result.ForegroundLines.size() > BackgroundSpans[CurrentStackPos + 1].back().span.start.first
                && BackgroundSpans[CurrentStackPos + 1].back().span.end.first == -1)
            {
                auto& lastLineIdx = BackgroundSpans[CurrentStackPos + 1].back();
                lastLineIdx.span.end.first = (int)result.ForegroundLines.size();
                lastLineIdx.span.end.second = std::max(0, (int)currline.Segments.size() - (segmentAdded ? 2 : 1));
            }

            if (selfTerminatingTag) TagStack[CurrentStackPos + 1] = StackData{};
            currTag = CurrentStackPos == -1 ? "" : TagStack[CurrentStackPos].tag;
            tagType = TagType::Unknown;
            tagHasAttributes = false;
            tagPropDesc = TagPropertyDescriptor{};
            return true;
        }

        void Error(std::string_view tag)
        {
            // TODO
        }

        bool IsSelfTerminating(std::string_view tag) const
        {
            return AreSame(tag, "br") || AreSame(tag, "hr");
        }

        bool IsPreformattedContent(std::string_view tag) const
        {
            return AreSame(tag, "code") || AreSame(tag, "pre");
        }
    };

    Drawables GetDrawables(const char* text, const char* textend, const RenderConfig& config)
    {
        Drawables result;
        DefaultTagVisitor visitor{ config, result };
        ParseRichText(text, textend, config.TagStart, config.TagEnd, visitor);
        return result;
    }

    ImVec2 GetBounds(const Drawables& drawables, ImVec2 bounds)
    {
        ImVec2 result = bounds;
        const auto& style = ImGui::GetCurrentContext()->Style;

        if (bounds.x < 0.f)
        {
            float width = 0.f;
            for (const auto& line : drawables.ForegroundLines)
                width = std::max(width, line.width() + line.Content.left);
            for (const auto& bg : drawables.BackgroundShapes)
                width = std::max(width, bg.End.x);
            result.x = width + (2.f * style.FramePadding.x);
        }

        if (bounds.y < 0.f)
        {
            auto fgheight = 0.f, bgheight = 0.f;

            if (!drawables.ForegroundLines.empty())
            {
                const auto& lastFg = drawables.ForegroundLines.back();
                fgheight = lastFg.height() + lastFg.Content.top;
            }
            
            if (!drawables.BackgroundShapes.empty())
                bgheight = drawables.BackgroundShapes.back().End.y;
           
            result.y = std::max(fgheight, bgheight) + (2.f * style.FramePadding.y);
        }

        return result;
    }

    ImVec2 ComputeBounds(Drawables& drawables, RenderConfig* config)
    {
        auto bounds = GetBounds(drawables, config->Bounds);

        // <hr> elements may not have width unless pre-specified, hence update them
        for (auto& line : drawables.ForegroundLines)
            for (auto& segment : line.Segments)
                for (auto& token : segment.Tokens)
                    if ((token.Type == TokenType::HorizontalRule) && ((drawables.StyleDescriptors[segment.StyleIdx + 1].propsSpecified & StyleWidth) == 0)
                        && token.Bounds.width == -1.f)
                        token.Bounds.width = segment.Bounds.width = line.Content.width = bounds.x;
        return bounds;
    }

    bool Show(const char* text, const char* textend)
    {
        if (textend == nullptr) textend = text + std::strlen(text);
        if (textend - text == 0) return false;

        auto config = GetRenderConfig();
        if (config->Bounds.x == 0 || config->Bounds.y == 0) return false;

        auto drawables = GetDrawables(text, textend, *config);
        auto bounds = ComputeBounds(drawables, config);
        return ShowDrawables(std::string_view{ text, (size_t)(textend - text) }, drawables, bounds, config);
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

            if (config != drawdata.config || config->Scale != drawdata.scale || 
                config->FontScale != drawdata.fontScale || 
                config->DefaultBgColor != drawdata.bgcolor || drawdata.contentChanged)
            {
                drawdata.contentChanged = false;
                drawdata.config = config;
                drawdata.bgcolor = config->DefaultBgColor;
                drawdata.scale = config->Scale;
                drawdata.fontScale = config->FontScale;

#ifdef _DEBUG
                auto ts = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock().now().time_since_epoch());
                drawdata.drawables = GetDrawables(drawdata.richText.data(),
                    drawdata.richText.data() + drawdata.richText.size(), *config);
                ts = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock().now().time_since_epoch()) - ts;
                HIGHLIGHT("\nParsing [#%d] took %lldus", (int)richTextId, ts.count());
#else
                drawdata.drawables = GetDrawables(drawdata.richText.data(),
                    drawdata.richText.data() + drawdata.richText.size(), *config);
#endif
            }

            auto bounds = ComputeBounds(drawdata.drawables, config);
            ShowDrawables(drawdata.richText, drawdata.drawables, bounds, config);
            return true;
        }

        return false;
    }

    bool ToggleOverlay()
    {
        ShowOverlay = !ShowOverlay;
        return ShowOverlay;
    }
}