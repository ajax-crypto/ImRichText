#pragma once

#include "imgui.h"
#include "imrichtextutils.h"

#include <string_view>
#include <vector>
#include <deque>
#include <initializer_list>

#define IM_RICHTEXT_DEFAULT_FONTFAMILY "default-font-family"
#define IM_RICHTEXT_MONOSPACE_FONTFAMILY "monospace"

#ifndef IM_RICHTEXT_MAXDEPTH
#define IM_RICHTEXT_MAXDEPTH 32
#endif

#ifndef IM_RICHTEXT_MAX_LISTDEPTH
#define IM_RICHTEXT_MAX_LISTDEPTH 16
#endif

#ifndef IM_RICHTEXT_MAX_LISTITEM
#define IM_RICHTEXT_MAX_LISTITEM 128
#endif

#ifndef IM_RICHTEXT_MAXTABSTOP
#define IM_RICHTEXT_MAXTABSTOP 32
#endif

#ifndef IM_RICHTEXT_BLINK_ANIMATION_INTERVAL
#define IM_RICHTEXT_BLINK_ANIMATION_INTERVAL 500
#endif

#ifndef IM_RICHTEXT_MARQUEE_ANIMATION_INTERVAL
#define IM_RICHTEXT_MARQUEE_ANIMATION_INTERVAL 18
#endif

#define IM_RICHTEXT_NESTED_ITEMCOUNT_STRSZ 64

#ifndef IMGUI_DEFINE_MATH_OPERATORS
ImVec2 operator+(ImVec2 lhs, ImVec2 rhs) { return ImVec2{ lhs.x + rhs.x, lhs.y + rhs.y }; }
#endif

namespace ImRichText
{
    enum class HorizontalAlignment
    {
        Left, Right, Center, Justify
    };

    enum class VerticalAlignment
    {
        Top, Bottom, Center
    };

    enum class TokenType
    {
        Text,
        ListItemBullet,
        ListItemNumbered,
        HorizontalRule,
        Meter
    };

    struct Token
    {
        std::string_view Content = "";
        BoundedBox Bounds;
        TokenType Type = TokenType::Text;
        char NestedListItemIndex[IM_RICHTEXT_NESTED_ITEMCOUNT_STRSZ];

        int ListDepth = -1;
        int ListItemIndex = -1;
    };

    struct FontStyle
    {
        ImFont* font = nullptr;
        std::string_view family = IM_RICHTEXT_DEFAULT_FONTFAMILY;
        float size = 24.f;
        bool bold = false;
        bool italics = false;
        bool light = false;
        bool strike = false;
        bool underline = false;
        bool wrap = true;
        bool overflowEllipsis = false;
    };

    struct ListStyle
    {
        ImColor itemColor = IM_COL32_BLACK;
        BulletType itemStyle = BulletType::FilledCircle;
    };

    //enum BoxSide
    //{
    //    TopSide,
    //    RightSide,
    //    BottomSide,
    //    LeftSide
    //};

    enum StyleProperty
    {
        StyleError = -1,
        NoStyleChange = 0,
        StyleBackground = 1,
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
        StylePaddingRight = 16384,
        StyleBlink = 32768
    };

    // A rich text consists of the following hierarchial structures:
    // Array of segments of unqiue style
    // Each segment consists of block of lines
    // Each line consists of array of tokens

    struct StyleDescriptor
    {
        int propsSpecified = 0;
        ImColor fgcolor = IM_COL32_BLACK;
        ImColor bgcolor = IM_COL32_BLACK_TRANS;
        float height = 0;
        float width = 0;
        HorizontalAlignment alignmentH = HorizontalAlignment::Left;
        VerticalAlignment alignmentV = VerticalAlignment::Center;
        FontStyle font;
        ListStyle list;
        FourSidedMeasure padding;
        float superscriptOffset = 0.f;
        float subscriptOffset = 0.f;
        ColorGradient gradient;
        
        std::string_view tooltip = "";
        std::string_view link = "";
        float value = 0.f;
        std::pair<float, float> range = { 0.f, 0.f };
        bool blink = false;
    };

    struct SegmentData
    {
        std::vector<Token> Tokens;
        BoundedBox Bounds;
        int StyleIdx = -1;

        int SubscriptDepth = 0;
        int SuperscriptDepth = 0;
        bool HasText = false;

        float width() const { return Bounds.width; }
        float height() const { return Bounds.height; }
    };

    struct DrawableLine
    {
        std::vector<SegmentData> Segments;
        BoundedBox Content;
        FourSidedMeasure Offset;

        int  BlockquoteDepth = 0;
        bool HasText = false;
        bool HasSuperscript = false;
        bool HasSubscript = false;
        bool Marquee = false;

        float width() const { return Content.width + Offset.left + Offset.right; }
        float height() const { return Content.height + Offset.top + Offset.bottom; }
    };

