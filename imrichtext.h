#pragma once

#include "imgui.h"

#include <string_view>
#include <vector>
#include <deque>
#include <initializer_list>

#define IM_RICHTEXT_DEFAULT_FONTFAMILY "default-font-family"
#define IM_RICHTEXT_MONOSPACE_FONTFAMILY "monospace"

#ifndef IM_RICHTEXT_MIN_RTF_CACHESZ
#define IM_RICHTEXT_MIN_RTF_CACHESZ 128
#endif

#ifndef IM_RICHTEXT_MAXDEPTH
#define IM_RICHTEXT_MAXDEPTH 256
#endif

#ifndef IM_RICHTEXT_MAX_LISTDEPTH
#define IM_RICHTEXT_MAX_LISTDEPTH 256
#endif

#ifndef IM_RICHTEXT_MAX_LISTITEM
#define IM_RICHTEXT_MAX_LISTITEM 128
#endif

#ifndef IM_RICHTEXT_MAXTABSTOP
#define IM_RICHTEXT_MAXTABSTOP 32
#endif

namespace ImRichText
{
    enum class BulletType
    {
        Circle,
        FilledCircle,
        Disk = FilledCircle,
        Square,
        FilledSquare,
        Concentric,
        Cross,
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
        ListStart,
        ListItemBullet,
        ListItemNumbered,
        ListEnd,
        HorizontalRule,
        BlockquoteStart,
        BlockquoteEnd,
        CodeBlockStart,
        CodeBlockEnd
    };

    struct Token
    {
        std::string_view Content = "";
        ImVec2 Size;
        TokenType Type = TokenType::Text;
    };

    struct FontStyle
    {
        ImFont* font = nullptr;
        std::string_view family = IM_RICHTEXT_DEFAULT_FONTFAMILY;
        float size = 24.f;
        bool bold = false;
        bool italics = false;
        bool light = false;
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
        float superscriptOffset = 0.f;
        float subscriptOffset = 0.f;
    };

    struct SegmentDetails
    {
        std::vector<Token> Tokens;
        SegmentStyle Style;
        int subscriptDepth = 0;
        int superscriptDepth = 0;
        bool HasText = false;
    };

    struct DrawableLine
    {
        std::vector<SegmentDetails> Segments;
        ImVec2 offseth = { 0.f, 0.f };
        ImVec2 offsetv = { 0.f, 0.f };
        int  BlockquoteDepth = 0;
        bool HasText = false;
        bool HasSuperscript = false;
        bool HasSubscript = false;
        bool InsideCodeBlock = false;
    };

    struct RenderConfig
    {
        float Scale = 1.0f;

        char TagStart = '<';
        char TagEnd = '>';
        char EscapeSeqStart = '&';
        char EscapeSeqEnd = ';';
        std::vector<std::pair<std::string_view, std::string_view>> EscapeCodes;

        float LineGap = 5;
        ImVec2 Bounds;
        bool DrawDebugRects = false;
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
        ImColor MarkHighlight = ImColor{};

        ImFont* (*GetFont)(std::string_view, float, bool, bool, bool, void*);
        ImVec2(*GetTextSize)(std::string_view, ImFont*);
        ImColor(*NamedColor)(const char*, void*);
        void (*DrawBullet)(ImVec2, ImVec2, const SegmentStyle&, int*, int);

        float HFontSizes[6] = { 36, 32, 24, 20, 16, 12 };
        ImColor HeaderLineColor = ImColor(128, 128, 128, 255);

        ImColor BlockquoteBar = { 0.25f, 0.25f, 0.25f, 1.0f };
        ImColor BlockquoteBg = { 0.5f, 0.5f, 0.5f, 1.0f };
        float BlockquotePadding = 5.f;
        float BlockquoteOffset = 15.f;
        float BlockquoteMargins = 10.f;
        float BlockquoteBarWidth = 5.f;

        float BulletSizeScale = 2.f;
        float ScaleSuperscript = 0.62f;
        float ScaleSubscript = 0.62f;
        float DefaultHrVerticalMargins = 8.f;
        void* UserData = nullptr;
    };

    [[nodiscard]] ImColor GetColor(const char* name, void*);
    [[nodiscard]] RenderConfig* GetDefaultConfig(ImVec2 Bounds, bool skipDefaultFontLoading = false);
    [[nodiscard]] std::deque<DrawableLine> GetDrawableLines(const char* text, int start, int end, RenderConfig& config);

#ifdef _DEBUG
    void PrintAllTokens(const std::deque<DrawableLine>& lines);
#endif
    void PushConfig(const RenderConfig& config);
    void PopConfig();
    void Draw(const char* text, int start = 0, int end = -1, RenderConfig* config = nullptr);
    void Draw(std::deque<DrawableLine>& lines, RenderConfig* config = nullptr);
}
