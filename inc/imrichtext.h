#pragma once

#include "imgui.h"

#include <string_view>
#include <vector>
#include <deque>
#include <initializer_list>

#define IM_RICHTEXT_DEFAULT_FONTFAMILY "default-font-family"
#define IM_RICH_TEXT_MONOSPACE_FONTFAMILY "monospace"

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

    struct RenderConfig
    {
        float LineGap = 5;
        ImVec2 Bounds;
        bool DrawDebugRects = false;

        int ParagraphStop = 4;
        int TabStop = 4;
        float ListItemIndent = 15;
        float ListItemOffset = 10;
        BulletType ListItemBullet = BulletType::FilledCircle;

        std::string_view DefaultFontFamily = IM_RICHTEXT_DEFAULT_FONTFAMILY;
        float DefaultFontSize = 20;
        ImColor DefaultFgColor = IM_COL32_BLACK;
        ImColor DefaultBgColor = IM_COL32_WHITE;
        
        ImFont* (*GetFont)(std::string_view, float, bool, bool, bool, void*);
        float HFontSizes[6] = { 36, 32, 24, 20, 16, 12 };
        ImColor(*NamedColor)(const char*, void*);

        float BulletSizeScale = 3.f;
        float ScaleSuperscript = 0.62f;
        float ScaleSubscript = 0.62f;
        float DefaultHrVerticalMargins = 8.f;
        void* UserData = nullptr;
    };

    enum class TokenType
    {
        Text,
        ListStart,
        ListEnd,
        ListItem,
        HorizontalRule,
        Superscript,
        Subscript,
        ParagraphStart,
        TableHeaderCell,
        TableDataCell
    };

    struct Token
    {
        std::string_view Content;
        ImVec2 Size;
        TokenType Type = TokenType::Text;
        ImVec2 Span; // only applicable for table cells
        std::pair<int, int> Extent;
    };

    struct FontStyle
    {
        ImFont* font = nullptr;
        std::string_view family = IM_RICHTEXT_DEFAULT_FONTFAMILY;
        float size = 12.f;
        bool bold = false;
        bool italics = false;
        bool light = false;
    };

    struct ListStyle
    {
        ImColor itemColor = IM_COL32_BLACK;
        BulletType itemStyle = BulletType::FilledCircle;
    };

    enum BoxCorner
    {
        TopLeftCorner,
        TopRightCorner,
        BottomLeftCorner,
        BottomRightCorner
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
        FontStyle font;
        ListStyle list;
        BorderStyle border[4];
        ImVec2 offsetv;
        float borderRoundedness[4];
        bool renderAllWhitespace = false;
    };

    struct SegmentDetails
    {
        std::vector<Token> Tokens;
        SegmentStyle Style;
        bool HasText = false;
    };

    struct DrawableLine
    {
        std::vector<SegmentDetails> Segments;
        bool HasText = false;
    };

    bool LoadFonts(std::string_view family, const FontCollectionFile& files, float size, const ImFontConfig& config);
    bool LoadDefaultFonts(float sz, FontFileNames* names = nullptr);
    bool LoadDefaultFonts(const std::initializer_list<float>& szs, FontFileNames* names = nullptr);
    [[nodiscard]] ImFont* GetFont(std::string_view family, float size, bool bold, bool italics, bool light, void*);
    [[nodiscard]] ImColor GetColor(const char* name, void*);
    [[nodiscard]] std::deque<DrawableLine> GetDrawableLines(const char* text, int start, int end, RenderConfig& config);

    void PushConfig(const RenderConfig& config);
    void PopConfig();
    void Draw(const char* text, int start = 0, int end = -1, RenderConfig* config = nullptr);
    void Draw(std::deque<DrawableLine>& lines, RenderConfig* config = nullptr);
}
