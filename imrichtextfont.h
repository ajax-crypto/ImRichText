#pragma once

#include <string_view>

#ifdef IM_RICHTEXT_TARGET_IMGUI
#include "imgui.h"
#endif

/*
    This file is optional, provides a default cached
    font loading/caching mechanism that is used by the
    library. It is not mandatory to include this file
    in your project, if you are implementing your own 
    IRenderer interface.
*/

namespace ImRichText
{
    enum FontType
    {
        FT_Normal, FT_Light, FT_Bold, FT_Italics, FT_BoldItalics, FT_Total
    };

    struct FontCollectionFile
    {
        std::string_view Files[FT_Total];
    };

    struct FontFileNames
    {
        FontCollectionFile Proportional;
        FontCollectionFile Monospace;
        std::string_view BasePath;
    };

    struct RenderConfig;

#ifdef IM_RICHTEXT_TARGET_IMGUI
    bool LoadFonts(std::string_view family, const FontCollectionFile& files, float size, ImFontConfig config);
#endif
#ifdef IM_RICHTEXT_TARGET_BLEND2D
    bool LoadFonts(std::string_view family, const FontCollectionFile& files, float size);
#endif

    bool LoadDefaultFonts(float sz, FontFileNames* names = nullptr);
    bool LoadDefaultFonts(const std::initializer_list<float>& szs, FontFileNames* names = nullptr);
    bool LoadDefaultFonts(const RenderConfig& config);

    [[nodiscard]] void* GetFont(std::string_view family, float size, FontType type, void*);
    [[nodiscard]] void* GetOverlayFont(const RenderConfig& config);
}
