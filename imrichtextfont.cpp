#include "imrichtextfont.h"
#include "imrichtext.h"
#include "misc/freetype/imgui_freetype.h"

#include <unordered_set>
#include <unordered_map>
#include <map>

namespace ImRichText
{
    struct FontCollection
    {
        ImFont* Normal = nullptr;
        ImFont* Light = nullptr;
        ImFont* Bold = nullptr;
        ImFont* Italics = nullptr;
        ImFont* BoldItalics = nullptr;

        FontCollectionFile Files;
    };

    static std::unordered_map<std::string_view, std::map<float, FontCollection>> FontStore;

    bool LoadFonts(std::string_view family, const FontCollectionFile& files, float size, ImFontConfig config)
    {
        ImGuiIO& io = ImGui::GetIO();
        auto& fonts = FontStore[family][size];
        fonts.Files = files;

        fonts.Normal = files.Normal.empty() ? nullptr : io.Fonts->AddFontFromFileTTF(files.Normal.data(), size, &config);
        assert(fonts.Normal != nullptr);

#ifdef IMGUI_ENABLE_FREETYPE
        auto configFlags = config.FontBuilderFlags;

        if (files.Bold.empty()) { 
            config.FontBuilderFlags = configFlags | ImGuiFreeTypeBuilderFlags_Bold;
            fonts.Bold = io.Fonts->AddFontFromFileTTF(files.Normal.data(), size, &config);
        }
        else fonts.Bold = io.Fonts->AddFontFromFileTTF(files.Bold.data(), size, &config);

        if (files.Italics.empty()) {
            config.FontBuilderFlags = configFlags | ImGuiFreeTypeBuilderFlags_Oblique;
            fonts.Italics = io.Fonts->AddFontFromFileTTF(files.Normal.data(), size, &config);
        }
        else fonts.Italics = io.Fonts->AddFontFromFileTTF(files.Italics.data(), size, &config);

        if (files.BoldItalics.empty()) {
            config.FontBuilderFlags = configFlags | ImGuiFreeTypeBuilderFlags_Oblique | ImGuiFreeTypeBuilderFlags_Bold;
            fonts.Italics = io.Fonts->AddFontFromFileTTF(files.Normal.data(), size, &config);
        }
        else fonts.BoldItalics = io.Fonts->AddFontFromFileTTF(files.BoldItalics.data(), size, &config);
#else
        
        fonts.Bold = files.Bold.empty() ? fonts.Normal : io.Fonts->AddFontFromFileTTF(files.Bold.data(), size, &config);
        fonts.Italics = files.Italics.empty() ? fonts.Normal : io.Fonts->AddFontFromFileTTF(files.Italics.data(), size, &config);
        fonts.BoldItalics = files.BoldItalics.empty() ? fonts.Normal : io.Fonts->AddFontFromFileTTF(files.BoldItalics.data(), size, &config);
#endif
        fonts.Light = files.Light.empty() ? fonts.Normal : io.Fonts->AddFontFromFileTTF(files.Light.data(), size, &config);
        return true;
    }

    void LoadDefaultProportionalFont(float sz, const ImFontConfig& fconfig)
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

    void LoadDefaultMonospaceFont(float sz, const ImFontConfig& fconfig)
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

    bool LoadDefaultFonts(float sz, FontFileNames* names)
    {
        ImFontConfig fconfig;
        fconfig.OversampleH = 3.0;
        fconfig.OversampleV = 1.0;
        fconfig.RasterizerMultiply = sz <= 16.f ? 2.f : 1.f;
#ifdef IMGUI_ENABLE_FREETYPE
        fconfig.FontBuilderFlags = ImGuiFreeTypeBuilderFlags_LightHinting;
#endif

        auto copyFileName = [](const std::string_view fontname, char* fontpath, int startidx) {
            auto sz = std::min((int)fontname.size(), _MAX_PATH - startidx);
            std::memcpy(fontpath + startidx, fontname.data(), sz);
            fontpath[startidx + sz] = 0;
            return fontpath;
        };

        if (names == nullptr)
        {
            LoadDefaultProportionalFont(sz, fconfig);
            LoadDefaultMonospaceFont(sz, fconfig);
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

            if (!names->Proportional.Normal.empty())
            {
                files.Normal = copyFileName(names->Proportional.Normal, fontpath, startidx);
                files.Light = copyFileName(names->Proportional.Light, fontpath, startidx);
                files.Bold = copyFileName(names->Proportional.Bold, fontpath, startidx);
                files.Italics = copyFileName(names->Proportional.Italics, fontpath, startidx);
                files.BoldItalics = copyFileName(names->Proportional.BoldItalics, fontpath, startidx);
                LoadFonts(IM_RICHTEXT_DEFAULT_FONTFAMILY, files, sz, fconfig);
            }
            else
            {
                LoadDefaultProportionalFont(sz, fconfig);
            }

            if (!names->Monospace.Normal.empty())
            {
                files.Normal = copyFileName(names->Monospace.Normal, fontpath, startidx);
                files.Bold = copyFileName(names->Monospace.Bold, fontpath, startidx);
                files.Italics = copyFileName(names->Monospace.Italics, fontpath, startidx);
                files.BoldItalics = copyFileName(names->Monospace.BoldItalics, fontpath, startidx);
                LoadFonts(IM_RICHTEXT_MONOSPACE_FONTFAMILY, files, sz, fconfig);
            }
            else
            {
                LoadDefaultMonospaceFont(sz, fconfig);
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

        ImGui::GetIO().Fonts->Build();
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

        ImGui::GetIO().Fonts->Build();
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

    ImFont* GetFont(std::string_view family, float size, bool bold, bool italics, bool light, void*)
    {
        auto famit = LookupFontFamily(family);
        auto szit = famit->second.find(size);

        if (szit == famit->second.end() && !famit->second.empty())
        {
            szit = famit->second.lower_bound(size);
            szit = szit == famit->second.begin() ? szit : std::prev(szit);
        }

        if (bold && italics) return szit->second.BoldItalics;
        else if (bold) return szit->second.Bold;
        else if (italics) return szit->second.Italics;
        else if (light) return szit->second.Light;
        else return szit->second.Normal;
    }

    ImFont* GetOverlayFont()
    {
        auto it = LookupFontFamily(IM_RICHTEXT_DEFAULT_FONTFAMILY);
        return it->second.begin()->second.Bold;
    }
}