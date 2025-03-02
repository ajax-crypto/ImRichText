#include "ImRichText.h"
#if __has_include("imrichtextfont.h")
#include "imrichtextfont.h"
#endif
#include "imrichtextutils.h"
#include <unordered_map>
#include <cstring>
#include <optional>
#include <string>
#include <chrono>
#include <deque>

#ifdef _WIN32
#pragma warning( push )
#pragma warning( disable : 4244)
#endif

#ifdef _DEBUG
#include <cstdio>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#undef ERROR
#define ERROR(FMT, ...) { \
    CONSOLE_SCREEN_BUFFER_INFO cbinfo; \
    auto h = GetStdHandle(STD_ERROR_HANDLE); \
    GetConsoleScreenBufferInfo(h, &cbinfo); \
    SetConsoleTextAttribute(h, FOREGROUND_RED | FOREGROUND_INTENSITY); \
    std::fprintf(stderr, FMT, __VA_ARGS__); \
    SetConsoleTextAttribute(h, cbinfo.wAttributes); }
#undef DrawText
#else
#define ERROR(FMT, ...) std::fprintf(stderr, "\x1B[31m" FMT "\x1B[0m", __VA_ARGS__)
#endif

static const char* GetTokenTypeString(const ImRichText::Token& token)
{
    switch (token.Type)
    {
    case ImRichText::TokenType::ElidedText:
    case ImRichText::TokenType::Text: return "Text";
    case ImRichText::TokenType::HorizontalRule: return "HorizontalRule";
    case ImRichText::TokenType::ListItemBullet: return "ListItemBullet";
    case ImRichText::TokenType::ListItemNumbered: return "ListItemNumbered";
    default: return "InvalidToken";
    }
}

#ifdef IM_RICHTEXT_ENABLE_PARSER_LOGS
#define DashedLine "-----------------------------------------"
const char* TabLine = "\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t";
#define LOG(FMT, ...)  std::fprintf(stdout, "%.*s" FMT, _currentStackPos+1, TabLine, __VA_ARGS__)
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

    struct AnimationData
    {
        std::vector<float> xoffsets;
        long long lastBlinkTime;
        long long lastMarqueeTime;
        bool isVisible = true;
    };

    struct RichTextData
    {
        ImVec2 specifiedBounds;
        ImVec2 computedBounds;
        RenderConfig* config = nullptr;
        std::string_view richText;
        float scale = 1.f;
        float fontScale = 1.f;
        uint32_t bgcolor;
        bool contentChanged = false;

        Drawables drawables;
        AnimationData animationData;
    };

    struct TooltipData
    {
        ImVec2 pos;
        std::string_view content;
    };

    struct BackgroundSpanData
    {
        std::pair<int, int> start{ -1, -1 };
        std::pair<int, int> end{ -1, -1 };
    };

    enum class TagType
    {
        Unknown,
        Bold, Italics, Underline, Strikethrough, Mark, Small, Font, Center,
        Span, List, ListItem, Paragraph, Header, RawText, Blockquote, Quotation, Abbr, CodeBlock, Hyperlink,
        Subscript, Superscript,
        Hr, LineBreak,
        Blink, Marquee,
        Meter
    };

    struct StackData
    {
        std::string_view tag;
        TagType tagType = TagType::Unknown;
        int styleIdx = -1;
        bool hasBackground = false;
    };

    struct BackgroundData
    {
        BackgroundSpanData span;
        BackgroundShape shape;
        int styleIdx = -1;
        bool isMultiline = true;
    };

    static std::unordered_map<std::size_t, RichTextData> RichTextMap;

#ifdef IM_RICHTEXT_TARGET_IMGUI
    static std::unordered_map<ImGuiContext*, std::deque<RenderConfig>> ImRenderConfigs;
#endif
#ifdef IM_RICHTEXT_TARGET_BLEND2D
    static std::unordered_map<BLContext*, std::deque<RenderConfig>> BLRenderConfigs;
#endif

    static const ListItemTokenDescriptor InvalidListItemToken{};
    static const TagPropertyDescriptor InvalidTagPropDesc{};

    // String representation of numbers, std::string_view is constructed from
    // these strings and used for <li> in <ol> lists
    static std::vector<std::string> NumbersAsStr;

#ifdef IM_RICHTEXT_TARGET_IMGUI
#ifdef _DEBUG
    static bool ShowOverlay = true;
    static bool ShowBoundingBox = true;
#else
    static const bool ShowOverlay = false;
    static const bool ShowBoundingBox = false;
