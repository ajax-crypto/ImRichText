#include "imrichtextfont.h"
#include "imrichtext.h"
#if defined(IM_RICHTEXT_TARGET_IMGUI) && defined(IMGUI_ENABLE_FREETYPE)
#include "misc/freetype/imgui_freetype.h"
#elif defined(IM_RICHTEXT_TARGET_BLEND2D)
#include "blend2d.h"
#endif

#include <unordered_set>
#include <unordered_map>
#include <map>

// {family, style} -> { size } -> font

namespace ImRichText
{
    struct FontFamily
    {
        // TODO: Maybe the std::map can be avoided
        // Use sorted vector + lower_bound lookups?
#ifdef IM_RICHTEXT_TARGET_IMGUI
        std::map<float, ImFont*> Fonts[FT_Total];
#endif
#ifdef IM_RICHTEXT_TARGET_BLEND2D
        std::map<float, BLFont> Fonts[FT_Total];
        BLFontFace FontFace[FT_Total];
#endif
        FontCollectionFile Files;
    };

    static std::unordered_map<std::string_view, FontFamily> FontStore;

#ifdef IM_RICHTEXT_TARGET_IMGUI
    static void LoadFont(ImGuiIO& io, FontFamily& family, FontType ft, float size, ImFontConfig config, int flag)
    {
        //io.Fonts->Flags = io.Fonts->Flags | ImFontAtlasFlags_NoBakedLines;

        if (ft == FT_Normal)
        {
            auto font = family.Files.Files[FT_Normal].empty() ? nullptr :
                io.Fonts->AddFontFromFileTTF(family.Files.Files[FT_Normal].data(), size, &config);
            assert(font != nullptr);
            family.Fonts[FT_Normal][size] = font;
        }
        else
        {
            auto fallback = family.Fonts[FT_Normal][size];

#ifdef IMGUI_ENABLE_FREETYPE
            auto configFlags = config.FontBuilderFlags;

            if (family.Files.Files[ft].empty()) {
                config.FontBuilderFlags = configFlags | flag;
                family.Fonts[ft][size] = io.Fonts->AddFontFromFileTTF(
                    family.Files.Files[FT_Normal].data(), size, &config);
            }
            else family.Fonts[ft][size] = io.Fonts->AddFontFromFileTTF(
                family.Files.Files[ft].data(), size, &config);
#else
            fonts[ft][size] = files.Files[ft].empty() ? fallback :
                io.Fonts->AddFontFromFileTTF(files.Files[ft].data(), size, &config);
#endif
        }
    }

    bool LoadFonts(std::string_view family, const FontCollectionFile& files, float size, ImFontConfig config)
    {
        ImGuiIO& io = ImGui::GetIO();
        FontStore[family].Files = files;

        auto& ffamily = FontStore[family];
        LoadFont(io, ffamily, FT_Normal, size, config, 0);

#ifdef IMGUI_ENABLE_FREETYPE
        LoadFont(io, ffamily, FT_Bold, size, config, ImGuiFreeTypeBuilderFlags_Bold);
        LoadFont(io, ffamily, FT_Italics, size, config, ImGuiFreeTypeBuilderFlags_Oblique);
        LoadFont(io, ffamily, FT_BoldItalics, size, config, ImGuiFreeTypeBuilderFlags_Bold | ImGuiFreeTypeBuilderFlags_Oblique);
#else
        LoadFont(io, ffamily, FT_Bold, size, config, 0);
        LoadFont(io, ffamily, FT_Italics, size, config, 0);
        LoadFont(io, ffamily, FT_BoldItalics, size, config, 0);
#endif
        LoadFont(io, ffamily, FT_Light, size, config, 0);
        return true;
    }

#endif
#ifdef IM_RICHTEXT_TARGET_BLEND2D
    static void CreateFont(FontFamily& family, FontType ft, float size)
    {
        auto& face = family.FontFace[ft];

        if (ft == FT_Normal)
        {
            auto& font = family.Fonts[FT_Normal][size];
            auto res = face.createFromFile(family.Files.Files[FT_Normal].c_str());
            res = res == BL_SUCCESS ? font.createFromFace(face, size) : res;
            assert(res == BL_SUCCESS);
        }
        else
        {
            const auto& fallback = family.Fonts[FT_Normal][size];

            if (!files.Files[ft].empty())
            {
                auto res = face.createFromFile(family.Files.Files[ft].c_str());
                if (res == BL_SUCCESS) res = family.Fonts[ft][size].createFromFace(face, size);
                else family.Fonts[ft][size] = fallback;

                if (res != BL_SUCCESS) family.Fonts[ft][size] = fallback;
            }
            else
                family.Fonts[ft][size] = fallback;
        }
    }

