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

namespace ImRichText
{
    struct FontCollectionFile
    {
        std::string_view Normal;
        std::string_view Light;
        std::string_view Bold;
        std::string_view Italics;
        std::string_view BoldItalics;
    };

    struct FontFileNames
    {
        FontCollectionFile Proportional;
        FontCollectionFile Monospace;
        std::string_view BasePath;
    };

    enum class BulletType
    {
        Circle,
        FilledCircle,
        Disk = FilledCircle,
        Square,
        FilledSquare,
        Concentric,
        Cross,
        Tick,
        TickedSquare
    };

    enum class HorizontalAlignment
    {
        Left, Right, Center, Justify
    };

    enum class VerticalAlignment
    {
        Top, Bottom, Center
    };

    struct RenderConfig
    {
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
        ImVec2  (*GetTextSize)(std::string_view, ImFont*);
        ImColor (*NamedColor)(const char*, void*);
        float HFontSizes[6] = { 36, 32, 24, 20, 16, 12 };

        ImColor BlockquoteBar = { 0.25f, 0.25f, 0.25f, 1.0f };
        float BlockquotePadding = 5.f;
        float BlockquoteOffset = 15.f;
        float BlockquoteMargins = 10.f;

        float BulletSizeScale = 2.f;
        float ScaleSuperscript = 0.62f;
        float ScaleSubscript = 0.62f;
        float DefaultHrVerticalMargins = 8.f;
        void* UserData = nullptr;
    };

    enum class TokenType
    {
        Text,
        ListItemBullet,
        HorizontalRule,
        SuperscriptStart,
        SuperscriptEnd,
        SubscriptStart,
        SubscriptEnd
    };

    struct Token
    {
        std::string_view Content = "";
        ImVec2 Size;
        TokenType Type = TokenType::Text;
        int blockquoteDepth = 0;
        int listDepth = 0;
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

    struct BorderStyle
    {
        float thickness = 0;
        ImColor color = IM_COL32_BLACK;
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
        bool HasText = false;
        bool HasSuperscript = false;
        bool HasSubscript = false;
    };

    bool LoadFonts(std::string_view family, const FontCollectionFile& files, float size, const ImFontConfig& config);
    bool LoadDefaultFonts(float sz, FontFileNames* names = nullptr);
    bool LoadDefaultFonts(const std::initializer_list<float>& szs, FontFileNames* names = nullptr);
    bool LoadDefaultFonts(const RenderConfig& config);

    [[nodiscard]] ImFont* GetFont(std::string_view family, float size, bool bold, bool italics, bool light, void*);
    [[nodiscard]] ImColor GetColor(const char* name, void*);
    [[nodiscard]] RenderConfig* GetDefaultConfig(ImVec2 Bounds);
    [[nodiscard]] std::deque<DrawableLine> GetDrawableLines(const char* text, int start, int end, RenderConfig& config);

#ifdef _DEBUG
    void PrintAllTokens(const std::deque<DrawableLine>& lines);
#endif
    void PushConfig(const RenderConfig& config);
    void PopConfig();
    void Draw(const char* text, int start = 0, int end = -1, RenderConfig* config = nullptr);
    void Draw(std::deque<DrawableLine>& lines, RenderConfig* config = nullptr);
}
