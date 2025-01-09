#pragma once

#include <string_view>

#include "imgui.h"

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

    struct RenderConfig;

    bool LoadFonts(std::string_view family, const FontCollectionFile& files, float size, ImFontConfig config);
    bool LoadDefaultFonts(float sz, FontFileNames* names = nullptr);
    bool LoadDefaultFonts(const std::initializer_list<float>& szs, FontFileNames* names = nullptr);
    bool LoadDefaultFonts(const RenderConfig& config);

    [[nodiscard]] ImFont* GetFont(std::string_view family, float size, bool bold, bool italics, bool light, void*);
    [[nodiscard]] ImFont* GetOverlayFont();
}