    bool LoadFonts(std::string_view family, const FontCollectionFile& files, float size)
    {
        auto& ffamily = FontStore[family];
        ffamily.Files = files;
        CreateFont(ffamily, FT_Normal, size);
        CreateFont(ffamily, FT_Light, size);
        CreateFont(ffamily, FT_Bold, size);
        CreateFont(ffamily, FT_Italics, size);
        CreateFont(ffamily, FT_BoldItalics, size);
    }
#endif

#ifdef IM_RICHTEXT_TARGET_IMGUI
    static void LoadDefaultProportionalFont(float sz, const ImFontConfig& fconfig)
    {
#ifdef _WIN32
        LoadFonts(IM_RICHTEXT_DEFAULT_FONTFAMILY, {
            "c:\\Windows\\Fonts\\segoeui.ttf",
            "c:\\Windows\\Fonts\\segoeuil.ttf",
            "c:\\Windows\\Fonts\\segoeuib.ttf",
            "c:\\Windows\\Fonts\\segoeuii.ttf",
            "c:\\Windows\\Fonts\\segoeuiz.ttf"
            }, sz, fconfig);
#endif
        // TODO: Add default fonts for other platforms
    }

    static void LoadDefaultMonospaceFont(float sz, const ImFontConfig& fconfig)
    {
#ifdef _WIN32
        LoadFonts(IM_RICHTEXT_MONOSPACE_FONTFAMILY, {
            "c:\\Windows\\Fonts\\consola.ttf",
            "",
            "c:\\Windows\\Fonts\\consolab.ttf",
            "c:\\Windows\\Fonts\\consolai.ttf",
            "c:\\Windows\\Fonts\\consolaz.ttf"
            }, sz, fconfig);
#endif
        // TODO: Add default fonts for other platforms
    }
#endif

#ifdef IM_RICHTEXT_TARGET_BLEND2D
    static void LoadDefaultProportionalFont(float sz)
    {
#ifdef _WIN32
        LoadFonts(IM_RICHTEXT_DEFAULT_FONTFAMILY, {
            "c:\\Windows\\Fonts\\segoeui.ttf",
            "c:\\Windows\\Fonts\\segoeuil.ttf",
            "c:\\Windows\\Fonts\\segoeuib.ttf",
            "c:\\Windows\\Fonts\\segoeuii.ttf",
            "c:\\Windows\\Fonts\\segoeuiz.ttf"
            }, sz);
#endif
        // TODO: Add default fonts for other platforms
    }

    static void LoadDefaultMonospaceFont(float sz)
    {
#ifdef _WIN32
        LoadFonts(IM_RICHTEXT_MONOSPACE_FONTFAMILY, {
            "c:\\Windows\\Fonts\\consola.ttf",
            "",
            "c:\\Windows\\Fonts\\consolab.ttf",
            "c:\\Windows\\Fonts\\consolai.ttf",
            "c:\\Windows\\Fonts\\consolaz.ttf"
            }, sz);
#endif
        // TODO: Add default fonts for other platforms
    }
#endif

