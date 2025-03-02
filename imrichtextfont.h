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

    enum FontLoadType : uint64_t
    {
        FLT_Proportional = 1,
        FLT_Monospace = 2,
        FLT_HasSmall = 4,
        FLT_HasSuperscript = 8,
        FLT_HasSubscript = 16,
        FLT_HasH1 = 32,
        FLT_HasH2 = 64,
        FLT_HasH3 = 128,
        FLT_HasH4 = 256,
        FLT_HasH5 = 512,
        FLT_HasH6 = 1024,
        FLT_HasHeaders = FLT_HasH1 | FLT_HasH2 | FLT_HasH3 | FLT_HasH4 | FLT_HasH5 | FLT_HasH6,

        // TODO: Handle absolute size font-size fonts (Look at imrichtext.cpp: PopulateSegmentStyle function)
    };

    // TODO: Add support for char ranges based on charset
    bool LoadDefaultFonts(const RenderConfig& config, uint64_t flt, FontFileNames* names = nullptr);

    [[nodiscard]] void* GetFont(std::string_view family, float size, FontType type, void*);
    [[nodiscard]] void* GetOverlayFont(const RenderConfig& config);

#ifdef IM_RICHTEXT_TARGET_IMGUI
    [[nodiscard]] bool IsFontLoaded();
#endif
}