    struct BackgroundShape
    {
        ImVec2 Start, End;
        ImColor Color = IM_COL32_BLACK_TRANS;
        ColorGradient Gradient;
    };

#ifdef _DEBUG
    enum DebugContentType
    {
        ContentTypeToken,
        ContentTypeSegment,
        ContentTypeLine,
        ContentTypeBg,
        ContentTypeTotal
    };
#endif

    struct RenderConfig
    {
        float Scale = 1.0f;
        float FontScale = 1.f;

        char TagStart = '<';
        char TagEnd = '>';
        char EscapeSeqStart = '&';
        char EscapeSeqEnd = ';';
        std::vector<std::pair<std::string_view, std::string_view>> EscapeCodes;

        float LineGap = 5;
        ImVec2 Bounds;
        bool WordWrap = false;

        int   ParagraphStop = 4;
        int   TabStop = 4;
        float ListItemIndent = 15.f;
        float ListItemOffset = 15.f;
        BulletType ListItemBullet = BulletType::FilledCircle;

        std::string_view DefaultFontFamily = IM_RICHTEXT_DEFAULT_FONTFAMILY;
        float   DefaultFontSize = 20;
        ImColor DefaultFgColor = IM_COL32_BLACK;
        ImColor DefaultBgColor = IM_COL32_WHITE;
        ImColor MarkHighlight = ImColor{ 255, 255, 0 };
        ImColor HyperlinkColor = ImColor{ 0, 50, 255 };

        ImFont* (*GetFont)(std::string_view, float, bool, bool, bool, void*) = nullptr;
        ImVec2  (*GetTextSize)(std::string_view, ImFont*) = nullptr;
        ImColor (*NamedColor)(const char*, void*) = nullptr;
        void    (*DrawBullet)(ImVec2, ImVec2, const StyleDescriptor&, int, int, void*) = nullptr;
        void    (*HandleAttribute)(std::string_view, std::string_view, std::string_view, void*) = nullptr;
        void    (*HandleHyperlink)(std::string_view, void*) = nullptr;
        void    (*RequestFrame)(void*) = nullptr;

        float   HFontSizes[6] = { 36, 32, 24, 20, 16, 12 };
        ImColor HeaderLineColor = ImColor(128, 128, 128, 255);

        ImColor BlockquoteBar = { 0.25f, 0.25f, 0.25f, 1.0f };
        ImColor BlockquoteBg = { 0.5f, 0.5f, 0.5f, 1.0f };
        float   BlockquotePadding = 5.f;
        float   BlockquoteOffset = 15.f;
        float   BlockquoteBarWidth = 5.f;

        ImColor MeterBorderColor = ImColor{ 100, 100, 100 };
        ImColor MeterBgColor = ImColor{ 200, 200, 200 };
        ImColor MeterFgColor = ImColor{ 0, 200, 25 };
        ImVec2  MeterDefaultSize = { 80.f, 16.f };
        ImColor CodeBlockBg = IM_COL32_BLACK_TRANS;
        float   CodeBlockPadding = 5.f;

        float BulletSizeScale = 2.f;
        float ScaleSuperscript = 0.62f;
        float ScaleSubscript = 0.62f;
        float HrVerticalMargins = 5.f;
        void* UserData = nullptr;

        bool IsStrictHTML5 = false;

#ifdef _DEBUG
        ImColor DebugContents[ContentTypeTotal] = {
            IM_COL32_BLACK_TRANS, IM_COL32_BLACK_TRANS, 
            IM_COL32_BLACK_TRANS, IM_COL32_BLACK_TRANS
        };
#endif
    };

    struct Drawables
    {
        std::deque<DrawableLine>     ForegroundLines;
        std::vector<BackgroundShape> BackgroundShapes;
        std::vector<StyleDescriptor> StyleDescriptors;
    };

    // RenderConfig related functions. In order to render rich text, such configs should be pushed/popped as desired 
    [[nodiscard]] RenderConfig* GetDefaultConfig(ImVec2 Bounds = { -1.f, -1.f }, float defaultFontSize = 24.f, 
        float fontScale = 1.f, bool skipDefaultFontLoading = false);
    [[nodiscard]] RenderConfig* GetCurrentConfig();
    void PushConfig(const RenderConfig& config);
    void PopConfig();

    // Get list of drawables from rich text
    [[nodiscard]] Drawables GetDrawables(const char* text, const char* textend, const RenderConfig& config);
    [[nodiscard]] ImVec2 GetBounds(const Drawables& drawables, ImVec2 bounds);

    // Render rich text without any drawable caching
    bool Show(const char* text, const char* end = nullptr);

    // Create cacheable rich text content
    [[nodiscard]] std::size_t CreateRichText(const char* text, const char* end = nullptr);
    bool UpdateRichText(std::size_t id, const char* text, const char* end = nullptr);
    bool RemoveRichText(std::size_t id);
    void ClearAllRichTexts();
    bool Show(std::size_t richTextId);
}