    bool LoadDefaultFonts(float sz, FontFileNames* names)
    {
#ifdef IM_RICHTEXT_TARGET_IMGUI
        ImFontConfig fconfig;
        fconfig.OversampleH = 2;
        fconfig.OversampleV = 1;
        fconfig.RasterizerMultiply = sz <= 16.f ? 2.f : 1.f;
#ifdef IMGUI_ENABLE_FREETYPE
        fconfig.FontBuilderFlags = ImGuiFreeTypeBuilderFlags_LightHinting;
#endif
#endif

        auto copyFileName = [](const std::string_view fontname, char* fontpath, int startidx) {
            auto sz = std::min((int)fontname.size(), _MAX_PATH - startidx);
            std::memcpy(fontpath + startidx, fontname.data(), sz);
            fontpath[startidx + sz] = 0;
            return fontpath;
        };

        if (names == nullptr)
        {
#ifdef IM_RICHTEXT_TARGET_IMGUI
            LoadDefaultProportionalFont(sz, fconfig);
            LoadDefaultMonospaceFont(sz, fconfig);
#endif
#ifdef IM_RICHTEXT_TARGET_BLEND2D
            LoadDefaultProportionalFont(sz);
            LoadDefaultMonospaceFont(sz);
#endif
        }
        else
        {
#if defined(_WIN32)
            char fontpath[_MAX_PATH] = "c:\\Windows\\Fonts\\";
#elif __APPLE__
            char fontpath[_MAX_PATH] = "/Library/Fonts/";
#elif __linux__
            char fontpath[_MAX_PATH] = "/usr/share/fonts/truetype/";
#else
#error "Platform unspported..."
#endif

            if (!names->BasePath.empty())
            {
                std::memset(fontpath, 0, _MAX_PATH);
                auto sz = std::min((int)names->BasePath.size(), _MAX_PATH);
                strncpy_s(fontpath, names->BasePath.data(), sz);
                fontpath[sz] = '\0';
            }

            const int startidx = (int)std::strlen(fontpath);
            FontCollectionFile files;

            if (!names->Proportional.Files[FT_Normal].empty())
            {
                files.Files[FT_Normal] = copyFileName(names->Proportional.Files[FT_Normal], fontpath, startidx);
                files.Files[FT_Light] = copyFileName(names->Proportional.Files[FT_Light], fontpath, startidx);
                files.Files[FT_Bold] = copyFileName(names->Proportional.Files[FT_Bold], fontpath, startidx);
                files.Files[FT_Italics] = copyFileName(names->Proportional.Files[FT_Italics], fontpath, startidx);
                files.Files[FT_BoldItalics] = copyFileName(names->Proportional.Files[FT_BoldItalics], fontpath, startidx);
#ifdef IM_RICHTEXT_TARGET_IMGUI
                LoadFonts(IM_RICHTEXT_DEFAULT_FONTFAMILY, files, sz, fconfig);
#endif
#ifdef IM_RICHTEXT_TARGET_BLEND2D
                LoadFonts(IM_RICHTEXT_DEFAULT_FONTFAMILY, files, sz);
#endif
            }
            else
            {
#ifdef IM_RICHTEXT_TARGET_IMGUI
                LoadDefaultProportionalFont(sz, fconfig);
#endif
#ifdef IM_RICHTEXT_TARGET_BLEND2D
                LoadDefaultProportionalFont(sz);
#endif
            }

            if (!names->Monospace.Files[FT_Normal].empty())
            {
                files.Files[FT_Normal] = copyFileName(names->Monospace.Files[FT_Normal], fontpath, startidx);
                files.Files[FT_Bold] = copyFileName(names->Monospace.Files[FT_Bold], fontpath, startidx);
                files.Files[FT_Italics] = copyFileName(names->Monospace.Files[FT_Italics], fontpath, startidx);
                files.Files[FT_BoldItalics] = copyFileName(names->Monospace.Files[FT_BoldItalics], fontpath, startidx);
#ifdef IM_RICHTEXT_TARGET_IMGUI
                LoadFonts(IM_RICHTEXT_MONOSPACE_FONTFAMILY, files, sz, fconfig);
#endif
#ifdef IM_RICHTEXT_TARGET_BLEND2D
                LoadFonts(IM_RICHTEXT_MONOSPACE_FONTFAMILY, files, sz);
#endif
            }
            else
            {
#ifdef IM_RICHTEXT_TARGET_IMGUI
                LoadDefaultMonospaceFont(sz, fconfig);
#endif
#ifdef IM_RICHTEXT_TARGET_BLEND2D
                LoadDefaultMonospaceFont(sz);
#endif
            }
        }

        return true;
    }

    bool LoadDefaultFonts(const std::initializer_list<float>& szs, FontFileNames* names)
    {
        for (auto sz : szs)
        {
            LoadDefaultFonts(sz, names);
        }

#ifdef IM_RICHTEXT_TARGET_IMGUI
        ImGui::GetIO().Fonts->Build();
#endif
        return true;
    }

    bool LoadDefaultFonts(const RenderConfig& config)
    {
        // TODO: Handle absolute size font-size fonts (Look at imrichtext.cpp: PopulateSegmentStyle function)
        std::unordered_set<float> sizes;
        sizes.insert(config.DefaultFontSize * config.FontScale);
        sizes.insert(config.DefaultFontSize * config.ScaleSubscript * config.FontScale);
        sizes.insert(config.DefaultFontSize * config.ScaleSuperscript * config.FontScale);
        sizes.insert(config.DefaultFontSize * 0.8f * config.FontScale); // for <small> content
        for (auto sz : config.HFontSizes) sizes.insert(sz * config.FontScale);

        for (auto sz : sizes)
        {
            LoadDefaultFonts(sz, nullptr);
        }

#ifdef IM_RICHTEXT_TARGET_IMGUI
        ImGui::GetIO().Fonts->Build();
#endif
        return true;
    }

    [[nodiscard]] auto LookupFontFamily(std::string_view family)
    {
        auto famit = FontStore.find(family);

        if (famit == FontStore.end())
        {
            for (auto it = FontStore.begin(); it != FontStore.end(); ++it)
            {
                if (it->first.find(family) == 0u ||
                    family.find(it->first) == 0u)
                {
                    return it;
                }
            }
        }

        if (famit == FontStore.end())
            famit = FontStore.find(IM_RICHTEXT_DEFAULT_FONTFAMILY);

        return famit;
    }

    void* GetFont(std::string_view family, float size, FontType ft, void*)
    {
        auto famit = LookupFontFamily(family);
        const auto& fonts = famit->second.Fonts[ft];
        auto szit = fonts.find(size);

        if (szit == fonts.end() && !fonts.empty())
        {
            szit = fonts.lower_bound(size);
            szit = szit == fonts.begin() ? szit : std::prev(szit);
        }

        return szit->second;
    }

    void* GetOverlayFont(const RenderConfig& config)
    {
        auto it = LookupFontFamily(IM_RICHTEXT_DEFAULT_FONTFAMILY);
        auto fontsz = config.DefaultFontSize * 0.8f * config.FontScale;
        return it->second.Fonts->lower_bound(fontsz)->second;
    }
}