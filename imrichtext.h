#pragma once

#include "imgui.h"

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
    };

    struct ListStyle
    {
        ImColor itemColor = IM_COL32_BLACK;
        BulletType itemStyle = BulletType::FilledCircle;
    };

    enum BoxSide
    {
        TopSide,
        RightSide,
        BottomSide,
        LeftSide
    };

    struct SegmentStyle
    {
        ImColor fgcolor = IM_COL32_BLACK;
        ImColor bgcolor = IM_COL32_WHITE;
        float height = 0;
        float width = 0;
        HorizontalAlignment alignmentH = HorizontalAlignment::Left;
        VerticalAlignment alignmentV = VerticalAlignment::Center;
        FontStyle font;
        ListStyle list;
        FourSidedMeasure padding;
        float superscriptOffset = 0.f;
        float subscriptOffset = 0.f;
        std::string_view tooltip = "";
        std::string_view link = "";
        bool blink = false;
    };

    struct SegmentDetails
    {
        std::vector<Token> Tokens;
        SegmentStyle Style;
        BoundedBox Bounds;

        int SubscriptDepth = 0;
        int SuperscriptDepth = 0;
        bool HasText = false;

        float width() const { return Bounds.width + Style.padding.left + Style.padding.right; }
        float height() const { return Bounds.height + Style.padding.top + Style.padding.bottom; }
    };

    struct DrawableLine
    {
        std::vector<SegmentDetails> Segments;
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

        int ParagraphStop = 4;
        int TabStop = 4;
        float ListItemIndent = 15.f;
        float ListItemOffset = 15.f;
        BulletType ListItemBullet = BulletType::FilledCircle;

        std::string_view DefaultFontFamily = IM_RICHTEXT_DEFAULT_FONTFAMILY;
        float DefaultFontSize = 20;
        ImColor DefaultFgColor = IM_COL32_BLACK;
        ImColor DefaultBgColor = IM_COL32_WHITE;
        ImColor MarkHighlight = ImColor{ 255, 255, 0 };
        ImColor HyperlinkColor = ImColor{ 0, 50, 255 };

        ImFont* (*GetFont)(std::string_view, float, bool, bool, bool, void*) = nullptr;
        ImVec2  (*GetTextSize)(std::string_view, ImFont*) = nullptr;
        ImColor (*NamedColor)(const char*, void*) = nullptr;
        void    (*DrawBullet)(ImVec2, ImVec2, const SegmentStyle&, int, int, void*) = nullptr;
        void    (*HandleAttribute)(std::string_view, std::string_view, std::string_view, void*) = nullptr;
        void    (*HandleHyperlink)(std::string_view, void*) = nullptr;

        float HFontSizes[6] = { 36, 32, 24, 20, 16, 12 };
        ImColor HeaderLineColor = ImColor(128, 128, 128, 255);

        ImColor BlockquoteBar = { 0.25f, 0.25f, 0.25f, 1.0f };
        ImColor BlockquoteBg = { 0.5f, 0.5f, 0.5f, 1.0f };
        float BlockquotePadding = 5.f;
        float BlockquoteOffset = 15.f;
        float BlockquoteBarWidth = 5.f;

        ImColor CodeBlockBg = IM_COL32_BLACK_TRANS;
        float CodeBlockPadding = 5.f;

        float BulletSizeScale = 2.f;
        float ScaleSuperscript = 0.62f;
        float ScaleSubscript = 0.62f;
        float HrVerticalMargins = 5.f;
        void* UserData = nullptr;

#ifdef _DEBUG
        ImColor DebugContents[ContentTypeTotal] = {
            IM_COL32_BLACK_TRANS, IM_COL32_BLACK_TRANS, 
            IM_COL32_BLACK_TRANS, IM_COL32_BLACK_TRANS
        };
#endif
    };

    struct BackgroundShape
    {
        ImVec2 Start, End;
        ImColor Color;
    };

    struct Drawables
    {
        std::deque<DrawableLine> Foreground;
        std::deque<BackgroundShape> Background;
    };

    // RenderConfig related functions. In order to render rich text, such configs should be pushed/popped as desired 
    [[nodiscard]] RenderConfig* GetDefaultConfig(ImVec2 Bounds, float defaultFontSize, float fontScale = 1.f, bool skipDefaultFontLoading = false);
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