#endif
#endif

    static const char* LineSpaces = "                                ";

    class DefaultTagVisitor final : public ITagVisitor
    {
    private:
        std::string_view _currTag;
        TagType _currTagType = TagType::Unknown;
        bool _currHasBackground = false;
        int _styleIdx = -1;
        int _currentStackPos = -1;
        int _currListDepth = -1, _currBlockquoteDepth = -1;
        int _currSubscriptLevel = 0, _currSuperscriptLevel = 0;
        float _maxWidth = 0.f;
        ImVec2 _bounds;

        const RenderConfig& _config;
        Drawables& _result;

        DrawableLine _currLine;
        StyleDescriptor _currStyle;
        TagPropertyDescriptor _currTagProps;
        BackgroundShape _currBgShape;

        StackData _tagStack[IM_RICHTEXT_MAXDEPTH];
        int _styleIndexStack[IM_RICHTEXT_MAXDEPTH] = { 0 };
        std::vector<BackgroundData> _backgroundSpans[IM_RICHTEXT_MAXDEPTH];

        int _listItemCountByDepths[IM_RICHTEXT_MAX_LISTDEPTH];
        BlockquoteDrawData _blockquoteStack[IM_RICHTEXT_MAXDEPTH];

        struct TokenPosition
        {
            int lineIdx = 0;
            int segmentIdx = 0;
            int tokenIdx = 0;
        };

        struct TokenPositionRemapping
        {
            TokenPosition oldIdx;
            TokenPosition newIdx;
        };

    public:

        DefaultTagVisitor(const RenderConfig& cfg, Drawables& res, ImVec2 bounds);

        void PushTag(std::string_view currTag, TagType tagType)
        {
            _currentStackPos++;
            _tagStack[_currentStackPos].tag = currTag;
            _tagStack[_currentStackPos].tagType = tagType;
        }

        void PopTag(bool reset)
        {
            if (reset) _tagStack[_currentStackPos] = StackData{};
            --_currentStackPos;
        }

        void AddToken(Token token, int propsChanged);
        SegmentData& AddSegment();
        void GenerateTextToken(std::string_view content);
        std::vector<TokenPositionRemapping> PerformWordWrap(int index);
        void AdjustBackgroundSpans(const std::vector<TokenPositionRemapping>& remapping);
        void AdjustForSuperSubscripts(const std::pair<int, int>& indexes);
        void ComputeLineBounds(const std::pair<int, int>& linesModified);
        void RecordBackgroundSpanEnd(bool isTagStart, bool segmentAdded);
        DrawableLine MoveToNextLine(bool isTagStart);

        ImVec2 GetSegmentSize(const SegmentData& segment) const;
        ImVec2 GetLineSize(const DrawableLine& line) const;

        float GetMaxSuperscriptOffset(const DrawableLine& line, float scale) const;
        float GetMaxSubscriptOffset(const DrawableLine& line, float scale) const;

        StyleDescriptor& Style(int stackpos);
        bool CreateNewStyle();
        void PopCurrentStyle();
        bool TagStart(std::string_view tag);
        bool Attribute(std::string_view name, std::optional<std::string_view> value);
        bool TagStartDone();
        bool Content(std::string_view content);
        bool TagEnd(std::string_view tag, bool selfTerminatingTag);
        void Finalize();

        void Error(std::string_view tag);
        bool IsSelfTerminating(std::string_view tag) const;
        bool IsPreformattedContent(std::string_view tag) const;
    };

    // ===============================================================
    // Section #1 : Implementation of style-related functions
    // ===============================================================

    template <typename T>
    static T Clamp(T val, T min, T max)
    {
        return val < min ? min : val > max ? max : val;
    }

    static int PopulateSegmentStyle(StyleDescriptor& style,
        const StyleDescriptor& parentStyle,
        BackgroundShape& shape,
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
        else if (AreSame(stylePropName, "text-wrap"))
        {
            if (AreSame(stylePropVal, "nowrap")) style.font.flags |= FontStyleNoWrap;
            prop = StyleTextWrap;
        }
        else if (AreSame(stylePropName, "background-color") || AreSame(stylePropName, "background"))
        {
            if (StartsWith(stylePropVal, "linear-gradient")) 
                shape.Gradient = ExtractLinearGradient(stylePropVal, config.NamedColor, config.UserData);
            else shape.Color = ExtractColor(stylePropVal, config.NamedColor, config.UserData);
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
            if (AreSame(stylePropVal, "normal")) 
            { 
                style.wbbhv = WordBreakBehavior::Normal; 
                style.wscbhv = WhitespaceCollapseBehavior::Collapse;
            }
            else if (AreSame(stylePropVal, "pre"))
            {
                style.wbbhv = WordBreakBehavior::Normal; 
                style.wscbhv = WhitespaceCollapseBehavior::Preserve;
                style.font.flags |= FontStyleNoWrap;
            }
            else if (AreSame(stylePropVal, "pre-wrap"))
            {
                style.wbbhv = WordBreakBehavior::Normal; 
                style.wscbhv = WhitespaceCollapseBehavior::Preserve;
                style.font.flags &= ~FontStyleNoWrap;
            }
            else if (AreSame(stylePropVal, "pre-line"))
            {
                style.wbbhv = WordBreakBehavior::Normal;
                style.wscbhv = WhitespaceCollapseBehavior::PreserveBreaks;
                style.font.flags &= ~FontStyleNoWrap;
            }

            prop = StyleWhitespace;
        }
        else if (AreSame(stylePropName, "text-overflow"))
        {
            if (AreSame(stylePropVal, "ellipsis"))
            {
                style.font.flags |= FontStyleOverflowEllipsis;
                prop = StyleTextOverflow;
            }
        }
        else if (AreSame(stylePropName, "word-break"))
        {
            if (AreSame(stylePropVal, "normal")) style.wbbhv = WordBreakBehavior::Normal;
            if (AreSame(stylePropVal, "break-all")) style.wbbhv = WordBreakBehavior::BreakAll;
            if (AreSame(stylePropVal, "keep-all")) style.wbbhv = WordBreakBehavior::KeepAll;
            if (AreSame(stylePropVal, "break-word")) style.wbbhv = WordBreakBehavior::BreakWord;
            prop = StyleWordBreak;
        }
        else if (AreSame(stylePropName, "white-space-collapse"))
        {
            if (AreSame(stylePropVal, "collapse")) style.wscbhv = WhitespaceCollapseBehavior::Collapse;
            if (AreSame(stylePropVal, "preserve")) style.wscbhv = WhitespaceCollapseBehavior::Preserve;
            if (AreSame(stylePropVal, "preserve-breaks")) style.wscbhv = WhitespaceCollapseBehavior::PreserveBreaks;
            if (AreSame(stylePropVal, "preserve-spaces")) style.wscbhv = WhitespaceCollapseBehavior::PreserveSpaces;
            if (AreSame(stylePropVal, "break-spaces")) style.wscbhv = WhitespaceCollapseBehavior::BreakSpaces;
            prop = StyleWhitespaceCollapse;
        }
        else if (AreSame(stylePropName, "border"))
        {
            shape.Border.top = shape.Border.bottom = shape.Border.left = shape.Border.right = ExtractBorder(stylePropVal,
                config.DefaultFontSize * config.FontScale, parentStyle.height, config.NamedColor, config.UserData);
            style.border.top = shape.Border.top.thickness;
            style.border.bottom = shape.Border.bottom.thickness;
            style.border.left = shape.Border.left.thickness;
            style.border.right = shape.Border.right.thickness;
            shape.Border.isUniform = true;
            prop = StyleBorder;
        }
        else if (AreSame(stylePropName, "border-top"))
        {
            shape.Border.top = ExtractBorder(stylePropVal, config.DefaultFontSize * config.FontScale,
                parentStyle.height, config.NamedColor, config.UserData);
            style.border.top = shape.Border.top.thickness;
            shape.Border.isUniform = false;
            prop = StyleBorder;
        }
        else if (AreSame(stylePropName, "border-left"))
        {
            shape.Border.left = ExtractBorder(stylePropVal, config.DefaultFontSize * config.FontScale,
                parentStyle.height, config.NamedColor, config.UserData);
            style.border.left = shape.Border.left.thickness;
            shape.Border.isUniform = false;
            prop = StyleBorder;
        }
        else if (AreSame(stylePropName, "border-right"))
        {
            shape.Border.right = ExtractBorder(stylePropVal, config.DefaultFontSize * config.FontScale,
                parentStyle.height, config.NamedColor, config.UserData);
            style.border.right = shape.Border.right.thickness;
            shape.Border.isUniform = false;
            prop = StyleBorder;
        }
        else if (AreSame(stylePropName, "border-bottom"))
        {
            shape.Border.bottom = ExtractBorder(stylePropVal, config.DefaultFontSize * config.FontScale,
                parentStyle.height, config.NamedColor, config.UserData);
            style.border.bottom = shape.Border.bottom.thickness;
            prop = StyleBorder;
            shape.Border.isUniform = false;
        }
        else if (AreSame(stylePropName, "border-radius"))
        {
            shape.Border.radius = ExtractFloatWithUnit(stylePropVal, 0.f, config.DefaultFontSize * config.FontScale,
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
        else if (AreSame(stylePropName, "box-shadow"))
        {
            shape.Shadow = ExtractBoxShadow(stylePropVal, config.DefaultFontSize, style.height, config.NamedColor,
                config.UserData);
            prop = StyleBoxShadow;
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

    static StyleDescriptor CreateDefaultStyle(const RenderConfig& config)
    {
        StyleDescriptor result;
        result.font.family = config.DefaultFontFamily;
        result.font.size = config.DefaultFontSize * config.FontScale;
        result.font.font = GetFont(result.font.family, result.font.size, FT_Normal, config.UserData);
        result.fgcolor = config.DefaultFgColor;
        result.list.itemStyle = config.ListItemBullet;
        return result;
    }

    static DrawableLine CreateNewLine(int)
    {
        DrawableLine line;
        line.BlockquoteDepth = -1;
        return line;
    }

    static float CalcVerticalOffset(int maxSuperscriptDepth, float baseFontSz, float scale)
    {
        float sum = 0.f, multiplier = scale;
        for (auto idx = 1; idx <= maxSuperscriptDepth; ++idx)
        {
            sum += multiplier;
            multiplier *= multiplier;
        }
        return sum * (baseFontSz * 0.5f);
    }

    static bool IsLineEmpty(const DrawableLine& line)
    {
        bool isEmpty = true;

        for (const auto& segment : line.Segments)
            isEmpty = isEmpty && segment.Tokens.empty();

        return isEmpty;
    }

    static void CreateElidedTextToken(DrawableLine& line, const StyleDescriptor& style, const RenderConfig& config, ImVec2 bounds)
    {
        auto width = bounds.x;
        width = (style.propsSpecified & StyleWidth) != 0 ? std::min(width, style.width) : width;
        auto sz = config.Renderer->EllipsisWidth(style.font.font);
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
                            auto revidx = (int)token.Content.size() - 1;

                            while (startx > width)
                            {
                                auto partial = token.Content.substr(revidx, 1);
                                startx -= config.Renderer->GetTextSize(partial, style.font.font).x;
                                token.VisibleTextSize -= (int16_t)1;
                                --revidx;
                            }

                            token.Type = TokenType::ElidedText;
                        }

                        break;
                    }
                }
            }
        }
    }

    static bool IsStyleSupported(TagType type)
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
        case TagType::Center:
            return false;
        default: return true;
        }
    }

    static std::pair<int, bool> RecordTagProperties(TagType tagType, std::string_view attribName, std::optional<std::string_view> attribValue,
        StyleDescriptor& style, BackgroundShape& bgshape, TagPropertyDescriptor& tagprops, const StyleDescriptor& parentStyle,
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
                        auto prop = PopulateSegmentStyle(style, parentStyle, bgshape, stylePropName,
                            stylePropVal.value(), config);
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
        else if (tagType == TagType::Font)
        {
            if (AreSame(attribName, "color") && attribValue.has_value())
            {
                style.fgcolor = ExtractColor(attribValue.value(), config.NamedColor, config.UserData);
                result = result | StyleFgColor;
            }
            else if (AreSame(attribName, "size") && attribValue.has_value())
            {
                style.font.size = ExtractFloatWithUnit(attribValue.value(), config.DefaultFontSize * config.FontScale,
                    config.DefaultFontSize * config.FontScale, parentStyle.height, config.Scale);
                result = result | StyleFontSize;
            }
            else if (AreSame(attribName, "face") && attribValue.has_value())
            {
                style.font.family = attribValue.value();
                result = result | StyleFontFamily;
            }
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

    static int CreateNextStyle(std::vector<StyleDescriptor>& styles)
    {
        auto& newstyle = styles.emplace_back(styles.back());
        return (int)styles.size() - 1;
    }

    static TagType GetTagType(std::string_view currTag, bool isStrictHTML5)
    {
        if (AreSame(currTag, "b") || AreSame(currTag, "strong")) return TagType::Bold;
        else if (AreSame(currTag, "i") || AreSame(currTag, "em") || AreSame(currTag, "cite") || AreSame(currTag, "var"))
            return TagType::Italics;
        else if (!isStrictHTML5 && AreSame(currTag, "font")) return TagType::Font;
        else if (AreSame(currTag, "hr")) return TagType::Hr;
        else if (AreSame(currTag, "br")) return TagType::LineBreak;
        else if (AreSame(currTag, "span")) return TagType::Span;
        else if (!isStrictHTML5 && AreSame(currTag, "center")) return TagType::Center;
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
        else if (!isStrictHTML5 && AreSame(currTag, "blink")) return TagType::Blink;
        else if (AreSame(currTag, "marquee")) return TagType::Marquee;
        else if (AreSame(currTag, "meter")) return TagType::Meter;
        return TagType::Unknown;
    }

    static void SetImplicitStyleProps(TagType tagType, std::string_view currTag,
        StyleDescriptor& style, const StyleDescriptor& parentStyle,
        BackgroundShape& shape, DrawableLine& line, const RenderConfig& config)
    {
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
            if (((style.propsSpecified & StyleWhitespace) == 0) && ((style.propsSpecified & StyleTextWrap) == 0))
                style.font.flags |= FontStyleNoWrap;
            if (((style.propsSpecified & StyleWhitespace) == 0) && ((style.propsSpecified & StyleWhitespaceCollapse) == 0))
                style.wscbhv = WhitespaceCollapseBehavior::Preserve;

            if (tagType == TagType::CodeBlock)
            {
                if ((style.propsSpecified & StyleBackground) == 0)
                    shape.Color = config.CodeBlockBg;
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
            if ((style.propsSpecified & StyleBackground) == 0)
                shape.Color = config.MarkHighlight;
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
        else if (tagType == TagType::Center)
        {
            style.alignment = TextAlignCenter;
            style.propsSpecified = StyleHAlignment | StyleVAlignment;
        }

        if (style.propsSpecified != NoStyleChange)
        {
            FontType fstyle = FT_Normal;
            if ((style.font.flags & FontStyleBold) != 0 &&
                (style.font.flags & FontStyleItalics) != 0) fstyle = FT_BoldItalics;
            else if ((style.font.flags & FontStyleBold) != 0) fstyle = FT_Bold;
            else if ((style.font.flags & FontStyleItalics) != 0) fstyle = FT_Italics;
            else if ((style.font.flags & FontStyleLight) != 0) fstyle = FT_Light;
            style.font.font = GetFont(style.font.family, style.font.size,
                fstyle, config.UserData);
        }
    }

    static bool CanContentBeMultiline(TagType type)
    {
        switch (type)
        {
        case ImRichText::TagType::Span: [[fallthrough]];
        case ImRichText::TagType::Subscript: [[fallthrough]];
        case ImRichText::TagType::Superscript: [[fallthrough]];
        case ImRichText::TagType::Hyperlink: [[fallthrough]];
        case ImRichText::TagType::Meter: [[fallthrough]];
        case ImRichText::TagType::Marquee: return false;
        default: return true;
        }
    }

    // ===============================================================
    // Section #2 : Implementation of drawing routines for drawables
    // ===============================================================

#if defined(_DEBUG) && defined(IM_RICHTEXT_TARGET_IMGUI)
    inline void DrawBoundingBox(DebugContentType type, ImVec2 startpos, ImVec2 endpos, const RenderConfig& config)
    {
        if (config.DebugContents[type] != IM_COL32_BLACK_TRANS && ShowBoundingBox)
            config.OverlayRenderer->DrawRect(startpos, endpos, config.DebugContents[type], false);
    }
#else
#define DrawBoundingBox(...)
#endif

    template <typename ItrT>
    static void DrawLinearGradient(ImVec2 initpos, ImVec2 endpos, float angle, ImGuiDir dir, ItrT start, ItrT end, const RenderConfig& config)
    {
        auto width = endpos.x - initpos.x;
        auto height = endpos.y - initpos.y;

        if (dir == ImGuiDir::ImGuiDir_Left)
        {
            // TODO: Add support for non-axis aligned gradients
            /*ImVec2 points[4];
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
            }*/

            for (auto it = start; it != end; ++it)
            {
                auto extent = width * it->pos;
                config.Renderer->DrawRectGradient(initpos, initpos + ImVec2{ extent, height },
                    it->from, it->to, it->to, it->from);
                initpos.x += extent;
            }
        }
        else if (dir == ImGuiDir::ImGuiDir_Down)
        {
            for (auto it = start; it != end; ++it)
            {
                auto extent = height * it->pos;
                config.Renderer->DrawRectGradient(initpos, initpos + ImVec2{ width, extent },
                    it->from, it->from, it->to, it->to);
                initpos.y += extent;
            }
        }
    }

    static void DrawBackground(ImVec2 startpos, ImVec2 endpos,
        const ColorGradient& gradient, uint32_t color, const RenderConfig& config)
    {
        if (gradient.totalStops != 0)
            (gradient.dir == ImGuiDir_Down || gradient.dir == ImGuiDir_Left) ?
            DrawLinearGradient(startpos, endpos, gradient.angleDegrees, gradient.dir,
                std::begin(gradient.colorStops), std::begin(gradient.colorStops) + gradient.totalStops, config) :
            DrawLinearGradient(startpos, endpos, gradient.angleDegrees, gradient.dir,
                std::rbegin(gradient.colorStops), std::rbegin(gradient.colorStops) + gradient.totalStops, config);
        else if (color != config.DefaultBgColor && color != IM_COL32_BLACK_TRANS)
            config.Renderer->DrawRect(startpos, endpos, color, true);
    }

#ifdef IM_RICHTEXT_TARGET_IMGUI

    std::tuple<int, int, int> DecomposeToRGBChannels(uint32_t color)
    {
        auto mask = (uint32_t)-1;
        return std::make_tuple((int)(color & (mask >> 24)),
            (int)(color & ((mask >> 16) & (mask << 8))) >> 8,
            (int)(color & ((mask >> 8) & (mask << 16))) >> 16);
    }

    bool DrawOverlay(ImVec2 startpos, ImVec2 endpos, const Token& token, 
        const StyleDescriptor& style, const BackgroundShape& bgshape, 
        const TagPropertyDescriptor& tagprops, const RenderConfig& config)
    {
        const auto& io = ImGui::GetCurrentContext()->IO;
        if (ImRect{ startpos, endpos }.Contains(io.MousePos) && ShowOverlay)
        {
            auto overlay = ImGui::GetForegroundDrawList();
            startpos.y = 0.f;

            char props[2048] = { 0 };
            auto currpos = 0;
            for (auto exp = 0; exp <= 21; ++exp)
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
                    case StyleBorder: currpos += std::snprintf(props + currpos, 2047 - currpos, "StyleBorder,"); break;
                    case StyleBorderRadius: currpos += std::snprintf(props + currpos, 2047 - currpos, "StyleBorderRadius,"); break;
                    case StyleBlink: currpos += std::snprintf(props + currpos, 2047 - currpos, "StyleBlink,"); break;
                    case StyleTextWrap: currpos += std::snprintf(props + currpos, 2047 - currpos, "StyleTextWrap,"); break;
                    default: break;
                    }
                }
            }

            constexpr int bufsz = 4096;
            char buffer[bufsz] = { 0 };
            auto yesorno = [](bool val) { return val ? "Yes" : "No"; };
            auto [fr, fg, fb] = DecomposeToRGBChannels(style.fgcolor);
            auto [br, bg, bb] = DecomposeToRGBChannels(bgshape.Color);

            currpos = std::snprintf(buffer, bufsz - 1, "Position            : (%.2f, %.2f)\n"
                "Bounds              : (%.2f, %.2f)\n",
                startpos.x, startpos.y, token.Bounds.width, token.Bounds.height);

            currpos += std::snprintf(buffer + currpos, bufsz - currpos,
                "\nProperties Specified: %s\nForeground Color    : (%d, %d, %d)\n",
                props, fr, fg, fb);

            if (style.backgroundIdx != -1)
            {
                if (bgshape.Gradient.totalStops == 0)
                    if (bgshape.Color != IM_COL32_BLACK_TRANS)
                        currpos += std::snprintf(buffer + currpos, bufsz - currpos, "Background Color    : (%d, %d, %d)\n", br, bg, bb);
                    else
                        currpos += std::snprintf(buffer + currpos, bufsz - currpos, "Background Color    : Transparent\n");
                else
                {
                    currpos += std::snprintf(buffer + currpos, bufsz - currpos, "Linear Gradient     :");

                    for (auto idx = 0; idx < bgshape.Gradient.totalStops; ++idx)
                    {
                        auto [r1, g1, b1] = DecomposeToRGBChannels(bgshape.Gradient.colorStops[idx].from);
                        auto [r2, g2, b2] = DecomposeToRGBChannels(bgshape.Gradient.colorStops[idx].to);
                        currpos += std::snprintf(buffer + currpos, bufsz - currpos, "From (%d, %d, %d) To (%d, %d, %d) at %.2f\n",
                            r1, g1, b1, r2, g2, b2, bgshape.Gradient.colorStops[idx].pos);
                    }
                }

                int br = 0, bg = 0, bb = 0;
                std::tie(br, bg, bb) = DecomposeToRGBChannels(bgshape.Border.top.color);
                currpos += std::snprintf(buffer + currpos, bufsz - currpos,
                    "Border.top          : (%.2fpx, rgb(%d, %d, %d))\n",
                    bgshape.Border.top.thickness, br, bg, bb);

                std::tie(br, bg, bb) = DecomposeToRGBChannels(bgshape.Border.right.color);
                currpos += std::snprintf(buffer + currpos, bufsz - currpos,
                    "Border.right        : (%.2fpx, rgb(%d, %d, %d))\n",
                    bgshape.Border.right.thickness, br, bg, bb);

                std::tie(br, bg, bb) = DecomposeToRGBChannels(bgshape.Border.bottom.color);
                currpos += std::snprintf(buffer + currpos, bufsz - currpos,
                    "Border.bottom       : (%.2fpx, rgb(%d, %d, %d))\n",
                    bgshape.Border.bottom.thickness, br, bg, bb);

                std::tie(br, bg, bb) = DecomposeToRGBChannels(bgshape.Border.left.color);
                currpos += std::snprintf(buffer + currpos, bufsz - currpos,
                    "Border.left         : (%.2fpx, rgb(%d, %d, %d))\n",
                    bgshape.Border.left.thickness, br, bg, bb);
            }

            currpos += std::snprintf(buffer + currpos, bufsz - currpos,
                "\nHeight              : %.2fpx\nWidth               : %.2fpx\n"
                "Tooltip               : %.*s\nLink                : %.*s\n"
                "Blink                 : %s\n",
                style.width, style.height, (int)tagprops.tooltip.size(), tagprops.tooltip.data(),
                (int)tagprops.link.size(), tagprops.link.data(), yesorno(style.blink));

            currpos += std::snprintf(buffer + currpos, bufsz - currpos,
                "Padding             : (%.2fpx, %.2fpx, %.2fpx, %.2fpx)\n",
                style.padding.top, style.padding.right, style.padding.bottom, style.padding.left);

            if (token.Type == TokenType::Text || token.Type == TokenType::ElidedText)
            {
                currpos += std::snprintf(buffer + currpos, bufsz - currpos, "\n\nFont.family         : %.*s\n"
                    "Font.size           : %.2fpx\nFont.bold           : %s\nFont.italics        : %s\n"
                    "Font.underline      : %s\n"
                    "Font.strike         : %s\n"
                    "Font.wrap           : %s", (int)style.font.family.size(), style.font.family.data(), style.font.size,
                    yesorno(style.font.flags & FontStyleBold), yesorno(style.font.flags & FontStyleItalics), 
                    yesorno(style.font.flags & FontStyleUnderline), yesorno(style.font.flags & FontStyleStrikethrough),
                    yesorno(!(style.font.flags & FontStyleNoWrap)));
            }
            else if (token.Type == TokenType::Meter)
            {
                currpos += std::snprintf(buffer + currpos, bufsz - currpos, "\n\nRange               : "
                    "(%.2f, %.2f)\nValue          : %.2f",
                    tagprops.range.first, tagprops.range.second, tagprops.value);
            }

            auto font = (ImFont*)GetOverlayFont(config);
            ImGui::PushFont(font);
            auto sz = ImGui::CalcTextSize(buffer, buffer + currpos, false, 300.f);
            sz.x += 20.f;

            startpos.x = ImGui::GetCurrentWindow()->Size.x - sz.x;
            overlay->AddRectFilled(startpos, startpos + ImVec2{ ImGui::GetCurrentWindow()->Size.x, sz.y }, IM_COL32_WHITE);
            overlay->AddText(font, font->FontSize, startpos, IM_COL32_BLACK, buffer, NULL, 300.f);
            ImGui::PopFont();
            return true;
        }

        return false;
    }
#endif

    static bool DrawToken(int lineidx, const Token& token, ImVec2 initpos,
        ImVec2 bounds, const StyleDescriptor& style, const TagPropertyDescriptor& tagprops, 
        const BackgroundShape& bgshape, const ListItemTokenDescriptor& listItem, 
        const RenderConfig& config, TooltipData& tooltip, AnimationData& animation)
    {
        auto startpos = token.Bounds.start(initpos) + ImVec2{ token.Offset.left, token.Offset.top };
        auto endpos = token.Bounds.end(initpos);

        if ((style.blink && animation.isVisible) || !style.blink)
        {
            if (token.Type == TokenType::HorizontalRule)
            {
                config.Renderer->DrawRect(startpos, endpos, style.fgcolor, true);
            }
            else if (token.Type == TokenType::ListItemBullet)
            {
                auto bulletscale = Clamp(config.BulletSizeScale, 1.f, 4.f);
                auto bulletsz = (style.font.size) / bulletscale;

                if (style.list.itemStyle == BulletType::Custom)
                    config.Renderer->DrawBullet(startpos, endpos, style.fgcolor, listItem.ListItemIndex, listItem.ListDepth);
                else config.Renderer->DrawDefaultBullet(style.list.itemStyle, initpos, token.Bounds, style.fgcolor, bulletsz);
            }
            else if (token.Type == TokenType::ListItemNumbered)
            {
                config.Renderer->DrawText(listItem.NestedListItemIndex, startpos, style.fgcolor);
            }
            else if (token.Type == TokenType::Meter)
            {
                auto border = ImVec2{ 1.f, 1.f };
                auto borderRadius = (endpos.y - startpos.y) * 0.5f;
                auto diff = tagprops.range.second - tagprops.range.first;
                auto progress = (tagprops.value / diff) * token.Bounds.width;

                config.Renderer->DrawRect(startpos, endpos, config.MeterBgColor, true, 1.f, borderRadius, AllCorners);
                config.Renderer->DrawRect(startpos, endpos, config.MeterBorderColor, false, 1.f, borderRadius, AllCorners);
                config.Renderer->DrawRect(startpos + border, startpos - border + ImVec2{ progress, token.Bounds.height },
                    config.MeterFgColor, true, 1.f, borderRadius, TopLeftCorner | BottomLeftCorner);
            }
            else
            {
                auto textend = token.Content.data() + token.VisibleTextSize;
                auto halfh = token.Bounds.height * 0.5f;
                config.Renderer->DrawText(token.Content, startpos, style.fgcolor);

                if (token.Type == TokenType::ElidedText)
                {
                    auto ewidth = config.Renderer->EllipsisWidth(style.font.font);
                    config.Renderer->DrawText("...", ImVec2{ startpos.x + token.Bounds.width - ewidth, startpos.y }, style.fgcolor);
                }

                if (style.font.flags & FontStyleStrikethrough) config.Renderer->DrawLine(startpos + ImVec2{ 0.f, halfh }, endpos + ImVec2{ 0.f, -halfh }, style.fgcolor);
                if (style.font.flags & FontStyleUnderline) config.Renderer->DrawLine(startpos + ImVec2{ 0.f, token.Bounds.height }, endpos, style.fgcolor);

                if (!tagprops.tooltip.empty())
                {
                    if (!(style.font.flags & FontStyleUnderline))
                    {
                        // TODO: Refactor this out
                        auto posx = startpos.x;
                        while (posx < endpos.x)
                        {
                            config.Renderer->DrawCircle(ImVec2{ posx, endpos.y }, 1.f, style.fgcolor, true);
                            posx += 3.f;
                        }
                    }

                    auto mousepos = config.Platform->GetCurrentMousePos();
                    if (ImRect{ startpos, endpos }.Contains(mousepos))
                    {
                        tooltip.pos = mousepos;
                        tooltip.content = tagprops.tooltip;
                    }
                }
                else if (!tagprops.link.empty() && (config.Platform != nullptr))
                { 
                    auto pos = config.Platform->GetCurrentMousePos();
                    if (ImRect{ startpos, endpos }.Contains(pos))
                    {
                        config.Platform->HandleHover(true);
                        if (config.Platform->IsMouseClicked())
                            config.Platform->HandleHyperlink(tagprops.link);
                    }
                    else
                        config.Platform->HandleHover(false);
                }
            }
        }

#ifdef IM_RICHTEXT_TARGET_IMGUI
        if (DrawOverlay(startpos, endpos, token, style, bgshape, tagprops, config))
#endif
            DrawBoundingBox(ContentTypeToken, startpos, endpos, config);
        if ((token.Bounds.left + token.Bounds.width) > (bounds.x + initpos.x)) return false;
        return true;
    }

    static void DrawBorderRect(const FourSidedBorder& border, ImVec2 startpos, ImVec2 endpos, bool isUniform,
        uint32_t bgcolor, const RenderConfig& config)
    {
        if (isUniform && border.top.thickness > 0.f && border.top.color != bgcolor)
        {
            config.Renderer->DrawRect(startpos, endpos, border.top.color, false, border.top.thickness,
                border.radius, border.rounding);
        }
        else
        {
            auto width = endpos.x - startpos.x, height = endpos.y - startpos.y;

            if (border.top.thickness > 0.f && border.top.color != bgcolor)
                config.Renderer->DrawLine(startpos, startpos + ImVec2{ width, 0.f }, border.top.color, border.top.thickness);
            if (border.right.thickness > 0.f && border.right.color != bgcolor)
                config.Renderer->DrawLine(startpos + ImVec2{ width - border.right.thickness, 0.f }, endpos -
                    ImVec2{ border.right.thickness, 0.f }, border.right.color, border.right.thickness);
            if (border.left.thickness > 0.f && border.left.color != bgcolor)
                config.Renderer->DrawLine(startpos, startpos + ImVec2{ 0.f, height }, border.left.color, border.left.thickness);
            if (border.bottom.thickness > 0.f && border.bottom.color != bgcolor)
                config.Renderer->DrawLine(startpos + ImVec2{ 0.f, height - border.bottom.thickness }, endpos -
                    ImVec2{ 0.f, border.bottom.thickness }, border.bottom.color, border.bottom.thickness);
        }
    }

    static bool DrawSegment(int lineidx, const SegmentData& segment,
        ImVec2 initpos, ImVec2 bounds, const Drawables& result, 
        const RenderConfig& config, TooltipData& tooltip, AnimationData& animation)
    {
        static const BackgroundShape InvalidShape{};
        if (segment.Tokens.empty()) return true;
        const auto& style = result.StyleDescriptors[segment.StyleIdx + 1];
        auto popFont = false;

        if (style.font.font != nullptr)
        {
            popFont = config.Renderer->SetCurrentFont(style.font.font);
        }

        auto drawTokens = true;
        auto startpos = segment.Bounds.start(initpos), endpos = segment.Bounds.end(initpos);
        auto isMeter = (segment.Tokens.size() == 1u &&
            (segment.Tokens.front().Type == TokenType::Meter));
        auto tokenidx = 0;

        for (const auto& token : segment.Tokens)
        {
            const auto& listItem = token.ListPropsIdx == -1 ? InvalidListItemToken :
                result.ListItemTokens[token.ListPropsIdx];
            const auto& tagprops = token.PropertiesIdx == -1 ? InvalidTagPropDesc :
                result.TagDescriptors[token.PropertiesIdx];
            const auto& bgshape = style.backgroundIdx == -1 ? InvalidShape : 
                result.BackgroundShapes[segment.Depths[tokenidx]][style.backgroundIdx];
            if (drawTokens && !DrawToken(lineidx, token, initpos, bounds, style,
                tagprops, bgshape, listItem, config, tooltip, animation))
            {
                drawTokens = false; 
                break;
            }
        }

        DrawBoundingBox(ContentTypeSegment, startpos, endpos, config);
        if (popFont) config.Renderer->ResetFont();
        return drawTokens;
    }

    static void DrawForegroundLayer(ImVec2 initpos, ImVec2 bounds,
        const std::vector<DrawableLine>& lines, const Drawables& result,
        const RenderConfig& config, TooltipData& tooltip, AnimationData& animation)
    {
        for (auto lineidx = 0; lineidx < (int)lines.size(); ++lineidx)
        {
            if (lines[lineidx].Segments.empty()) continue;

            for (const auto& segment : lines[lineidx].Segments)
            {
                auto linestart = initpos;
                if (lines[lineidx].Marquee) linestart.x += animation.xoffsets[lineidx];
                if (!DrawSegment(lineidx, segment, linestart, bounds, result, config, tooltip, animation))
                    break;
            }
            
#ifdef _DEBUG
            auto linestart = lines[lineidx].Content.start(initpos) + ImVec2{ lines[lineidx].Offset.left, lines[lineidx].Offset.top };
            auto lineend = lines[lineidx].Content.end(initpos);
            DrawBoundingBox(ContentTypeLine, linestart, lineend, config);
#endif
            if ((lines[lineidx].Content.top + lines[lineidx].height()) > (bounds.y + initpos.y)) break;
        }
    }

    static void DrawBackgroundLayer(ImVec2 initpos, ImVec2 bounds,
        const std::vector<BackgroundShape>* shapes, const RenderConfig& config)
    {
        for (auto depth = 0; depth < IM_RICHTEXT_MAXDEPTH; ++depth)
        {
            for (const auto& shape : shapes[depth])
            {
                auto startpos = shape.Start + initpos;
                auto endpos = shape.End + initpos;

                DrawBackground(startpos, endpos, shape.Gradient, shape.Color, config);
                DrawBoundingBox(ContentTypeBg, startpos, endpos, config);
                DrawBorderRect(shape.Border, startpos, endpos, shape.Border.isUniform, shape.Color, config);
                if (shape.End.y > (bounds.y + initpos.y)) break;
            }
        }
    }

    static void DrawImpl(AnimationData& animation, const Drawables& drawables, ImVec2 pos, ImVec2 bounds, RenderConfig* config)
    {
        using namespace std::chrono;

#if defined(_DEBUG) && defined(IM_RICHTEXT_TARGET_IMGUI)
        ImRichText::ImGuiRenderer overlay{ *config };
        config->OverlayRenderer = &overlay;
        config->OverlayRenderer->UserData = ImGui::GetForegroundDrawList();
#endif

        auto endpos = pos + bounds;
        TooltipData tooltip;

        if (animation.xoffsets.empty())
        {
            animation.xoffsets.resize(drawables.ForegroundLines.size());
            std::fill(animation.xoffsets.begin(), animation.xoffsets.end(), 0.f);
        }

        auto currFrameTime = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();

        config->Renderer->SetClipRect(pos, endpos);
        config->Renderer->DrawRect(pos, endpos, config->DefaultBgColor, true);

        DrawBackgroundLayer(pos, bounds, drawables.BackgroundShapes, *config);
        DrawForegroundLayer(pos, bounds, drawables.ForegroundLines, drawables, *config, tooltip, animation);
        config->Renderer->DrawTooltip(tooltip.pos, tooltip.content);

        if (config->Platform != nullptr)
        {
            if (!config->IsStrictHTML5 && (currFrameTime - animation.lastBlinkTime > IM_RICHTEXT_BLINK_ANIMATION_INTERVAL))
            {
                animation.isVisible = !animation.isVisible;
                animation.lastBlinkTime = currFrameTime;
                config->Platform->RequestFrame();
            }

            if (currFrameTime - animation.lastMarqueeTime > IM_RICHTEXT_MARQUEE_ANIMATION_INTERVAL)
            {
                for (auto lineidx = 0; lineidx < (int)animation.xoffsets.size(); ++lineidx)
                {
                    animation.xoffsets[lineidx] += 1.f;
                    auto linewidth = drawables.ForegroundLines[lineidx].Content.width;

                    if (animation.xoffsets[lineidx] >= linewidth)
                        animation.xoffsets[lineidx] = -linewidth;
                }

                config->Platform->RequestFrame();
                animation.lastMarqueeTime = currFrameTime;
            }
        }

        config->Renderer->ResetClipRect();
    }

    // ===============================================================
    // Section #3 : Implementation of `DefaultTagVisitor` member functions
    // ===============================================================

    static bool operator!=(const TagPropertyDescriptor& lhs, const TagPropertyDescriptor& rhs)
    {
        return lhs.tooltip != rhs.tooltip || lhs.link != rhs.link || lhs.value != rhs.value ||
            lhs.range != rhs.range;
    }

    DefaultTagVisitor::DefaultTagVisitor(const RenderConfig& cfg, Drawables& res, ImVec2 bounds)
        : _bounds{ bounds }, _config{ cfg }, _result{ res }
    {
        _currentStackPos = -1;
        std::memset(_listItemCountByDepths, 0, IM_RICHTEXT_MAX_LISTDEPTH);
        std::memset(_styleIndexStack, -2, IM_RICHTEXT_MAXDEPTH);
        _result.StyleDescriptors.emplace_back(CreateDefaultStyle(_config));
        _currStyle = _result.StyleDescriptors.front();
        _maxWidth = _bounds.x;
    }

    void DefaultTagVisitor::AddToken(Token token, int propsChanged)
    {
        auto& segment = _currLine.Segments.back();
        const auto& style = _result.StyleDescriptors[segment.StyleIdx + 1];

        if (token.Type == TokenType::Text)
        {
            auto sz = _config.Renderer->GetTextSize(token.Content, style.font.font);
            token.VisibleTextSize = (int16_t)token.Content.size();
            token.Bounds.width = sz.x;
            token.Bounds.height = sz.y;
        }
        else if (token.Type == TokenType::HorizontalRule)
        {
            token.Bounds.width = _bounds.x - _currLine.Content.left - _currLine.Offset.h() -
                style.padding.h();
            token.Bounds.height = style.height;
        }
        else if (token.Type == TokenType::ListItemBullet)
        {
            auto bulletscale = Clamp(_config.BulletSizeScale, 1.f, 4.f);
            auto bulletsz = (style.font.size) / bulletscale;
            token.Bounds.width = token.Bounds.height = bulletsz;
            token.Offset.right = _config.ListItemOffset;
        }
        else if (token.Type == TokenType::ListItemNumbered)
        {
            if (NumbersAsStr.empty())
            {
                NumbersAsStr.reserve(IM_RICHTEXT_MAX_LISTITEM);

                for (auto num = 1; num <= IM_RICHTEXT_MAX_LISTITEM; ++num)
                    NumbersAsStr.emplace_back(std::to_string(num));
            }

            auto& listItem = _result.ListItemTokens[token.ListPropsIdx];
            std::memset(listItem.NestedListItemIndex, 0, IM_RICHTEXT_NESTED_ITEMCOUNT_STRSZ);
            auto currbuf = 0;

            for (auto depth = 0; depth <= listItem.ListDepth && currbuf < IM_RICHTEXT_NESTED_ITEMCOUNT_STRSZ; ++depth)
            {
                auto itemcount = _listItemCountByDepths[depth] - 1;
                auto itemlen = itemcount > 99 ? 3 : itemcount > 9 ? 2 : 1;
                std::memcpy(listItem.NestedListItemIndex + currbuf, NumbersAsStr[itemcount].data(), itemlen);
                currbuf += itemlen;

                listItem.NestedListItemIndex[currbuf] = '.';
                currbuf += 1;
            }

            std::string_view input{ listItem.NestedListItemIndex, (size_t)currbuf };
            auto sz = _config.Renderer->GetTextSize(input, style.font.font);
            token.Bounds.width = sz.x;
            token.Bounds.height = sz.y;
        }
        else if (token.Type == TokenType::Meter)
        {
            if ((propsChanged & StyleWidth) == 0) token.Bounds.width = _config.MeterDefaultSize.x;
            if ((propsChanged & StyleHeight) == 0) token.Bounds.height = _config.MeterDefaultSize.y;
        }

        segment.Tokens.emplace_back(token);
        segment.Depths.emplace_back(_currentStackPos);
        segment.HasText = segment.HasText || (!token.Content.empty());
        segment.Bounds.width += token.Bounds.width;
        segment.Bounds.height = std::max(token.Bounds.height, segment.Bounds.height);
        _currLine.HasText = _currLine.HasText || segment.HasText;
        _currLine.HasSubscript = _currLine.HasSubscript || segment.SubscriptDepth > 0;
        _currLine.HasSuperscript = _currLine.HasSuperscript || segment.SuperscriptDepth > 0;

        LOG("Added token: %.*s [itemtype: %s][font-size: %f][size: (%f, %f)]\n",
            (int)token.Content.size(), token.Content.data(),
            GetTokenTypeString(token), style.font.size,
            token.Bounds.width, token.Bounds.height);
    }

    SegmentData& DefaultTagVisitor::AddSegment()
    {
        if (!_currLine.Segments.empty())
        {
            auto& lastSegment = _currLine.Segments.back();
            auto sz = GetSegmentSize(lastSegment);
            lastSegment.Bounds.width = sz.x;
            lastSegment.Bounds.height = sz.y;
        }

        auto& segment = _currLine.Segments.emplace_back();
        segment.StyleIdx = _styleIdx;
        return segment;
    }

    void DefaultTagVisitor::GenerateTextToken(std::string_view content)
    {
        Token token;
        token.Content = content;
        AddToken(token, NoStyleChange);
    }

    std::vector<DefaultTagVisitor::TokenPositionRemapping> DefaultTagVisitor::PerformWordWrap(int index)
    {
        LOG("Performing word wrap on line #%d", index);
        std::vector<DefaultTagVisitor::TokenPositionRemapping> result;
        auto& lines = _result.ForegroundLines;

        if (!lines[index].HasText || !_config.WordWrap || (_bounds.x <= 0.f))
        {
            return result;
        }

        std::vector<DrawableLine> newlines;
        std::vector<int> styleIndexes;
        std::vector<std::string_view> words;
        std::vector<std::pair<int, int>> tokenIndexes;

        auto currline = CreateNewLine(-1);
        currline.Segments.emplace_back();
        auto currentx = 0.f;
        auto availwidth = _bounds.x;
        auto segmentIdx = 0;

        for (auto& segment : lines[index].Segments)
        {
            auto tokenIdx = 0;

            for (auto& token : segment.Tokens)
            {
                if (token.Type == TokenType::Text)
                {
                    styleIndexes.push_back(segment.StyleIdx);
                    tokenIndexes.emplace_back(segmentIdx, tokenIdx);
                    words.push_back(token.Content);
                    ++tokenIdx;
                }
            }

            ++segmentIdx;
        }

        struct UserData
        {
            const std::vector<StyleDescriptor>& styles;
            const std::vector<int>& styleIndexes;
            std::vector<std::pair<int, int>>& tokenIndexes;
            std::vector<DrawableLine>& newlines;
            std::vector<DefaultTagVisitor::TokenPositionRemapping>& result;
            DrawableLine& currline;
            DrawableLine& targetline;
            int index;
        };

        UserData data{ _result.StyleDescriptors, styleIndexes, tokenIndexes, newlines, 
            result, currline, lines[index], index };

        _config.TextShaper->ShapeText(availwidth, { words.begin(), words.end() },
            [](int wordIdx, void* userdata) {
                const auto& data = *reinterpret_cast<UserData*>(userdata);
                const auto& style = data.styles[data.styleIndexes[wordIdx] + 1];
                return ITextShaper::WordProperty{ style.font.font, style.wbbhv };
            },
            [](int wordIdx, void* userdata) {
                const auto& data = *reinterpret_cast<UserData*>(userdata);
                data.newlines.push_back(data.currline);

                data.currline = CreateNewLine(-1);
                data.currline.Segments.emplace_back().StyleIdx = data.styleIndexes[wordIdx];
            },
            [](int wordIdx, std::string_view word, ImVec2 dim, void* userdata) {
                const auto& data = *reinterpret_cast<UserData*>(userdata);
                auto tidx = data.tokenIndexes[wordIdx];

                if ((wordIdx > 0) && (data.styleIndexes[wordIdx - 1] != data.styleIndexes[wordIdx]))
                    data.currline.Segments.emplace_back().StyleIdx = data.styleIndexes[wordIdx];

                const auto& token = data.targetline.Segments[tidx.first].Tokens[tidx.second];
                auto& ntk = data.currline.Segments.back().Tokens.emplace_back(token);
                ntk.VisibleTextSize = (int16_t)(word.size());
                ntk.Content = word;
                ntk.Bounds.width = dim.x;
                ntk.Bounds.height = dim.y;

                auto& remap = data.result.emplace_back();
                remap.oldIdx.lineIdx = data.index;
                remap.oldIdx.segmentIdx = tidx.first;
                remap.oldIdx.tokenIdx = tidx.second;
                remap.newIdx.lineIdx = (int)data.newlines.size() + data.index;
                remap.newIdx.segmentIdx = (int)data.currline.Segments.size() - 1;
                remap.newIdx.tokenIdx = (int)data.currline.Segments.back().Tokens.size() - 1;
            },
            _config, &data);

        newlines.push_back(currline);
        auto it = lines.erase(lines.begin() + index);
        auto sz = (int)lines.size();
        lines.insert(it, newlines.begin(), newlines.end());
        return result;
    }

    void DefaultTagVisitor::AdjustBackgroundSpans(const std::vector<TokenPositionRemapping>& remapping)
    {
        for (auto depth = _currentStackPos + 1; depth < IM_RICHTEXT_MAXDEPTH; ++depth)
        {
            for (auto bidx = 0; bidx < (int)_backgroundSpans[depth].size(); ++bidx)
            {
                auto& bg = _backgroundSpans[depth][bidx];

                for (auto idx = 0; idx < (int)remapping.size(); ++idx)
                {
                    const auto& from = remapping[idx].oldIdx;
                    const auto& to = remapping[idx].newIdx;

                    if (bg.span.start == std::make_pair(from.lineIdx, from.segmentIdx))
                    {
                        bg.span.start = std::make_pair(to.lineIdx, to.segmentIdx);
                        auto currsegment = from.segmentIdx;
                        auto currline = from.lineIdx;
                        while (currsegment == remapping[idx].oldIdx.segmentIdx &&
                            currline == remapping[idx].oldIdx.lineIdx) idx++;
                        idx--;
                    }
                    else if (bg.span.end == std::make_pair(from.lineIdx, from.segmentIdx))
                    {
                        bg.span.end = std::make_pair(std::min(to.lineIdx, from.lineIdx), to.segmentIdx);

                        if (to.lineIdx > from.lineIdx)
                        {
                            for (auto line = from.lineIdx + 1; line <= to.lineIdx; ++line)
                            {
                                auto& newbg = _backgroundSpans[_currentStackPos + 1].emplace_back(bg);
                                newbg.span.start = std::make_pair(line, 0);
                                newbg.span.end = std::make_pair(line, line == to.lineIdx ? to.segmentIdx :
                                    (int)_result.ForegroundLines[line].Segments.size() - 1);
                            }
                        }
                    }
                }
            }
        }
    }

    void DefaultTagVisitor::AdjustForSuperSubscripts(const std::pair<int, int>& indexes)
    {
        auto& lines = _result.ForegroundLines;

        for (auto idx = indexes.first; idx < (indexes.first + indexes.second); ++idx)
        {
            auto& line = lines[idx];
            if (!line.HasSubscript && !line.HasSuperscript) continue;

            auto maxTopOffset = GetMaxSuperscriptOffset(line, _config.ScaleSuperscript);
            auto maxBottomOffset = GetMaxSubscriptOffset(line, _config.ScaleSubscript);
            auto lastFontSz = _config.DefaultFontSize * _config.FontScale;
            auto lastSuperscriptDepth = 0, lastSubscriptDepth = 0;

            for (auto& segment : line.Segments)
            {
                auto& style = _result.StyleDescriptors[segment.StyleIdx + 1];

                if (segment.SuperscriptDepth > lastSuperscriptDepth)
                {
                    style.font.size = lastFontSz * _config.ScaleSuperscript;
                    maxTopOffset -= style.font.size * 0.5f;
                }
                else if (segment.SuperscriptDepth < lastSuperscriptDepth)
                {
                    maxTopOffset += lastFontSz * 0.5f;
                    style.font.size = lastFontSz / _config.ScaleSuperscript;
                }

                if (segment.SubscriptDepth > lastSubscriptDepth)
                {
                    style.font.size = lastFontSz * _config.ScaleSubscript;
                    maxBottomOffset += (lastFontSz - style.font.size * 0.5f);
                }
                else if (segment.SubscriptDepth < lastSubscriptDepth)
                {
                    style.font.size = lastFontSz / _config.ScaleSubscript;
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

    void DefaultTagVisitor::ComputeLineBounds(const std::pair<int, int>& linesModified)
    {
        auto& result = _result.ForegroundLines;

        for (auto index = linesModified.first; index < (linesModified.first + linesModified.second); ++index)
        {
            auto& line = result[index];
            auto currx = line.Content.left + line.Offset.left;
            auto sz = GetLineSize(line);
            line.Content.width = sz.x;
            line.Content.height = sz.y;

            if (index > 0) line.Content.top = result[index - 1].Content.top + result[index - 1].height() + _config.LineGap;

            for (auto& segment : line.Segments)
            {
                if (segment.Tokens.empty()) continue;

                // This will align the segment as vertical centre, TODO: Handle other alignments
                segment.Bounds.top = line.Content.top + line.Offset.top + ((line.Content.height - segment.height()) * 0.5f);
                segment.Bounds.left = currx; // This will align left, TODO: Handle other alignments
                const auto& style = _result.StyleDescriptors[segment.StyleIdx + 1];

                currx += style.padding.left + style.border.left;

                for (auto tokidx = 0; tokidx < (int)segment.Tokens.size(); ++tokidx)
                {
                    auto& token = segment.Tokens[tokidx];
                    token.Bounds.top = segment.Bounds.top + style.padding.top +
                        style.superscriptOffset + style.subscriptOffset + style.border.top +
                        ((segment.Bounds.height - token.Bounds.height) * 0.5f);

                    // TODO: Fix bullet positioning w.r.t. first text block (baseline aligned?)
                    /*if ((token.Type == TokenType::ListItemBullet) && ((tokidx + 1) < (int)segment.Tokens.size()))
                         segment.Tokens[tokidx + 1]*/
                    token.Bounds.left = currx + token.Offset.left;
                    currx += token.Bounds.width + token.Offset.h();
                }

                currx += style.padding.right + style.border.right;
            }

            HIGHLIGHT("\nCreated line #%d at (%f, %f) of size (%f, %f) with %d segments", index,
                line.Content.left, line.Content.top, line.Content.width, line.Content.height,
                (int)line.Segments.size());
        }
    }

    void DefaultTagVisitor::RecordBackgroundSpanEnd(bool isTagStart, bool segmentAdded)
    {
        if (!isTagStart && !_backgroundSpans[_currentStackPos + 1].empty()
            && _backgroundSpans[_currentStackPos + 1].back().span.end.first == -1)
        {
            auto& lastBgSpan = _backgroundSpans[_currentStackPos + 1].back();
            lastBgSpan.span.end.first = std::max((int)_result.ForegroundLines.size() - 1,
                lastBgSpan.span.start.first);
            lastBgSpan.span.end.second = std::max(0, (int)_currLine.Segments.size() - (segmentAdded ? 2 : 1));
        }
    }

    DrawableLine DefaultTagVisitor::MoveToNextLine(bool isTagStart)
    {
        auto isEmpty = IsLineEmpty(_currLine);
        std::pair<int, int> linesModified;
        _result.ForegroundLines.emplace_back(_currLine);

        if (_currLine.Segments.size() == 1u && _currLine.Segments.front().Tokens.size() == 1u &&
            _currLine.Segments.front().Tokens.front().Type == TokenType::HorizontalRule)
        {
            linesModified = std::make_pair((int)_result.ForegroundLines.size() - 1, 1);
        }
        else
        {
            linesModified = std::make_pair((int)_result.ForegroundLines.size() - 1, 1);
            ComputeLineBounds(linesModified);
            RecordBackgroundSpanEnd(isTagStart, false);

            if (!_currLine.Marquee && _bounds.x > 0.f && (_result.StyleDescriptors[_styleIdx + 1].font.flags & FontStyleNoWrap) == 0 &&
                _result.ForegroundLines.back().width() > _bounds.x)
            {
                auto remapping = PerformWordWrap((int)_result.ForegroundLines.size() - 1);
                AdjustBackgroundSpans(remapping);
            }

            linesModified = std::make_pair(linesModified.first, (int)_result.ForegroundLines.size() - linesModified.first);
            AdjustForSuperSubscripts(linesModified);
        }

        auto& lastline = _result.ForegroundLines.back();
        auto newline = CreateNewLine(_styleIdx);
        newline.BlockquoteDepth = _currBlockquoteDepth;
        if (isTagStart) newline.Marquee = _currTagType == TagType::Marquee;

        if (_currBlockquoteDepth > 0) newline.Offset.left = newline.Offset.right = _config.BlockquotePadding;
        if (_currBlockquoteDepth > lastline.BlockquoteDepth) newline.Offset.top = _config.BlockquotePadding;
        else if (_currBlockquoteDepth < lastline.BlockquoteDepth) lastline.Offset.bottom = _config.BlockquotePadding;

        ComputeLineBounds(linesModified);
        const auto& style = _result.StyleDescriptors[_styleIdx + 1];
        CreateElidedTextToken(_result.ForegroundLines.back(), style, _config, _bounds);

        newline.Content.left = ((float)(_currListDepth + 1) * _config.ListItemIndent) +
            ((float)(_currBlockquoteDepth + 1) * _config.BlockquoteOffset);
        newline.Content.top = lastline.Content.top + lastline.height() + (isEmpty ? 0.f : _config.LineGap);
        return newline;
    }

    ImVec2 DefaultTagVisitor::GetSegmentSize(const SegmentData& segment) const
    {
        auto height = 0.f, width = 0.f;
        const auto& style = _result.StyleDescriptors[segment.StyleIdx + 1];

        for (const auto& token : segment.Tokens)
        {
            height = std::max(height, token.Bounds.height + token.Offset.v());
            width += token.Bounds.width + token.Offset.h();
        }

        return { width + style.padding.h() + style.border.h(), height + style.padding.v() + style.border.v() };
    }

    ImVec2 DefaultTagVisitor::GetLineSize(const DrawableLine& line) const
    {
        auto height = 0.f, width = 0.f;

        for (const auto& segment : line.Segments)
        {
            auto sz = GetSegmentSize(segment);
            height = std::max(height, sz.y);
            width += sz.x;
        }

        return { width, height };
    }

    float DefaultTagVisitor::GetMaxSuperscriptOffset(const DrawableLine& line, float scale) const
    {
        auto topOffset = 0.f;
        auto baseFontSz = 0.f;

        for (auto idx = 0; idx < (int)line.Segments.size();)
        {
            const auto& segment = line.Segments[idx];
            baseFontSz = _result.StyleDescriptors[segment.StyleIdx + 1].font.size;
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

    float DefaultTagVisitor::GetMaxSubscriptOffset(const DrawableLine& line, float scale) const
    {
        auto topOffset = 0.f;
        auto baseFontSz = 0.f;

        for (auto idx = 0; idx < (int)line.Segments.size();)
        {
            const auto& segment = line.Segments[idx];
            baseFontSz = _result.StyleDescriptors[segment.StyleIdx + 1].font.size;
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

    StyleDescriptor& DefaultTagVisitor::Style(int stackpos)
    {
        return stackpos < 0 ? _result.StyleDescriptors.front() : 
            _result.StyleDescriptors[_tagStack[stackpos].styleIdx + 1];
    }

    bool DefaultTagVisitor::CreateNewStyle()
    {
        auto parentIdx = _currentStackPos <= 0 ? -1 : _styleIndexStack[_currentStackPos - 1];
        const auto& parentStyle = _result.StyleDescriptors[parentIdx + 1];
        SetImplicitStyleProps(_currTagType, _currTag, _currStyle, parentStyle, _currBgShape, 
            _currLine, _config);
        auto hasUniqueStyle = _currStyle.propsSpecified != 0;

        if (hasUniqueStyle)
        {
            if ((_currStyle.propsSpecified & StyleBackground) ||
                (_currStyle.propsSpecified & StyleBorder) ||
                (_currStyle.propsSpecified & StyleBoxShadow))
            {
                _currStyle.backgroundIdx = (int)_backgroundSpans[_currentStackPos].size();
                _currHasBackground = _tagStack[_currentStackPos].hasBackground = true;
            }

            _result.StyleDescriptors.emplace_back(_currStyle);
            _styleIdx = ((int)_result.StyleDescriptors.size() - 2);
            AddSegment();
        }

        _styleIdx = ((int)_result.StyleDescriptors.size() - 2);
        _styleIndexStack[_currentStackPos] = _styleIdx;
        _tagStack[_currentStackPos].styleIdx = _styleIdx;
        return hasUniqueStyle;
    }

    void DefaultTagVisitor::PopCurrentStyle()
    {
        // Make _currStyle refer to parent style, reset non-inheritable property
        auto parentIdx = _currentStackPos < 0 ? -1 : _styleIndexStack[_currentStackPos];
        _currStyle = _result.StyleDescriptors[parentIdx + 1];

        if (_currTagType != TagType::LineBreak)
        {
            _currStyle.propsSpecified = 0;
            _currStyle.backgroundIdx = -1;
            _currStyle.superscriptOffset = _currStyle.subscriptOffset = 0.f;
        }
    }

    bool DefaultTagVisitor::TagStart(std::string_view tag)
    {
        if (!CanContentBeMultiline(_currTagType) && AreSame(tag, "br")) return true;

        LOG("Entering Tag: <%.*s>\n", (int)tag.size(), tag.data());
        _currTag = tag;
        _currTagType = GetTagType(tag, _config.IsStrictHTML5);
        _currHasBackground = false;
        PopCurrentStyle();
            
        PushTag(_currTag, _currTagType);
        if (_currTagType == TagType::Superscript) _currSuperscriptLevel++;
        else if (_currTagType == TagType::Subscript) _currSubscriptLevel++;

        if (_currentStackPos >= 0 && _tagStack[_currentStackPos].tag != _currTag)
            ERROR("Tag mismatch...");
        return true;
    }
        
    bool DefaultTagVisitor::Attribute(std::string_view name, std::optional<std::string_view> value)
    {
        LOG("Reading attribute: %.*s\n", (int)name.size(), name.data());
        auto propsSpecified = 0;
        auto nonStyleAttribute = false;
        const auto& parentStyle = Style(_currentStackPos - 1);
        std::tie(propsSpecified, nonStyleAttribute) = RecordTagProperties(
            _currTagType, name, value, _currStyle, _currBgShape, _currTagProps, parentStyle, _config);

        if (!nonStyleAttribute)
            _currStyle.propsSpecified |= propsSpecified;

        return true;
    }

    bool DefaultTagVisitor::TagStartDone()
    {
        auto hasSegments = !_currLine.Segments.empty();
        auto hasUniqueStyle = CreateNewStyle();
        auto segmentAdded = hasUniqueStyle;
        auto& currentStyle = Style(_currentStackPos);
        int16_t tagPropIdx = -1;
        auto currListIsNumbered = false;

        if (_currTagProps != TagPropertyDescriptor{})
        {
            tagPropIdx = (int16_t)_result.TagDescriptors.size();
            _result.TagDescriptors.emplace_back(_currTagProps);
        }

        if (_currTagType == TagType::List)
        {
            _currListDepth++;
            currListIsNumbered = AreSame(_currTag, "ol");
        }
        else if (_currTagType == TagType::Font)
        {
            AddSegment();
        }
        else if (_currTagType == TagType::Paragraph || _currTagType == TagType::Header ||
            _currTagType == TagType::RawText || _currTagType == TagType::ListItem ||
            _currTagType == TagType::CodeBlock || _currTagType == TagType::Marquee)
        {
            if (hasSegments)
                _currLine = MoveToNextLine(true);
            _maxWidth = std::max(_maxWidth, _result.ForegroundLines.empty() ? 0.f : 
                _result.ForegroundLines.back().Content.width);

            if (_currTagType == TagType::Paragraph && _config.ParagraphStop > 0)
                _currLine.Offset.left += _config.Renderer->GetTextSize(std::string_view{ LineSpaces,
                    (std::size_t)std::min(_config.ParagraphStop, IM_RICHTEXT_MAXTABSTOP) }, 
                    currentStyle.font.font).x;
            else if (_currTagType == TagType::ListItem)
            {
                _listItemCountByDepths[_currListDepth]++;

                Token token;
                auto& listItem = _result.ListItemTokens.emplace_back();
                token.Type = !currListIsNumbered ? TokenType::ListItemBullet :
                    TokenType::ListItemNumbered;
                listItem.ListDepth = _currListDepth;
                listItem.ListItemIndex = _listItemCountByDepths[_currListDepth];
                token.ListPropsIdx = (int16_t)(_result.ListItemTokens.size() - 1u);

                AddSegment();
                AddToken(token, currentStyle.propsSpecified);
                segmentAdded = true;
            }
        }
        else if (_currTagType == TagType::Blockquote)
        {
            _currBlockquoteDepth++;
            if (!_currLine.Segments.empty())
                _currLine = MoveToNextLine(true);
            _maxWidth = std::max(_maxWidth, _result.ForegroundLines.empty() ? 0.f : 
                _result.ForegroundLines.back().Content.width);
            auto& start = _blockquoteStack[_currBlockquoteDepth].bounds.emplace_back();
            start.first = ImVec2{ _currLine.Content.left, _currLine.Content.top };
        }
        else if (_currTagType == TagType::Quotation)
        {
            Token token;
            token.Type = TokenType::Text;
            token.Content = "\"";
            AddToken(token, currentStyle.propsSpecified);
        }
        else if (_currTagType == TagType::Meter)
        {
            Token token;
            token.Type = TokenType::Meter;
            token.PropertiesIdx = tagPropIdx;
            AddToken(token, currentStyle.propsSpecified);
        }

        if (_currLine.Segments.empty())
            AddSegment();

        auto& segment = _currLine.Segments.back();
        segment.SubscriptDepth = _currSubscriptLevel;
        segment.SuperscriptDepth = _currSuperscriptLevel;

        if (_currHasBackground)
        {
            // The current line is `currline` which is not yet added, hence record .size()
            auto& bgspan = _backgroundSpans[_currentStackPos].emplace_back();
            bgspan.span.start.first = (int)_result.ForegroundLines.size();
            bgspan.span.start.second = (int)_currLine.Segments.size() - 1;
            bgspan.styleIdx = _styleIdx;
            bgspan.shape = _currBgShape;
            bgspan.isMultiline = CanContentBeMultiline(_currTagType);
            _currBgShape = BackgroundShape{};
        }

        return true;
    }

    bool DefaultTagVisitor::Content(std::string_view content)
    {
        struct UserData
        {
            StyleDescriptor& currentStyle;
            DrawableLine& currline;
            std::vector<DrawableLine>& newlines;
            std::string_view content;
            int styleIdx;
            DefaultTagVisitor* self;
        };

        // Ignore newlines, tabs & consecutive spaces
        auto to = 0, from = 0;
        auto& currentStyle = Style(_currentStackPos);
        LOG("Processing content [%.*s]\n", (int)content.size(), content.data());

        if (_currLine.Segments.empty()) _currLine.Segments.emplace_back().StyleIdx = _styleIdx;

        auto curridx = 0, start = 0;
        auto ignoreLineBreaks = _currSuperscriptLevel > 0 || _currSubscriptLevel > 0;
        auto isPreformatted = IsPreformattedContent(_currTag);
        UserData userdata{ currentStyle, _currLine, _result.ForegroundLines, content, _styleIdx, this };

        _config.TextShaper->SegmentText(content, _currStyle.wscbhv, 
            [](int, void* userdata)
            {
                const auto& data = *reinterpret_cast<UserData*>(userdata);
                data.newlines.push_back(data.currline);

                data.currline = CreateNewLine(-1);
                data.currline.Segments.emplace_back().StyleIdx = data.styleIdx;
            }, 
            [](int, std::string_view word, ImVec2 dim, void* userdata)
            {
                const auto& data = *reinterpret_cast<UserData*>(userdata);
                data.self->GenerateTextToken(word);
            }, 
            _config, ignoreLineBreaks, isPreformatted, &userdata);
        return true;
    }

    bool DefaultTagVisitor::TagEnd(std::string_view tag, bool selfTerminatingTag)
    {
        if (!CanContentBeMultiline(_currTagType) && AreSame(tag, "br")) return true;

        // pop stye properties and reset
        _styleIndexStack[_currentStackPos] = -2;
        PopTag(!selfTerminatingTag);
        _styleIdx = _currentStackPos >= 0 ? _styleIndexStack[_currentStackPos] : -1;
        PopCurrentStyle();

        auto segmentAdded = false;
        LOG("Exited Tag: <%.*s>\n", (int)_currTag.size(), _currTag.data());

        if (_currTagType == TagType::List || _currTagType == TagType::Paragraph || 
            _currTagType == TagType::Header ||
            _currTagType == TagType::RawText || _currTagType == TagType::Blockquote || 
            _currTagType == TagType::LineBreak ||
            _currTagType == TagType::CodeBlock || _currTagType == TagType::Marquee)
        {
            if (_currTagType == TagType::List)
            {
                _listItemCountByDepths[_currListDepth] = 0;
                _currListDepth--;
            }

            _currLine.Marquee = _currTagType == TagType::Marquee;
            _currLine = MoveToNextLine(false);
            _maxWidth = std::max(_maxWidth, _result.ForegroundLines.back().Content.width);

            if (_currTagType == TagType::Blockquote)
            {
                assert(!_blockquoteStack[_currBlockquoteDepth].bounds.empty());
                auto& bounds = _blockquoteStack[_currBlockquoteDepth].bounds.back();
                const auto& lastLine = _result.ForegroundLines[_result.ForegroundLines.size() - 2u];
                bounds.second = ImVec2{ lastLine.width() + bounds.first.x, lastLine.Content.top + lastLine.height() };
                _currBlockquoteDepth--;

                /*AddSegment(_currLine, _styleIdx, result.StyleDescriptors, config);
                segmentAdded = true;*/
            }
            else if (_currTagType == TagType::Header)
            {
                // Add properties for horizontal line below header
                StyleDescriptor style = _currStyle;
                style.height = 1.f;
                style.fgcolor = _config.HeaderLineColor;
                style.padding.top = style.padding.bottom = _config.HrVerticalMargins;
                _result.StyleDescriptors.emplace_back(style);
                AddSegment();
                _currLine.Segments.back().StyleIdx = (int)_result.StyleDescriptors.size() - 2;

                Token token;
                token.Type = TokenType::HorizontalRule;
                AddToken(token, NoStyleChange);

                // Move to next line for other content
                _currLine = MoveToNextLine(false);
                _maxWidth = std::max(_maxWidth, _result.ForegroundLines.back().Content.width);
            }
        }
        else if (_currTagType == TagType::Hr)
        {
            // Since hr style is popped, refer to next item in stack
            auto& prevstyle = Style(_currentStackPos + 1);
            prevstyle.padding.top = prevstyle.padding.bottom = _config.HrVerticalMargins;
            if (!_currLine.Segments.empty())
                _currLine = MoveToNextLine(false);
            _maxWidth = std::max(_maxWidth, _result.ForegroundLines.empty() ? 0.f : 
                _result.ForegroundLines.back().Content.width);

            Token token;
            token.Type = TokenType::HorizontalRule;
            AddSegment();
            AddToken(token, NoStyleChange);

            _currLine = MoveToNextLine(true);
            _maxWidth = std::max(_maxWidth, _result.ForegroundLines.back().Content.width);
        }
        else if (_currTagType == TagType::Quotation)
        {
            Token token;
            token.Type = TokenType::Text;
            token.Content = "\"";
            AddToken(token, NoStyleChange);
        }
        else if (_currTagType != TagType::Unknown)
        {
            if (_currTagType == TagType::Superscript)
            {
                _currSuperscriptLevel--;
                AddSegment();
            }
            else if (_currTagType == TagType::Subscript)
            {
                _currSubscriptLevel--;
                AddSegment();
            }
        }

        // Record background end for non-multiline content
        if (!CanContentBeMultiline(_currTagType) && _currHasBackground)
            RecordBackgroundSpanEnd(!selfTerminatingTag, segmentAdded);

        // Update all members for next tag in stack
        if (selfTerminatingTag) _tagStack[_currentStackPos + 1] = StackData{};
        _currTag = _currentStackPos == -1 ? "" : _tagStack[_currentStackPos].tag;
        _currTagType = _currentStackPos == -1 ? TagType::Unknown : _tagStack[_currentStackPos].tagType;
        _currHasBackground = _currentStackPos == -1 ? false : _tagStack[_currentStackPos].hasBackground;
        _currTagProps = TagPropertyDescriptor{};
        return true;
    }

    void DefaultTagVisitor::Finalize()
    {
        MoveToNextLine(false);
        _maxWidth = std::max(_maxWidth, _result.ForegroundLines.back().Content.width);

        for (auto& line : _result.ForegroundLines)
        {
            if (line.Marquee) line.Content.width = _maxWidth;

            // Only apply alignment to lines which contain content from single tag
            // Multi-tag lines can be aligned, but would require a general purpose
            // HTML/CSS renderer, which is not a goal here
            if (line.Segments.size() == 1u && !line.Segments.front().Tokens.empty())
            {
                auto& segment = line.Segments.front();
                auto& style = _result.StyleDescriptors[segment.StyleIdx + 1];

                // If complete text is already clipped/occluded, do not apply alignment
                if (segment.Tokens.size() == 1u && (segment.Tokens.front().Type == TokenType::Text || 
                    segment.Tokens.front().Type == TokenType::ElidedText) &&
                    segment.Tokens.front().VisibleTextSize < (int16_t)segment.Tokens.front().Content.size())
                    continue;

                if ((style.alignment & TextAlignHCenter) || (style.alignment & TextAlignRight)
                    || (style.alignment & TextAlignJustify))
                {
                    float occupiedWidth = 0.f;
                    for (const auto& token : segment.Tokens)
                        occupiedWidth += token.Bounds.width;
                    auto leftover = _maxWidth - occupiedWidth;

                    for (auto tidx = 0; tidx < (int)segment.Tokens.size(); ++tidx)
                    {
                        auto& token = segment.Tokens[tidx];
                        if (style.alignment & TextAlignHCenter)
                            token.Offset.left += leftover * 0.5f;
                        else if (style.alignment & TextAlignRight)
                            token.Offset.left += leftover;
                        else if (style.alignment & TextAlignJustify)
                        {
                            if (tidx == (int)(segment.Tokens.size() - 1u)) break;
                            token.Offset.right += (leftover / (float)(segment.Tokens.size() - 1u));
                        }
                    }
                }

                if ((style.alignment & TextAlignVCenter) || (style.alignment & TextAlignBottom))
                {
                    float occupiedHeight = 0.f;
                    for (const auto& token : segment.Tokens)
                        occupiedHeight = std::max(occupiedHeight, token.Bounds.height);

                    for (auto& token : segment.Tokens)
                    {
                        if (style.alignment & TextAlignVCenter)
                            token.Offset.top = (line.height() - occupiedHeight) * 0.5f;
                        else if (style.alignment & TextAlignBottom)
                            token.Offset.top = line.height() - occupiedHeight;
                    }
                }
            }
            else
                ERROR("Cannot apply alignment to multi-tag lines\n[NOTE: If this feature is required, "
                    "a general purpose HTML/CSS renderer should be used, which this library is not!]\n");
        }

        for (auto depth = 0; depth < IM_RICHTEXT_MAXDEPTH; ++depth)
        {
            /*for (const auto& bound : _blockquoteStack[depth].bounds)
            {
                if (config.BlockquoteBarWidth > 1.f && config.DefaultBgColor != config.BlockquoteBar)
                    result.BackgroundShapes.emplace_back(BackgroundShape{ bound.first, ImVec2{ config.BlockquoteBarWidth, bound.second.y },
                        config.BlockquoteBar });

                if (config.DefaultBgColor != config.BlockquoteBg)
                    result.BackgroundShapes.emplace_back(BackgroundShape{ ImVec2{ bound.first.x + config.BlockquoteBarWidth, bound.first.y },
                        bound.second, config.BlockquoteBg });
            }*/

            // Create background shapes for each depth and reset original specifications
            auto bgidx = 0;

            for (const auto& bgdata : _backgroundSpans[depth])
            {
                if (bgdata.span.end.first == -1) continue;

                auto& firstSegment = _result.ForegroundLines[bgdata.span.start.first].Segments[bgdata.span.start.second];
                const auto& lastSegment = _result.ForegroundLines[bgdata.span.end.first].Segments[bgdata.span.end.second];

                auto& background = _result.BackgroundShapes[depth].emplace_back();
                background = bgdata.shape;
                background.Start = { firstSegment.Bounds.left, firstSegment.Bounds.top };
                background.End = { lastSegment.Bounds.left + lastSegment.Bounds.width,
                    lastSegment.Bounds.top + _result.ForegroundLines[bgdata.span.end.first].height() };
                background.Start.x = std::min(background.Start.x, lastSegment.Bounds.left);
                background.End.x = std::max(background.End.x, firstSegment.Bounds.left + firstSegment.Bounds.width);
                ++bgidx;
            }
        }
    }

    void DefaultTagVisitor::Error(std::string_view tag)
    {
        // TODO
    }

    bool DefaultTagVisitor::IsSelfTerminating(std::string_view tag) const
    {
        return AreSame(tag, "br") || AreSame(tag, "hr");
    }

    bool DefaultTagVisitor::IsPreformattedContent(std::string_view tag) const
    {
        return AreSame(tag, "code") || AreSame(tag, "pre");
    }

    // ===============================================================
    // Section #4. Implementation of public API
    // ===============================================================

#ifdef IM_RICHTEXT_TARGET_IMGUI
    static RenderConfig* GetRenderConfig(RenderConfig* config = nullptr)
    {
        if (config == nullptr)
        {
            auto ctx = ImGui::GetCurrentContext();
            auto it = ImRenderConfigs.find(ctx);
            assert(it != ImRenderConfigs.end());
            config = &(it->second.back());
        }

        return config;
    }
#endif
#ifdef IM_RICHTEXT_TARGET_BLEND2D
    static RenderConfig* GetRenderConfig(BLContext& ctx, RenderConfig* config = nullptr)
    {
        if (config != nullptr) return config;
        auto it = BLRenderConfigs.find(&ctx);
        assert(it != BLRenderConfigs.end());
        return &(it->second.back());
    }
#endif

#ifdef IM_RICHTEXT_TARGET_IMGUI
    static void Draw(std::size_t richTextId, const Drawables& drawables, ImVec2 pos, ImVec2 bounds, RenderConfig* config)
    {
        config = GetRenderConfig(config);
        auto& animation = RichTextMap.at(richTextId).animationData;
        DrawImpl(animation, drawables, pos, bounds, config);
    }

    static bool ShowDrawables(ImVec2 pos, std::string_view content, std::size_t richTextId, Drawables& drawables,
        ImVec2 bounds, RenderConfig* config)
    {
        ImGuiWindow* window = ImGui::GetCurrentWindow();
        if (window->SkipItems)
            return false;

        const auto& style = ImGui::GetCurrentContext()->Style;
        auto id = window->GetID(content.data(), content.data() + content.size());
        ImGui::ItemSize(bounds);
        ImGui::ItemAdd(ImRect{ pos, pos + bounds }, id);
        Draw(richTextId, drawables, pos + style.FramePadding, bounds, config);
        return true;
    }

    RenderConfig* GetCurrentConfig()
    {
        auto ctx = ImGui::GetCurrentContext();
        auto it = ImRenderConfigs.find(ctx);
        return it != ImRenderConfigs.end() ? &(it->second.back()) : &(ImRenderConfigs.at(nullptr).front());
    }

    void PushConfig(const RenderConfig& config)
    {
        auto ctx = ImGui::GetCurrentContext();
        ImRenderConfigs[ctx].push_back(config);
    }

    void PopConfig()
    {
        auto ctx = ImGui::GetCurrentContext();
        auto it = ImRenderConfigs.find(ctx);
        if (it != ImRenderConfigs.end()) it->second.pop_back();
    }

#endif
#ifdef IM_RICHTEXT_TARGET_BLEND2D
    void Draw(BLContext& context, std::size_t richTextId, const Drawables& drawables,
        ImVec2 pos, ImVec2 bounds, RenderConfig* config)
    {
        config = GetRenderConfig(context, config);
        auto& animation = RichTextMap.at(richTextId).animationData;
        DrawImpl(animation, drawables, pos, bounds, config);
    }

    bool ShowDrawables(BLContext& context, ImVec2 pos, std::size_t richTextId, Drawables& drawables,
        ImVec2 bounds, RenderConfig* config)
    {
        Draw(context, richTextId, drawables, pos + style.FramePadding, bounds, config);
        return true;
    }

    RenderConfig* GetCurrentConfig(BLContext& context)
    {
        auto it = BLRenderConfigs.find(&context);
        return it != BLRenderConfigs.end() ? &(it->second.back()) : &(BLRenderConfigs.at(nullptr).front());
    }

    void PushConfig(const RenderConfig& config, BLContext& context)
    {
        BLRenderConfigs[&context].push_back(config);
    }

    void PopConfig(BLContext& context)
    {
        auto it = BLRenderConfigs.find(&context);
        if (it != BLRenderConfigs.end()) it->second.pop_back();
    }
#endif

    RenderConfig* GetDefaultConfig(const DefaultConfigParams& params)
    {
        auto config = &(ImRenderConfigs[nullptr].emplace_back());
        config->NamedColor = &GetColor;
        config->FontScale = params.FontScale;
        config->DefaultFontSize = params.DefaultFontSize;
        config->MeterDefaultSize = { params.DefaultFontSize * 5.0f, params.DefaultFontSize };
        config->TextShaper = GetTextShaper(params.Charset);

#ifdef IM_RICHTEXT_BUNDLED_FONTLOADER
        if (params.FontLoadFlags != 0) LoadDefaultFonts(*config, params.FontLoadFlags, nullptr);
#endif
        return config;
    }

    ITextShaper* GetTextShaper(TextContentCharset charset)
    {
        switch (charset)
        {
        case TextContentCharset::ASCII: return ASCIITextShaper::Instance();
        default: break;
        }

        return nullptr;
    }

    static Drawables GetDrawables(const char* text, const char* textend, const RenderConfig& config, ImVec2 bounds)
    {
        Drawables result;
        DefaultTagVisitor visitor{ config, result, bounds };
        ParseRichText(text, textend, config.TagStart, config.TagEnd, visitor);
        return result;
    }

    static ImVec2 GetBounds(const Drawables& drawables, ImVec2 bounds)
    {
        ImVec2 result = bounds;
        const auto& style = ImGui::GetCurrentContext()->Style;

        if (bounds.x == FLT_MAX)
        {
            float width = 0.f;
            for (const auto& line : drawables.ForegroundLines)
                width = std::max(width, line.width() + line.Content.left);
            for (auto depth = 0; depth < IM_RICHTEXT_MAXDEPTH; ++depth)
                for (const auto& bg : drawables.BackgroundShapes[depth])
                    width = std::max(width, bg.End.x);
            result.x = width + (2.f * style.FramePadding.x);
        }

        if (bounds.y == FLT_MAX)
        {
            auto fgheight = 0.f, bgheight = 0.f;

            if (!drawables.ForegroundLines.empty())
            {
                const auto& lastFg = drawables.ForegroundLines.back();
                fgheight = lastFg.height() + lastFg.Content.top;
            }
            
            for (auto depth = 0; depth < IM_RICHTEXT_MAXDEPTH; ++depth)
                if (!drawables.BackgroundShapes[depth].empty())
                    bgheight = std::max(bgheight, drawables.BackgroundShapes[depth].back().End.y);
           
            result.y = std::max(fgheight, bgheight) + (2.f * style.FramePadding.y);
        }

        return result;
    }

    static ImVec2 ComputeBounds(Drawables& drawables, RenderConfig* config, ImVec2 bounds)
    {
        auto computed = GetBounds(drawables, bounds);

        // <hr> elements may not have width unless pre-specified, hence update them
        for (auto& line : drawables.ForegroundLines)
            for (auto& segment : line.Segments)
                for (auto& token : segment.Tokens)
                    if ((token.Type == TokenType::HorizontalRule) && ((drawables.StyleDescriptors[segment.StyleIdx + 1].propsSpecified & StyleWidth) == 0)
                        && token.Bounds.width == -1.f)
                        token.Bounds.width = segment.Bounds.width = line.Content.width = computed.x;
        return computed;
    }

    std::size_t CreateRichText(const char* text, const char* end)
    {
        if (end == nullptr) end = text + std::strlen(text);

        std::string_view key{ text, (size_t)(end - text) };
        auto hash = std::hash<std::string_view>()(key);
        RichTextMap[hash].richText = key;
        RichTextMap[hash].contentChanged = true;
        return hash;
    }

    bool UpdateRichText(std::size_t id, const char* text, const char* end)
    {
        auto rit = RichTextMap.find(id);

        if (rit != RichTextMap.end())
        {
            if (end == nullptr) end = text + std::strlen(text);

            std::string_view existingKey{ rit->second.richText };
            std::string_view key{ text, (size_t)(end - text) };

            if (key != existingKey)
            {
                RichTextMap[id].richText = key;
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
            RichTextMap.erase(it);
            return true;
        }

        return false;
    }

    void ClearAllRichTexts()
    {
        RichTextMap.clear();
    }

#ifdef IM_RICHTEXT_TARGET_IMGUI

    static bool Render(ImVec2 pos, std::size_t richTextId, std::optional<ImVec2> sz, bool show)
    {
        auto it = RichTextMap.find(richTextId);

        if (it != RichTextMap.end())
        {
            auto& drawdata = it->second;
            auto config = GetRenderConfig();

            if (config != drawdata.config || config->Scale != drawdata.scale ||
                config->FontScale != drawdata.fontScale || config->DefaultBgColor != drawdata.bgcolor
                || (sz.has_value() && sz.value() != drawdata.specifiedBounds) || drawdata.contentChanged)
            {
                drawdata.contentChanged = false;
                drawdata.config = config;
                drawdata.bgcolor = config->DefaultBgColor;
                drawdata.scale = config->Scale;
                drawdata.fontScale = config->FontScale;
                drawdata.specifiedBounds = sz.has_value() ? sz.value() : drawdata.specifiedBounds;
                config->Renderer->UserData = ImGui::GetCurrentWindow()->DrawList;

#ifdef _DEBUG
                auto ts = std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::high_resolution_clock().now().time_since_epoch());

                drawdata.drawables = GetDrawables(drawdata.richText.data(),
                    drawdata.richText.data() + drawdata.richText.size(), *config,
                    drawdata.specifiedBounds);

                ts = std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::high_resolution_clock().now().time_since_epoch()) - ts;
                HIGHLIGHT("\nParsing [#%d] took %lldus", (int)richTextId, ts.count());
#else
                drawdata.drawables = GetDrawables(drawdata.richText.data(),
                    drawdata.richText.data() + drawdata.richText.size(), *config,
                    drawdata.specifiedBounds);
#endif
            }

            drawdata.computedBounds = ComputeBounds(drawdata.drawables, config, drawdata.specifiedBounds);
            ShowDrawables(pos, drawdata.richText, richTextId, drawdata.drawables, drawdata.computedBounds, config);
            return true;
        }

        return false;
    }

    ImVec2 GetBounds(std::size_t richTextId)
    {
        if (Render({}, richTextId, std::nullopt, false))
            return RichTextMap.at(richTextId).computedBounds;
        return ImVec2{ 0.f, 0.f };
    }

    bool Show(std::size_t richTextId, std::optional<ImVec2> sz)
    {
        return Show(ImGui::GetCurrentWindow()->DC.CursorPos, richTextId, sz);
    }

    bool Show(ImVec2 pos, std::size_t richTextId, std::optional<ImVec2> sz)
    {
        return Render(pos, richTextId, sz, true);
    }

    bool ToggleOverlay()
    {
#ifdef _DEBUG
        ShowOverlay = !ShowOverlay;
        ShowBoundingBox = !ShowBoundingBox;
#endif
        return ShowOverlay;
    }
#endif
#ifdef IM_RICHTEXT_TARGET_BLEND2D

    static bool Render(BLContext& context, ImVec2 pos, std::size_t richTextId, std::optional<ImVec2> sz, bool show)
    {
        auto it = RichTextMap.find(richTextId);

        if (it != RichTextMap.end())
        {
            auto& drawdata = RichTextMap[richTextId];
            auto config = GetRenderConfig(context);

            if (config != drawdata.config || config->Scale != drawdata.scale ||
                config->FontScale != drawdata.fontScale || config->DefaultBgColor != drawdata.bgcolor
                || (sz.has_value() && sz.value() != drawdata.specifiedBounds) || drawdata.contentChanged)
            {
                drawdata.contentChanged = false;
                drawdata.config = config;
                drawdata.bgcolor = config->DefaultBgColor;
                drawdata.scale = config->Scale;
                drawdata.fontScale = config->FontScale;
                drawdata.specifiedBounds = sz.has_value() ? sz.value() : drawdata.specifiedBounds;

#ifdef _DEBUG
                auto ts = std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::high_resolution_clock().now().time_since_epoch());

                drawdata.drawables = GetDrawables(drawdata.richText.data(),
                    drawdata.richText.data() + drawdata.richText.size(), *config,
                    drawdata.specifiedBounds);

                ts = std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::high_resolution_clock().now().time_since_epoch()) - ts;
                HIGHLIGHT("\nParsing [#%d] took %lldus", (int)richTextId, ts.count());
#else
                drawdata.drawables = GetDrawables(drawdata.richText.data(),
                    drawdata.richText.data() + drawdata.richText.size(), *config,
                    drawdata.specifiedBounds);
#endif
            }

            drawdata.computedBounds = ComputeBounds(drawdata.drawables, config, drawdata.specifiedBounds);
            ShowDrawables(context, pos, richTextId, drawdata.drawables, drawdata.computedBounds, config);
            return true;
        }

        return false;
    }

    ImVec2 GetBounds(BLContext& context, std::size_t richTextId)
    {
        if (Render(context, {}, richTextId, std::nullopt, false))
            return RichTextMap.at(richTextId).computedBounds;
        return ImVec2{ 0.f, 0.f };
    }

    bool Show(BLContext& context, ImVec2 pos, std::size_t richTextId, std::optional<ImVec2> sz)
    {
        return Render(context, pos, richTextId, sz, true);
    }
    
#endif
}