#include "ImRichText.h"

#include <unordered_map>
#include <map>
#include <cstring>
#include <cctype>
#include <optional>
#include <cstdlib>

#ifndef IM_RICHTEXT_MAXDEPTH
#define IM_RICHTEXT_MAXDEPTH 256
#endif

#ifdef _DEBUG
#include <cstdio>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#define ERROR(FMT, ...) { \
    CONSOLE_SCREEN_BUFFER_INFO cbinfo; \
    auto h = GetStdHandle(STD_ERROR_HANDLE); \
    GetConsoleScreenBufferInfo(h, &cbinfo); \
    SetConsoleTextAttribute(h, FOREGROUND_RED | FOREGROUND_INTENSITY); \
    std::fprintf(stderr, FMT, __VA_ARGS__); \
    SetConsoleTextAttribute(h, cbinfo.wAttributes); }
#endif
#define LOG(FMT, ...)  std::fprintf(stdout, "%.*s" FMT, CurrentStackPos+1, TabLine, __VA_ARGS__)
#define TabLine "\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t"
#else
#define ERROR(FMT, ...)
#define LOG(FMT, ...)
#endif

#ifndef IM_RICHTEXT_MAXTABSTOP
#define IM_RICHTEXT_MAXTABSTOP 32
#endif

namespace ImRichText
{
#pragma optimize( "", on )
    [[nodiscard]] int SkipSpace(const char* text, int idx, int end)
    {
        while ((idx < end) && std::isspace(text[idx])) idx++;
        return idx;
    }

    [[nodiscard]] int SkipDigits(const char* text, int idx, int end)
    {
        while ((idx < end) && std::isdigit(text[idx])) idx++;
        return idx;
    }

    [[nodiscard]] int WholeWord(const char* text, int idx, int end)
    {
        while ((idx < end) && !std::isspace(text[idx])) idx++;
        return idx;
    }

    [[nodiscard]] int SkipSpace(const std::string_view text, int from = 0)
    {
        auto end = (int)text.size();
        while ((from < end) && (std::isspace(text[from]))) from++;
        return from;
    }

    [[nodiscard]] int WholeWord(const std::string_view text, int from = 0)
    {
        auto end = (int)text.size();
        while ((from < end) && (!std::isspace(text[from]))) from++;
        return from;
    }

    [[nodiscard]] int SkipDigits(const std::string_view text, int from = 0)
    {
        auto end = (int)text.size();
        while ((from < end) && (std::isdigit(text[from]))) from++;
        return from;
    }
#pragma optimize( "", off )

    struct FontCollection
    {
        ImFont* Normal = nullptr;
        ImFont* Light = nullptr;
        ImFont* Bold = nullptr;
        ImFont* Italics = nullptr;
        ImFont* BoldItalics = nullptr;

        FontCollectionFile Files;
    };

    static thread_local std::pair<std::string_view, SegmentStyle> TagStack[IM_RICHTEXT_MAXDEPTH];
    static thread_local int CurrentStackPos = -1;

    static std::unordered_map<std::string_view, std::map<float, FontCollection>> FontStore;
    static std::unordered_map<ImGuiContext*, std::deque<RenderConfig>> RenderConfigs;
    static RenderConfig DefaultRenderConfig;
    static bool DefaultRenderConfigInit = false;

    static const std::pair<std::string_view, std::string_view> EscapeCodes[10] = {
        { "Tab", "\t" }, { "NewLine", "\n" }, { "nbsp", " " }, 
        { "gt", ">" }, { "lt", "<" }, 
        { "amp", "&" }, { "copy", "©" }, { "reg", "®" }, 
        { "deg", "°" }, { "micro", "μ" }
    };

    static const char* LineSpaces = "                                ";

    template <typename T>
    [[nodiscard]] T clamp(T val, T min, T max)
    {
        return val < min ? min : val > max ? max : val;
    }

    [[nodiscard]] bool AreSame(const std::string_view lhs, const char* rhs)
    {
        auto rlimit = (int)std::strlen(rhs);
        auto llimit = (int)lhs.size();
        if (rlimit != llimit)
            return false;

        for (auto idx = 0; idx < rlimit; ++idx)
            if (std::tolower(lhs[idx]) != std::tolower(rhs[idx]))
                return false;

        return true;
    }

    [[nodiscard]] bool AreSame(const std::string_view lhs, const std::string_view rhs)
    {
        auto rlimit = (int)rhs.size();
        auto llimit = (int)lhs.size();
        if (rlimit != llimit)
            return false;

        for (auto idx = 0; idx < rlimit; ++idx)
            if (std::tolower(lhs[idx]) != std::tolower(rhs[idx]))
                return false;

        return true;
    }

#ifdef _DEBUG
    [[nodiscard]] const char* GetTokenTypeString(const Token& token)
    {
        switch (token.Type)
        {
        case TokenType::Text: return "Text";
        case TokenType::HorizontalRule: return "HorizontalRule";
        case TokenType::ListEnd: return "ListEnd";
        case TokenType::ListStart: return "ListStart";
        case TokenType::ListItem: return "ListItem";
        case TokenType::ParagraphStart: return "ParagraphStart";
        case TokenType::Subscript: return "Subscript";
        case TokenType::Superscript: return "Superscript";
        case TokenType::BlockquoteStart: return "BlockquoteStart";
        case TokenType::BlockquoteEnd: return "BlockquoteEnd";
        default: return "InvalidToken";
        }
    }
#endif

    [[nodiscard]] int ExtractInt(const char* text, int end, int defaultVal)
    {
        int result = defaultVal;
        int base = 1;

        for (auto idx = end - 1; idx >= 0; --idx)
        {
            result += (text[idx] - '0') * base;
            base *= 10;
        }

        return result;
    }

    [[nodiscard]] int ExtractInt(std::string_view input, int defaultVal)
    {
        int result = defaultVal;
        int base = 1;

        for (auto idx = (int)input.size() - 1; idx >= 0; --idx)
        {
            result += (input[idx] - '0') * base;
            base *= 10;
        }

        return result;
    }

    [[nodiscard]] ImColor ExtractColor(std::string_view stylePropVal, ImColor(*NamedColor)(const char*, void*), void* userData)
    {
        if (stylePropVal[0] == 'r' && stylePropVal[1] == 'g' && stylePropVal[2] == 'b')
        {
            int r = 0, g = 0, b = 0, a = 255;
            auto hasAlpha = stylePropVal[3] == 'a';
            int curr = hasAlpha ? 4 : 3;
            curr = SkipSpace(stylePropVal, curr);
            assert(stylePropVal[curr] == '(');
            curr++;

            auto valstart = curr;
            curr = SkipDigits(stylePropVal, curr);
            r = ExtractInt(stylePropVal.data() + valstart, curr - valstart, 0);
            curr = SkipSpace(stylePropVal, curr);
            assert(stylePropVal[curr] == ',');
            curr++;
            curr = SkipSpace(stylePropVal, curr);

            valstart = curr;
            curr = SkipDigits(stylePropVal, curr);
            g = ExtractInt(stylePropVal.data() + valstart, curr - valstart, 0);
            curr = SkipSpace(stylePropVal, curr);
            assert(stylePropVal[curr] == ',');
            curr++;
            curr = SkipSpace(stylePropVal, curr);

            valstart = curr;
            curr = SkipDigits(stylePropVal, curr);
            b = ExtractInt(stylePropVal.data() + valstart, curr - valstart, 0);
            curr = SkipSpace(stylePropVal, curr);

            if (hasAlpha)
            {
                assert(stylePropVal[curr] == ',');
                curr++;
                curr = SkipSpace(stylePropVal, curr);

                valstart = curr;
                curr = SkipDigits(stylePropVal, curr);
                a = ExtractInt(stylePropVal.data() + valstart, curr - valstart, 0);
            }

            assert(stylePropVal[curr] == ')');

            return IM_COL32(r, g, b, a);
        }
        else if (NamedColor != nullptr)
        {
            static char buffer[32] = { 0 };
            std::memset(buffer, 0, 32);
            std::memcpy(buffer, stylePropVal.data(), std::min((int)stylePropVal.size(), 31));
            return NamedColor(buffer, userData);
        }
        else
        {
            return IM_COL32_BLACK;
        }
    }

    std::pair<ImColor, int> GetBorderColorAndThickness(std::string_view stylePropVal, const RenderConfig& config)
    {
        std::pair<ImColor, int> result;
        auto idx = SkipDigits(stylePropVal);
        result.second = ExtractInt(stylePropVal.substr(0u, idx), 0);

        idx = WholeWord(stylePropVal, idx);
        idx = SkipSpace(stylePropVal, idx);
        idx = WholeWord(stylePropVal, idx);
        idx = SkipSpace(stylePropVal, idx);
        // TODO: Obey line Type in border

        auto colstart = idx;
        idx = WholeWord(stylePropVal, idx);
        result.first = ExtractColor(stylePropVal.substr(colstart, idx),
            config.NamedColor, config.UserData);

        return result;
    }

    void PopulateSegmentStyle(SegmentStyle& el,
        std::string_view stylePropName,
        std::string_view stylePropVal,
        const RenderConfig& config)
    {
        if (AreSame(stylePropName, "font-size"))
        {
            auto idx = SkipDigits(stylePropVal);
            el.font.size = ExtractInt(stylePropVal.substr(0u, idx), config.DefaultFontSize);
            el.font.size = clamp(el.font.size, 1.f, (float)(1 << 14));

        }
        else if (AreSame(stylePropName, "font-weight"))
        {
            auto idx = SkipDigits(stylePropVal);

            if (idx == 0)
            {
                if (AreSame(stylePropVal, "bold")) el.font.bold = true;
                else if (AreSame(stylePropVal, "light")) el.font.light = true;
                else ERROR("Invalid font-weight property value... [%.*s]\n",
                    (int)stylePropVal.size(), stylePropVal.data());
            }
            else
            {
                int weight = ExtractInt(stylePropVal.substr(0u, idx), 400);
                el.font.bold = weight >= 600;
                el.font.light = weight < 400;
            }
        }
        else if (AreSame(stylePropName, "background-color") || AreSame(stylePropName, "background"))
        {
            el.bgcolor = ExtractColor(stylePropVal, config.NamedColor, config.UserData);
        }
        else if (AreSame(stylePropName, "color"))
        {
            el.fgcolor = ExtractColor(stylePropVal, config.NamedColor, config.UserData);
        }
        else if (AreSame(stylePropName, "width"))
        {
            el.width = ExtractInt(stylePropVal, 0);
        }
        else if (AreSame(stylePropName, "height"))
        {
            el.height = ExtractInt(stylePropVal, 0);
        }
        else if (AreSame(stylePropName, "border"))
        {
            auto [color, thickness] = GetBorderColorAndThickness(stylePropVal, config);

            for (auto corner = 0; corner < 4; ++corner)
            {
                el.border[corner].thickness = thickness;
                el.border[corner].color = color;
            }
        }
        else if (AreSame(stylePropName, "border-top"))
        {
            std::tie(el.border[TopSide].color, el.border[TopSide].thickness) = 
                GetBorderColorAndThickness(stylePropVal, config);
        }
        else if (AreSame(stylePropName, "border-right"))
        {
            std::tie(el.border[RightSide].color, el.border[RightSide].thickness) =
                GetBorderColorAndThickness(stylePropVal, config);
        }
        else if (AreSame(stylePropName, "border-bottom"))
        {
            std::tie(el.border[BottomSide].color, el.border[BottomSide].thickness) =
                GetBorderColorAndThickness(stylePropVal, config);
        }
        else if (AreSame(stylePropName, "border-left"))
        {
            std::tie(el.border[LeftSide].color, el.border[LeftSide].thickness) =
                GetBorderColorAndThickness(stylePropVal, config);
        }
        else if (AreSame(stylePropName, "alignment") || AreSame(stylePropName, "text-align"))
        {
            el.alignmentH = AreSame(stylePropVal, "justify") ? HorizontalAlignment::Justify :
                AreSame(stylePropVal, "right") ? HorizontalAlignment::Right :
                AreSame(stylePropVal, "center") ? HorizontalAlignment::Center :
                HorizontalAlignment::Left;
        }
        else if (AreSame(stylePropName, "vertical-align"))
        {
            el.alignmentV = AreSame(stylePropVal, "top") ? VerticalAlignment::Top :
                AreSame(stylePropVal, "bottom") ? VerticalAlignment::Bottom :
                VerticalAlignment::Center;
        }
        else if (AreSame(stylePropName, "font-family"))
        {
            el.font.family = stylePropVal;
        }
        else if (AreSame(stylePropName, "font-style"))
        {
            if (AreSame(stylePropVal, "normal")) el.font.italics = false;
            else if (AreSame(stylePropVal, "italic") || AreSame(stylePropVal, "oblique"))
                el.font.italics = true;
            else ERROR("Invalid font-style property value [%.*s]\n",
                (int)stylePropVal.size(), stylePropVal.data());
        }
        else if (AreSame(stylePropName, "list-style-type"))
        {
            if (AreSame(stylePropVal, "circle")) el.list.itemStyle = BulletType::Circle;
            else if (AreSame(stylePropVal, "disk")) el.list.itemStyle = BulletType::Disk;
            else if (AreSame(stylePropVal, "square")) el.list.itemStyle = BulletType::Square;
            else if (AreSame(stylePropVal, "solidsquare")) el.list.itemStyle = BulletType::FilledSquare;
            // TODO: Others...
        }
        else if (AreSame(stylePropName, "white-space"))
        {
            if (AreSame(stylePropVal, "pre")) el.renderAllWhitespace = true;
        }
        else if (AreSame(stylePropName, "border-radius"))
        {
            auto begin = 0, idx = 0;
            idx = SkipDigits(stylePropVal, idx);
            el.borderRoundedness[TopLeftCorner] = ExtractInt(stylePropVal.substr(begin, idx), 0);

            begin = idx;
            idx = SkipDigits(stylePropVal, idx);
            el.borderRoundedness[TopRightCorner] = ExtractInt(stylePropVal.substr(begin, idx), 0);

            begin = idx;
            idx = SkipDigits(stylePropVal, idx);
            el.borderRoundedness[BottomRightCorner] = ExtractInt(stylePropVal.substr(begin, idx), 0);

            begin = idx;
            idx = SkipDigits(stylePropVal, idx);
            el.borderRoundedness[BottomLeftCorner] = ExtractInt(stylePropVal.substr(begin, idx), 0);
        }
        else ERROR("Invalid style property... [%.*s]\n", (int)stylePropName.size(), stylePropName.data());
    }

    bool LoadFonts(std::string_view family, const FontCollectionFile& files, float size, const ImFontConfig& config)
    {
        ImGuiIO& io = ImGui::GetIO();
        auto& fonts = FontStore[family][size];
        fonts.Files = files;
        fonts.Normal = files.Normal.empty() ? nullptr : io.Fonts->AddFontFromFileTTF(files.Normal.data(), size, &config);
        assert(fonts.Normal != nullptr);

        fonts.Light = files.Light.empty() ? fonts.Normal : io.Fonts->AddFontFromFileTTF(files.Light.data(), size, &config);
        fonts.Bold = files.Bold.empty() ? fonts.Normal : io.Fonts->AddFontFromFileTTF(files.Bold.data(), size, &config);
        fonts.Italics = files.Italics.empty() ? fonts.Normal : io.Fonts->AddFontFromFileTTF(files.Italics.data(), size, &config);
        fonts.BoldItalics = files.BoldItalics.empty() ? fonts.Normal : io.Fonts->AddFontFromFileTTF(files.BoldItalics.data(), size, &config);
        return true;
    }

    bool LoadDefaultFonts(float sz, FontFileNames* names)
    {
        ImFontConfig fconfig;
        fconfig.OversampleH = 3.0;
        fconfig.OversampleV = 1.0;
        fconfig.RasterizerMultiply = sz < 32.f ? 1.5f : 1.0f;

        auto copyFileName = [](const std::string_view fontname, char* fontpath, int startidx) {
            auto sz = std::min((int)fontname.size(), _MAX_PATH - startidx);
            std::memcpy(fontpath + startidx, fontname.data(), sz);
            fontpath[startidx + sz] = 0;
            return fontpath;
        };

        if (names == nullptr)
        {
#ifdef _WIN32
            LoadFonts(IM_RICHTEXT_DEFAULT_FONTFAMILY, {
                "c:\\Windows\\Fonts\\segoeui.ttf",
                "c:\\Windows\\Fonts\\segoeuil.ttf",
                "c:\\Windows\\Fonts\\segoeuib.ttf",
                "c:\\Windows\\Fonts\\segoeuii.ttf",
                "c:\\Windows\\Fonts\\segoeuiz.ttf"
            }, sz, fconfig);

            LoadFonts(IM_RICH_TEXT_MONOSPACE_FONTFAMILY, { 
                "c:\\Windows\\Fonts\\consola.ttf", 
                "", 
                "c:\\Windows\\Fonts\\consolab.ttf", 
                "c:\\Windows\\Fonts\\consolai.ttf",
                "c:\\Windows\\Fonts\\consolaz.ttf" 
            }, sz, fconfig);
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
#ifdef _WIN32
                LoadFonts(IM_RICHTEXT_DEFAULT_FONTFAMILY, {
                    "c:\\Windows\\Fonts\\segoeui.ttf",
                    "c:\\Windows\\Fonts\\segoeuil.ttf",
                    "c:\\Windows\\Fonts\\segoeuib.ttf",
                    "c:\\Windows\\Fonts\\segoeuii.ttf",
                    "c:\\Windows\\Fonts\\segoeuiz.ttf"
                }, sz, fconfig);
#endif
            }

            if (!names->Monospace.Normal.empty())
            {
                files.Normal = copyFileName(names->Monospace.Normal, fontpath, startidx);
                files.Bold = copyFileName(names->Monospace.Bold, fontpath, startidx);
                files.Italics = copyFileName(names->Monospace.Italics, fontpath, startidx);
                files.BoldItalics = copyFileName(names->Monospace.BoldItalics, fontpath, startidx);
                LoadFonts(IM_RICH_TEXT_MONOSPACE_FONTFAMILY, files, sz, fconfig);
            }
            else
            {
#ifdef _WIN32
                LoadFonts(IM_RICH_TEXT_MONOSPACE_FONTFAMILY, {
                    "c:\\Windows\\Fonts\\consola.ttf",
                    "",
                    "c:\\Windows\\Fonts\\consolab.ttf",
                    "c:\\Windows\\Fonts\\consolai.ttf",
                    "c:\\Windows\\Fonts\\consolaz.ttf"
                }, sz, fconfig);
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

        return famit;
    }

    ImFont* GetFont(std::string_view family, float size, bool bold, bool italics, bool light, void*)
    {
        auto famit = LookupFontFamily(family);

        if (famit == FontStore.end())
        {
            return FontStore.at(IM_RICH_TEXT_MONOSPACE_FONTFAMILY).begin()->second.Normal;
        }
        else
        {

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
    }


    ImVec2 GetTextSize(std::string_view content, ImFont* font)
    {
        ImGui::PushFont(font);
        auto sz = ImGui::CalcTextSize(content.data(), content.data() + content.size());
        ImGui::PopFont();
        return sz;
    }

    template <int maxsz>
    struct CaseInsensitieHasher
    {
        std::size_t operator()(std::string_view key) const
        {
            thread_local static char buffer[maxsz] = { 0 };
            std::memset(buffer, 0, maxsz);
            auto limit = std::min((int)key.size(), maxsz - 1);
            
            for (auto idx = 0; idx < limit; ++idx)
                buffer[idx] = std::tolower(key[idx]);

            return std::hash<std::string_view>()(buffer);
        }
    };

    ImColor GetColor(const char* name, void*)
    {
        const static std::unordered_map<std::string_view, ImColor, CaseInsensitieHasher<32>> Colors{
            { "black", ImColor(0, 0, 0) },
            { "silver", ImColor(192, 192, 192) },
            { "gray", ImColor(128, 128, 128) },
            { "white", ImColor(255, 255, 255) },
            { "maroon", ImColor(128, 0, 0) },
            { "red", ImColor(255, 0, 0) },
            { "purple", ImColor(128, 0, 128) },
            { "fuchsia", ImColor(255, 0, 255) },
            { "green", ImColor(0, 128, 0) },
            { "lime", ImColor(0, 255, 0) },
            { "olive", ImColor(128, 128, 0) },
            { "yellow", ImColor(255, 255, 0) },
            { "navy", ImColor(0, 0, 128) },
            { "blue", ImColor(0, 0, 255) },
            { "teal", ImColor(0, 128, 128) },
            { "aqua", ImColor(0, 255, 255) },
            { "aliceblue", ImColor(240, 248, 255) },
            { "antiquewhite", ImColor(250, 235, 215) },
            { "aquamarine", ImColor(127, 255, 212) },
            { "azure", ImColor(240, 255, 255) },
            { "beige", ImColor(245, 245, 220) },
            { "bisque", ImColor(255, 228, 196) },
            { "blanchedalmond", ImColor(255, 235, 205) },
            { "blueviolet", ImColor(138, 43, 226) },
            { "brown", ImColor(165, 42, 42) },
            { "burlywood", ImColor(222, 184, 135) },
            { "cadetblue", ImColor(95, 158, 160) },
            { "chartreuse", ImColor(127, 255, 0) },
            { "chocolate", ImColor(210, 105, 30) },
            { "coral", ImColor(255, 127, 80) },
            { "cornflowerblue", ImColor(100, 149, 237) },
            { "cornsilk", ImColor(255, 248, 220) },
            { "crimson", ImColor(220, 20, 60) },
            { "darkblue", ImColor(0, 0, 139) },
            { "darkcyan", ImColor(0, 139, 139) },
            { "darkgoldenrod", ImColor(184, 134, 11) },
            { "darkgray", ImColor(169, 169, 169) },
            { "darkgreen", ImColor( 0, 100, 0) },
            { "darkgrey", ImColor(169, 169, 169) },
            { "darkkhaki", ImColor(189, 183, 107) },
            { "darkmagenta", ImColor(139, 0, 139) },
            { "darkolivegreen", ImColor(85, 107, 47) },
            { "darkorange", ImColor(255, 140, 0) },
            { "darkorchid", ImColor(153, 50, 204) },
            { "darkred", ImColor(139, 0, 0) },
            { "darksalmon", ImColor(233, 150, 122) },
            { "darkseagreen", ImColor(143, 188, 143) },
            { "darkslateblue", ImColor(72, 61, 139) },
            { "darkslategray", ImColor(47, 79, 79) },
            { "darkslategray", ImColor(47, 79, 79) },
            { "darkturquoise", ImColor(0, 206, 209) },
            { "darkviolet", ImColor(148, 0, 211) },
            { "deeppink", ImColor(255, 20, 147) },
            { "deepskyblue", ImColor(0, 191, 255) },
            { "dimgray", ImColor(105, 105, 105) },
            { "dimgrey", ImColor(105, 105, 105) },
            { "dodgerblue", ImColor(30, 144, 255) },
            { "firebrick", ImColor(178, 34, 34) },
            { "floralwhite", ImColor(255, 250, 240) },
            { "forestgreen", ImColor(34, 139, 34) },
            { "gainsboro", ImColor(220, 220, 220) },
            { "ghoshtwhite", ImColor(248, 248, 255) },
            { "gold", ImColor(255, 215, 0) },
            { "goldenrod", ImColor(218, 165, 32) },
            { "greenyellow", ImColor(173, 255, 47) },
            { "honeydew", ImColor(240, 255, 240) },
            { "hotpink", ImColor(255, 105, 180) },
            { "indianred", ImColor(205, 92, 92) },
            { "indigo", ImColor(75, 0, 130) },
            { "ivory", ImColor(255, 255, 240) },
            { "khaki", ImColor(240, 230, 140) },
            { "lavender", ImColor(230, 230, 250) },
            { "lavenderblush", ImColor(255, 240, 245) },
            { "lawngreen", ImColor(124, 252, 0) },
            { "lemonchiffon", ImColor(255, 250, 205) },
            { "lightblue", ImColor(173, 216, 230) },
            { "lightcoral", ImColor(240, 128, 128) },
            { "lightcyan", ImColor(224, 255, 255) },
            { "lightgoldenrodyellow", ImColor(250, 250, 210) },
            { "lightgray", ImColor(211, 211, 211) },
            { "lightgreen", ImColor(144, 238, 144) },
            { "lightgrey", ImColor(211, 211, 211) },
            { "lightpink", ImColor(255, 182, 193) },
            { "lightsalmon", ImColor(255, 160, 122) },
            { "lightseagreen", ImColor(32, 178, 170) },
            { "lightskyblue", ImColor(135, 206, 250) },
            { "lightslategray", ImColor(119, 136, 153) },
            { "lightslategrey", ImColor(119, 136, 153) },
            { "lightsteelblue", ImColor(176, 196, 222) },
            { "lightyellow", ImColor(255, 255, 224) },
            { "limegreen", ImColor(50, 255, 50) },
            { "linen", ImColor(250, 240, 230) },
            { "mediumaquamarine", ImColor(102, 205, 170) },
            { "mediumblue", ImColor(0, 0, 205) },
            { "mediumorchid", ImColor(186, 85, 211) },
            { "mediumpurple", ImColor(147, 112, 219) },
            { "mediumseagreen", ImColor(60, 179, 113) },
            { "mediumslateblue", ImColor(123, 104, 238) },
            { "mediumspringgreen", ImColor(0, 250, 154) },
            { "mediumturquoise", ImColor(72, 209, 204) },
            { "mediumvioletred", ImColor(199, 21, 133) },
            { "midnightblue", ImColor(25, 25, 112) },
            { "mintcream", ImColor(245, 255, 250) },
            { "mistyrose", ImColor(255, 228, 225) },
            { "moccasin", ImColor(255, 228, 181) },
            { "navajowhite", ImColor(255, 222, 173) },
            { "oldlace", ImColor(253, 245, 230) },
            { "olivedrab", ImColor(107, 142, 35) },
            { "orange", ImColor(255, 165, 0) },
            { "orangered", ImColor(255, 69, 0) },
            { "orchid", ImColor(218, 112, 214) },
            { "palegoldenrod", ImColor(238, 232, 170) },
            { "palegreen", ImColor(152, 251, 152) },
            { "paleturquoise", ImColor(175, 238, 238) },
            { "palevioletred", ImColor(219, 112, 147) },
            { "papayawhip", ImColor(255, 239, 213) },
            { "peachpuff", ImColor(255, 218, 185) },
            { "peru", ImColor(205, 133, 63) },
            { "pink", ImColor(255, 192, 203) },
            { "plum", ImColor(221, 160, 221) },
            { "powderblue", ImColor(176, 224, 230) },
            { "rosybrown", ImColor(188, 143, 143) },
            { "royalblue", ImColor(65, 105, 225) },
            { "saddlebrown", ImColor(139, 69, 19) },
            { "salmon", ImColor(250, 128, 114) },
            { "sandybrown", ImColor(244, 164, 96) },
            { "seagreen", ImColor(46, 139, 87) },
            { "seashell", ImColor(255, 245, 238) },
            { "sienna", ImColor(160, 82, 45) },
            { "skyblue", ImColor(135, 206, 235) },
            { "slateblue", ImColor(106, 90, 205) },
            { "slategray", ImColor(112, 128, 144) },
            { "slategrey", ImColor(112, 128, 144) },
            { "snow", ImColor(255, 250, 250) },
            { "springgreen", ImColor(0, 255, 127) },
            { "steelblue", ImColor(70, 130, 180) },
            { "tan", ImColor(210, 180, 140) },
            { "thistle", ImColor(216, 191, 216) },
            { "tomato", ImColor(255, 99, 71) },
            { "violet", ImColor(238, 130, 238) },
            { "wheat", ImColor(245, 222, 179) },
            { "whitesmoke", ImColor(245, 245, 245) },
            { "yellowgreen", ImColor(154, 205, 50) }
        }; 

        return Colors.at(name);
    }

    RenderConfig* GetDefaultConfig(ImVec2 Bounds)
    {
        auto config = &DefaultRenderConfig;
        config->Bounds = Bounds;
        config->GetFont = &GetFont;
        config->NamedColor = &GetColor;
        config->GetTextSize = &GetTextSize;

        config->EscapeCodes.insert(config->EscapeCodes.end(), std::begin(EscapeCodes),
            std::end(EscapeCodes));
        config->EscapeSeqStart = '&';
        config->EscapeSeqEnd = ';';
        DefaultRenderConfigInit = true;
        return config;
    }

    [[nodiscard]] std::optional<std::string_view> GetQuotedString(const char* text, int& idx, int end)
    {
        auto insideQuotedString = false;
        auto begin = idx;

        if ((idx < end) && text[idx] == '\'')
        {
            idx++;

            while (idx < end)
            {
                if (text[idx] == '\\' && ((idx + 1 < end) && text[idx + 1] == '\''))
                {
                    insideQuotedString = !insideQuotedString;
                    idx++;
                }
                else if (!insideQuotedString && text[idx] == '\'') break;
                idx++;
            }
        }
        else if ((idx < end) && text[idx] == '"')
        {
            idx++;

            while (idx < end)
            {
                if (text[idx] == '\\' && ((idx + 1 < end) && text[idx + 1] == '"'))
                {
                    insideQuotedString = !insideQuotedString;
                    idx++;
                }
                else if (!insideQuotedString && text[idx] == '"') break;
                idx++;
            }
        }

        if ((idx < end) && (text[idx] == '"' || text[idx] == '\''))
        {
            std::string_view res{ text + begin + 1, (std::size_t)(idx - begin - 1) };
            idx++;
            return res;
        }
        else if (idx > begin)
        {
            ERROR("Quoted string invalid... [%.*s] at %d\n", (int)(idx - begin), text + begin, idx);
        }

        return std::nullopt;
    }

    [[nodiscard]] SegmentStyle CreateDefaultStyle(const RenderConfig& config)
    {
        SegmentStyle result;
        result.font.family = config.DefaultFontFamily;
        result.font.size = config.DefaultFontSize;
        result.bgcolor = config.DefaultBgColor;
        result.fgcolor = config.DefaultFgColor;
        result.list.itemStyle = config.ListItemBullet;
        return result;
    }

    [[nodiscard]] std::pair<float, float> GetLineSize(const SegmentStyle& styleprops, const RenderConfig& config, SegmentDetails& segment)
    {
        auto borderv = styleprops.border[TopSide].thickness + styleprops.border[BottomSide].thickness;
        auto borderh = styleprops.border[LeftSide].thickness + styleprops.border[RightSide].thickness;

        if (!segment.HasText) return { styleprops.width + borderh, styleprops.height + borderv };
        else
        {
            float height = 0.f, width = 0.f;

            for (auto& token : segment.Tokens)
            {
                token.Size = ImGui::CalcTextSize(token.Content.data(), token.Content.data() + token.Content.size());
                height = std::max(height, token.Size.y);
                width = std::max(width, token.Size.x);
            }

            return { width + borderh, height + borderv };
        }
    }

    void AddToken(DrawableLine& line, const Token& token, const RenderConfig& config)
    {
        auto& segment = line.Segments.back();
        segment.Tokens.emplace_back(token);
        segment.HasText = segment.HasText || (!token.Content.empty());
        line.HasText = line.HasText || segment.HasText;

        LOG("Added token: %.*s [itemtype: %s]\n", 
            (int)token.Content.size(), token.Content.data(),
            GetTokenTypeString(token));
    }

    SegmentDetails& AddSegment(DrawableLine& line, const SegmentStyle& styleprops, const RenderConfig& config)
    {
        auto& segment = line.Segments.emplace_back();
        segment.Style = styleprops;
        return segment;
    }

    [[nodiscard]] DrawableLine CreateNewLine(const SegmentStyle& styleprops, const RenderConfig& config)
    {
        DrawableLine line;
        line.Segments.emplace_back();
        line.Segments.back().Style = styleprops;
        return line;
    }

    [[nodiscard]] DrawableLine MoveToNextLine(const SegmentStyle& styleprops, const DrawableLine& line,
        std::deque<DrawableLine>& result, const RenderConfig& config, bool wordwrap = false);

    void PerformWordWrap(std::deque<DrawableLine>& lines, int index, const RenderConfig& config)
    {
        if (!lines[index].HasText || !config.WordWrap || !config.GetTextSize) return;

        std::deque<DrawableLine> newlines;
        auto currline = CreateNewLine(lines.front().Segments.front().Style, config);
        auto currentx = 0.f;

        for (const auto& segment : lines[index].Segments)
        {
            for (const auto& token : segment.Tokens)
            {
                if (token.Type == TokenType::Text)
                {
                    auto sz = config.GetTextSize(token.Content, segment.Style.font.font);

                    if (currentx + sz.x > config.Bounds.x)
                    {
                        currline = MoveToNextLine(segment.Style, currline, newlines, config);
                        currentx = 0.f;
                    }

                    currline.Segments.back().Style = segment.Style;
                    currline.Segments.back().Tokens.emplace_back(token);
                    currentx += sz.x;
                }
            }
        }

        auto it = lines.erase(lines.begin() + index);
        lines.insert(it, newlines.begin(), newlines.end());
    }

    DrawableLine MoveToNextLine(const SegmentStyle& styleprops, const DrawableLine& line, 
        std::deque<DrawableLine>& result, const RenderConfig& config, bool wordwrap)
    {
        result.push_back(line);
        if (wordwrap) PerformWordWrap(result, (int)result.size() - 1, config);
        return CreateNewLine(styleprops, config);
    }

    std::pair<float, float> GetLineSize(DrawableLine& line, const RenderConfig& config)
    {
        auto height = 0.f, width = 0.f;

        for (auto& segment : line.Segments)
        {
            segment.Style.font.font = config.GetFont(segment.Style.font.family,
                segment.Style.font.size, segment.Style.font.bold, segment.Style.font.italics,
                segment.Style.font.light, config.UserData);
            ImGui::PushFont(segment.Style.font.font);
            auto sz = GetLineSize(segment.Style, config, segment);
            height = std::max(height, sz.second);
            width = std::max(width, sz.first);
            ImGui::PopFont();
        }

        return { width, height };
    }

    void GenerateToken(DrawableLine& line, std::string_view content, 
        int begin, int curridx, int start, const RenderConfig& config, 
        bool& paragraphBeginning, bool& listItemBeginning)
    {
        if (listItemBeginning)
        {
            listItemBeginning = false;
            Token token;
            token.Type = TokenType::ListItem;
            AddToken(line, token, config);
        }

        if (paragraphBeginning && config.ParagraphStop > 0)
        {
            paragraphBeginning = false;
            Token token;
            token.Type = TokenType::ParagraphStart;
            AddToken(line, token, config);
        }

        Token token;
        token.Extent = std::make_pair(begin + start, begin + curridx);
        token.Content = content.substr(start, curridx - start);
        AddToken(line, token, config);
    }

    [[nodiscard]] std::pair<bool, bool> AddEscapeSequences(const std::string_view content, 
        int curridx, const std::vector<std::pair<std::string_view, std::string_view>>& escapeCodes, 
        char escapeStart, char escapeEnd, DrawableLine& line, int& start)
    {
        Token token;
        auto hasEscape = false;
        auto isNewLine = false;

        for (const auto& pair : escapeCodes)
        {
            auto sz = (int)pair.first.size();
            if ((curridx + sz) < (int)content.size() &&
                AreSame(content.substr(curridx, sz), pair.first) &&
                content[curridx + sz] == escapeEnd)
            {
                if (pair.second == "\n") isNewLine = true;
                else
                {
                    token.Extent = std::make_pair(-1, -1);
                    token.Content = pair.second;
                    line.Segments.back().Tokens.emplace_back(token);
                }
                
                curridx += sz + 1;
                start = curridx;
                hasEscape = true;
                break;
            }
        }

        return { hasEscape, isNewLine };
    }

    std::deque<DrawableLine> GetDrawableLines(const char* text, int start, const int end, RenderConfig& config)
    {
        std::deque<DrawableLine> result;
        start = SkipSpace(text, start, end);
        auto initialStyle = CreateDefaultStyle(config);
        std::string_view currTag;

        thread_local char TabSpaces[IM_RICHTEXT_MAXTABSTOP] = { 0 };
        std::memset(TabSpaces, ' ', clamp(config.TabStop, 0, IM_RICHTEXT_MAXTABSTOP-1));
        TabSpaces[config.TabStop] = 0;

        DrawableLine line = CreateNewLine(initialStyle, config);
        auto paragraphBegin = false, listItemBegin = false;

        for (auto idx = start; idx < end;)
        {
            if (text[idx] == config.TagStart)
            {
                idx++;
                auto tagStart = true;
                
                if (text[idx] == '/')
                {
                    tagStart = false;
                    idx++;
                }
                else if (!std::isalnum(text[idx]))
                {
                    ERROR("Invalid tag at %d...\n", idx);
                    return result;
                }

                auto begin = idx;
                while ((idx < end) && !std::isspace(text[idx]) && (text[idx] != config.TagEnd)) idx++;

                if (idx - begin == 0)
                {
                    ERROR("Empty tag at %d...\n", begin);
                    return result;
                }

                currTag = std::string_view{ text + begin, (std::size_t)(idx - begin) };

                if (!tagStart)
                {
                    if (text[idx] == config.TagEnd) idx++;

                    if (currTag.empty())
                    {
                        ERROR("Empty tag at %d...\n", begin);
                        return result;
                    }
                    if (CurrentStackPos > -1 && currTag != TagStack[CurrentStackPos].first)
                    {
                        ERROR("Closing tag <%.*s> doesnt match opening tag <%.*s>...",
                            (int)currTag.size(), currTag.data(), (int)TagStack[CurrentStackPos].first.size(),
                            TagStack[CurrentStackPos].first.data());
                        return result;
                    }
                }

                auto selfTerminatingTag = false;
                idx = SkipSpace(text, idx, end);

                auto isList = AreSame(currTag, "ul") || AreSame(currTag, "ol");
                auto isListStart = tagStart && isList;
                auto isListEnd = !tagStart && isList;
                auto isListItem = tagStart && AreSame(currTag, "li");
                auto isParagraph = AreSame(currTag, "p");
                auto isHeader = currTag.size() == 2u && (currTag[0] == 'h' || currTag[0] == 'H') && std::isdigit(currTag[1]);
                auto isRawText = AreSame(currTag, "pre") || AreSame(currTag, "code");
                auto isBlockquote = AreSame(currTag, "blockquote");

                if (text[idx] == '/' && ((idx + 1) < end) && text[idx + 1] == config.TagEnd)
                {
                    if (tagStart)
                    {
                        ERROR("Invalid tag: [%.*s] at [%d]\n", (int)currTag.size(), currTag.data(), idx);
                        return result;
                    }
                    else
                    {
                        selfTerminatingTag = true;
                        idx += 2;
                    }
                }
                
                if (tagStart)
                {
                    LOG("Entering Tag: <%.*s>\n", (int)currTag.size(), currTag.data());

                    CurrentStackPos++;
                    TagStack[CurrentStackPos].first = currTag;
                    TagStack[CurrentStackPos].second = CurrentStackPos > 0 ?
                        TagStack[CurrentStackPos - 1].second : initialStyle;
                    begin = idx;

                    // Extract all attributes
                    while ((idx < end) && (text[idx] != config.TagEnd))
                    {
                        while ((idx < end) && (text[idx] != '=') && !std::isspace(text[idx])) idx++;
                        auto attribName = std::string_view{ text + begin, (std::size_t)(idx - begin) };
                        LOG("Attribute: %.*s\n", (int)attribName.size(), attribName.data());

                        idx = SkipSpace(text, idx, end);
                        if (text[idx] == '=') idx++;
                        idx = SkipSpace(text, idx, end);

                        if (AreSame(attribName, "style"))
                        {
                            auto attribValue = GetQuotedString(text, idx, end);

                            if (!attribValue.has_value())
                            {
                                ERROR("Style attribute value not specified...");
                                return result;
                            }
                            else
                            {
                                auto sidx = 0;
                                auto style = attribValue.value();

                                while (sidx < (int)style.size())
                                {
                                    sidx = SkipSpace(style, sidx);
                                    auto stbegin = sidx;
                                    while ((sidx < (int)style.size()) && (style[sidx] != ':') && 
                                        !std::isspace(style[sidx])) sidx++;
                                    auto stylePropName = style.substr(stbegin, sidx - stbegin);

                                    sidx = SkipSpace(style, sidx);
                                    if (style[sidx] == ':') sidx++;
                                    sidx = SkipSpace(style, sidx);

                                    auto stylePropVal = GetQuotedString(style.data(), sidx, end);
                                    if (!stylePropVal.has_value() || stylePropVal.value().empty())
                                    {
                                        stbegin = sidx;
                                        while ((sidx < (int)style.size()) && style[sidx] != ';') sidx++;
                                        stylePropVal = style.substr(stbegin, sidx - stbegin);

                                        if (style[sidx] == ';') sidx++;
                                    }

                                    if (stylePropVal.has_value())
                                    {
                                        PopulateSegmentStyle(TagStack[CurrentStackPos].second, stylePropName, stylePropVal.value(), config);
                                    }
                                }
                            }
                        }
                    }
                    
                    if (text[idx] == config.TagEnd) idx++;

                    auto& styleprops = TagStack[CurrentStackPos].second;
                    auto isLineBreak = AreSame(currTag, "br");

                    if (isHeader)
                    {
                        styleprops.font.size = config.HFontSizes[currTag[1] - '1'];
                        styleprops.font.bold = true;
                    }
                    else if (isRawText)
                    {
                        styleprops.font.family = IM_RICH_TEXT_MONOSPACE_FONTFAMILY;
                        line = MoveToNextLine(styleprops, line, result, config);
                    }
                    else if (AreSame(currTag, "i") || AreSame(currTag, "em"))
                    {
                        styleprops.font.italics = true;
                        AddSegment(line, styleprops, config);
                    }
                    else if (AreSame(currTag, "b") || AreSame(currTag, "strong"))
                    {
                        styleprops.font.bold = true;
                        AddSegment(line, styleprops, config);
                    }
                    else if (AreSame(currTag, "mark"))
                    {
                        styleprops.bgcolor = config.NamedColor("yellow", config.UserData);
                        AddSegment(line, styleprops, config);
                    }

                    if (isLineBreak || isParagraph || isHeader || isRawText || isListItem)
                    {
                        paragraphBegin = isParagraph;
                        listItemBegin = isListItem;
                        line = MoveToNextLine(styleprops, line, result, config);

                        if (isHeader)
                        {
                            auto& style = line.Segments.back().Style;
                            style.height = 1.f;
                            style.fgcolor = ImColor(128, 128, 128);
                            style.offsetv.x = style.offsetv.y = config.DefaultHrVerticalMargins;

                            Token token;
                            token.Type = TokenType::HorizontalRule;
                            AddToken(line, token, config);

                            line = MoveToNextLine(styleprops, line, result, config);
                        }
                    }
                    else if (isListStart)
                    {
                        Token token;
                        token.Type = TokenType::ListStart;
                        AddToken(line, token, config);
                    }
                    else if (isListEnd)
                    {
                        Token token;
                        token.Type = TokenType::ListEnd;
                        AddToken(line, token, config);
                    }
                    else if (AreSame(currTag, "hr"))
                    {
                        line = MoveToNextLine(styleprops, line, result, config);
                        auto& style = line.Segments.back().Style;
                        style.offsetv.x = style.offsetv.y = config.DefaultHrVerticalMargins;

                        Token token;
                        token.Type = TokenType::HorizontalRule;
                        AddToken(line, token, config);

                        line = MoveToNextLine(styleprops, line, result, config);
                    }
                    else if (isBlockquote)
                    {
                        line = MoveToNextLine(styleprops, line, result, config);
                        auto& style = line.Segments.back().Style;
                        style.offseth.x = style.offseth.y = config.BlockquoteOffset;

                        Token token;
                        token.Type = tagStart ? TokenType::BlockquoteStart : TokenType::BlockquoteEnd;
                        AddToken(line, token, config);

                        line = MoveToNextLine(styleprops, line, result, config);
                    }
                    else
                    {
                        if (AreSame(currTag, "sup") || AreSame(currTag, "sub")) AddSegment(line, styleprops, config);
                    }
                }
            
                if (selfTerminatingTag || !tagStart)
                {
                    // pop stye properties and reset
                    TagStack[CurrentStackPos] = std::make_pair(std::string_view{}, SegmentStyle{});
                    --CurrentStackPos;
                    LOG("Exiting Tag: <%.*s>\n", (int)currTag.size(), currTag.data());

                    if (isListEnd || isParagraph || isHeader || isRawText || isBlockquote)
                    {
                        line = MoveToNextLine(CurrentStackPos >= 0 ?
                            TagStack[CurrentStackPos].second : initialStyle, 
                            line, result, config);
                    }
                    else if (AreSame(currTag, "sup") || AreSame(currTag, "sub") ||
                        AreSame(currTag, "i") || AreSame(currTag, "b"))
                    {
                        AddSegment(line, CurrentStackPos >= 0 ?
                            TagStack[CurrentStackPos].second : initialStyle, config);
                    }

                    currTag = CurrentStackPos == -1 ? "" : TagStack[CurrentStackPos].first;
                }
            }
            else
            {
                Token token;
                auto& styleprops = CurrentStackPos == -1 ? initialStyle : TagStack[CurrentStackPos].second;
                auto isPreTag = AreSame(currTag, "pre");
                auto begin = idx;

                if (isPreTag)
                {
                    while (((idx + 6) < end) && AreSame(std::string_view{ text + idx, 6u }, "</pre>")) idx++;
                    std::string_view content{ text + begin, (std::size_t)(idx - begin) };
                    auto curridx = 0, start = 0;

                    while (curridx < (int)content.size())
                    {
                        if (content[curridx] == '\n')
                        {
                            GenerateToken(line, content, begin, curridx, start, config, paragraphBegin, listItemBegin);
                            line = MoveToNextLine(styleprops, line, result, config);
                            start = curridx;
                        }

                        ++curridx;
                    }

                    if (curridx > start)
                    {
                        GenerateToken(line, content, begin, curridx, start, config, paragraphBegin, listItemBegin);
                    }
                }
                else
                {
                    while ((idx < end) && (text[idx] != config.TagStart)) idx++;
                    std::string_view content{ text + begin, (std::size_t)(idx - begin) };
                    LOG("Processing content [%.*s] for tag <%.*s>\n", (int)content.size(), 
                        content.data(), (int)currTag.size(), currTag.data());

                    auto& segment = line.Segments.back();
                    segment.Style = styleprops;

                    if (styleprops.renderAllWhitespace)
                    {
                        auto curridx = 0, start = 0;

                        while (curridx < (int)content.size())
                        {
                            if (content[curridx] == '\n')
                            {
                                GenerateToken(line, content, begin, curridx, start, config, paragraphBegin, listItemBegin);
                                line = MoveToNextLine(styleprops, line, result, config, true);
                                start = curridx;
                            }
                            else if (content[curridx] == '\t')
                            {
                                GenerateToken(line, content, begin, curridx, start, config, paragraphBegin, listItemBegin);

                                // Token for tabs replaced with tabstop in config
                                token.Extent = std::make_pair(-1, -1);
                                token.Content = TabSpaces;
                                AddToken(line, token, config);
                                start = curridx;
                            }
                            else if (content[curridx] == config.EscapeSeqStart)
                            {
                                GenerateToken(line, content, begin, curridx, start, config, paragraphBegin, listItemBegin);

                                curridx++;
                                auto [hasEscape, isNewLine] = AddEscapeSequences(content, curridx, config.EscapeCodes, 
                                    config.EscapeSeqStart, config.EscapeSeqEnd, line, start);
                                curridx = start;

                                if (isNewLine) line = MoveToNextLine(styleprops, line, result, config, true);
                                if (hasEscape) continue;
                            }
                            else if (content[curridx] == ' ')
                            {
                                GenerateToken(line, content, begin, curridx, start, config, paragraphBegin, listItemBegin);

                                auto totalspaces = curridx;
                                curridx = SkipSpace(content, curridx);
                                totalspaces = curridx - totalspaces;
                                GenerateToken(line, LineSpaces, 0, totalspaces, 0, config, paragraphBegin, listItemBegin);
                                continue;
                            }

                            curridx++;
                        }

                        if (curridx > start)
                        {
                            GenerateToken(line, content, begin, curridx, start, config, paragraphBegin, listItemBegin);
                        }
                    }
                    else
                    {
                        // Ignore newlines, tabs & consecutive spaces
                        auto curridx = 0, start = 0;

                        while (curridx < (int)content.size())
                        {
                            if (content[curridx] == '\n')
                            {
                                GenerateToken(line, content, begin, curridx, start, config, paragraphBegin, listItemBegin);
                                while (curridx < (int)content.size() && content[curridx] == '\n') curridx++;
                                start = curridx;
                            }
                            else if (content[curridx] == '\t')
                            {
                                GenerateToken(line, content, begin, curridx, start, config, paragraphBegin, listItemBegin);
                                while (curridx < (int)content.size() && content[curridx] == '\t') curridx++;
                                start = curridx;
                            }
                            else if (content[curridx] == config.EscapeSeqStart)
                            {
                                GenerateToken(line, content, begin, curridx, start, config, paragraphBegin, listItemBegin);

                                curridx++;
                                auto [hasEscape, isNewLine] = AddEscapeSequences(content, curridx, config.EscapeCodes,
                                    config.EscapeSeqStart, config.EscapeSeqEnd, line, start);
                                curridx = start;

                                if (isNewLine) line = MoveToNextLine(styleprops, line, result, config, true);
                                if (hasEscape) continue;
                            }
                            else if (content[curridx] == ' ')
                            {
                                curridx++;

                                if (curridx - start > 1)
                                {
                                    GenerateToken(line, content, begin, curridx, start, config, paragraphBegin, listItemBegin);
                                }

                                curridx = SkipSpace(content, curridx);
                                start = curridx;
                                continue;
                            }

                            curridx++;
                        }

                        if (curridx > start)
                        {
                            GenerateToken(line, content, begin, curridx, start, config, paragraphBegin, listItemBegin);
                        }
                    }
                }
            }
        }

        result.push_back(line);
        return result;
    }

#ifdef _DEBUG
    void PrintAllTokens(const std::deque<DrawableLine>& lines)
    {
        for (const auto& line : lines)
        {
            for (const auto& segment : line.Segments)
            {
                std::fprintf(stdout, "Segment: [");

                for (const auto& token : segment.Tokens)
                {
                    std::fprintf(stdout, "Token: [%.*s]; ", (int)token.Content.size(), 
                        token.Content.data());
                }

                std::fprintf(stdout, "]  ");
            }

            std::fprintf(stdout, "\n");
        }
    }
#endif

    void PushConfig(const RenderConfig& config)
    {
        auto ctx = ImGui::GetCurrentContext();
        RenderConfigs[ctx].push_back(config);
    }

    void PopConfig()
    {
        auto ctx = ImGui::GetCurrentContext();
        auto it = RenderConfigs.find(ctx);
        if (it != RenderConfigs.end()) it->second.pop_back();
    }

    [[nodiscard]] bool IsBorderUniform(const BorderStyle (&border)[4])
    {
        for (auto side = 1; side < 4; ++side)
        {
            if (border[TopSide].color.Value != border[side].color.Value ||
                border[TopSide].thickness != border[side].thickness)
            {
                return false;
            }
        }

        return true;
    }

    void DrawBorder(const SegmentStyle& style, ImVec2 currpos, float width, float height, ImDrawList* drawList)
    {
        auto top = style.border[TopSide].thickness / 2.f;
        auto left = style.border[LeftSide].thickness / 2.f;
        auto right = style.border[RightSide].thickness / 2.f;
        auto bottom = style.border[BottomSide].thickness / 2.f;

        if (IsBorderUniform(style.border))
        {
            drawList->AddRect(currpos + ImVec2{ top, left },
                currpos + ImVec2{ width - right, height - bottom }, style.border[TopSide].color,
                style.borderRoundedness[TopLeftCorner], ImDrawFlags_RoundCornersAll,
                style.border[TopSide].thickness);
        }
        else
        {
            if (style.border[TopSide].thickness > 0 &&
                style.border[TopSide].color.Value.w > 0)
            {
                drawList->AddLine(currpos + ImVec2{ left, top },
                    currpos + ImVec2{ width - right, top },
                    style.border[TopSide].color, style.border[TopSide].thickness);
            }

            if (style.border[RightSide].thickness > 0 &&
                style.border[RightSide].color.Value.w > 0)
            {
                drawList->AddLine(currpos + ImVec2{ width - right, top },
                    currpos + ImVec2{ width - right, height - bottom },
                    style.border[RightSide].color, style.border[RightSide].thickness);
            }

            if (style.border[BottomSide].thickness > 0 &&
                style.border[BottomSide].color.Value.w > 0)
            {
                drawList->AddLine(currpos + ImVec2{ left, height - bottom },
                    currpos + ImVec2{ width - right, height - bottom },
                    style.border[BottomSide].color, style.border[BottomSide].thickness);
            }

            if (style.border[LeftSide].thickness > 0 &&
                style.border[LeftSide].color.Value.w > 0)
            {
                drawList->AddLine(currpos + ImVec2{ left, top },
                    currpos + ImVec2{ left, height - bottom },
                    style.border[LeftSide].color, style.border[LeftSide].thickness);
            }
        }
    }

    inline void DrawDebugRect(ImDrawList* drawList, const ImVec2& startpos, float width, float height, const RenderConfig& config)
    {
        if (config.DrawDebugRects) drawList->AddRect(startpos, startpos + ImVec2{ config.Bounds.x, height }, IM_COL32(255, 0, 0, 255));
    }

    [[nodiscard]] RenderConfig* GetRenderConfig(RenderConfig* config)
    {
        if (config == nullptr)
        {
            auto ctx = ImGui::GetCurrentContext();
            auto it = RenderConfigs.find(ctx);

            if (it == RenderConfigs.end())
            {
                if (!DefaultRenderConfigInit)
                    GetDefaultConfig({ 200.f, 200.f });

                config = &DefaultRenderConfig;
            }
            else config = &(it->second.back());
        }

        return config;
    }

    struct RichTextHasher
    {
        std::size_t operator()(const std::pair<std::string_view, RenderConfig*>& key) const
        {
            auto hash1 = std::hash<RenderConfig*>()(key.second);
            std::vector<char> buf; buf.resize(key.first.size() + sizeof(hash1));
            std::memcpy(buf.data(), key.first.data(), key.first.size());
            std::memcpy(buf.data() + key.first.size(), &hash1, sizeof(hash1));
            std::string_view total{ buf.data(), buf.size() };
            return std::hash<std::string_view>()(total);
        }
    };

    static std::unordered_map<std::pair<std::string_view, RenderConfig*>, std::deque<DrawableLine>, RichTextHasher> RichTextMap;

    void Draw(const char* text, int start, int end, RenderConfig* config)
    {
        assert(ImGui::GetFont() || !FontStore.empty());

        if (text == nullptr || config->Bounds.x == 0 || config->Bounds.y == 0) return;
        end = end == -1 ? (int)std::strlen(text + start) : end;
        if ((end - start) == 0) return;

        config = GetRenderConfig(config);
        
        if ((end - start) > IM_RICHTEXT_MIN_RTF_CACHESZ)
        {
            auto key = std::make_pair(std::string_view{ text + start, std::size_t(end - start) }, config);
            auto it = RichTextMap.find(key);

            if (it == RichTextMap.end())
            {
                auto drawables = GetDrawableLines(text, start, end, *config);
                it = RichTextMap.emplace(key, std::deque<DrawableLine>{}).first;
                it->second = GetDrawableLines(text, start, end, *config);
            }

            Draw(it->second, config);
        }
        else 
        {
            auto drawables = GetDrawableLines(text, start, end, *config);
            Draw(drawables, config);
        }
    }

    bool DrawToken(ImDrawList* drawList, ImVec2& currpos, const Token& token, ImVec2 startpos,
        std::pair<float, float> availsz, float top, float bottom, float left, float right, 
        float& offsetx, const SegmentStyle& style, RenderConfig* config)
    {
        if (token.Type == TokenType::HorizontalRule)
        {
            auto linestart = ImVec2{ left + startpos.x, top + currpos.y };
            auto lineend = linestart + ImVec2{ config->Bounds.x - right, style.height };

            drawList->AddRectFilled(linestart, lineend, style.fgcolor);
            DrawBorder(style, startpos, config->Bounds.x, availsz.second, drawList);
            DrawDebugRect(drawList, startpos, config->Bounds.x, availsz.second, *config);

            currpos.y += style.height + style.offsetv.x + style.offsetv.y;
            availsz.second = style.height;
        }
        else if (token.Type == TokenType::ParagraphStart)
        {
            if (config->ParagraphStop > 0)
            {
                static char buffer[32] = { 0 };
                std::memset(buffer, 0, 32);
                std::memset(buffer, ' ', std::min(31, config->ParagraphStop));
                auto sz = ImGui::CalcTextSize(buffer);
                offsetx += sz.x;
            }
        }
        else if (token.Type == TokenType::ListItem)
        {
            config->BulletSizeScale = clamp(config->BulletSizeScale, 1.f, 4.f);
            auto bulletsz = style.font.size / config->BulletSizeScale;

            switch (style.list.itemStyle)
            {
            case BulletType::Circle: drawList->AddCircle(ImVec2{ startpos.x + offsetx,
                currpos.y + availsz.second / 2.f - bulletsz / 2.f },
                bulletsz / 2.f, style.fgcolor);
                break;

            case BulletType::Disk: drawList->AddCircleFilled(ImVec2{ startpos.x + offsetx,
                currpos.y + availsz.second / 2.f - bulletsz / 2.f },
                bulletsz / 2.f, style.fgcolor);
                break;

            case BulletType::Square: drawList->AddRect(ImVec2{ offsetx + startpos.x,
                currpos.y + (availsz.second / 2.f) - (bulletsz / 2.f) },
                ImVec2{ offsetx + startpos.x + bulletsz,
                currpos.y + (availsz.second / 2.f) + (bulletsz / 2.f) }, style.fgcolor);
                break;

            case BulletType::FilledSquare: drawList->AddRectFilled(ImVec2{ offsetx + startpos.x,
            currpos.y + (availsz.second / 2.f) - (bulletsz / 2.f) },
                ImVec2{ offsetx + startpos.x + bulletsz,
                currpos.y + (availsz.second / 2.f) + (bulletsz / 2.f) }, style.fgcolor);
                break;

            case BulletType::Concentric: drawList->AddCircle(ImVec2{ startpos.x + offsetx,
                currpos.y + availsz.second / 2.f - bulletsz / 2.f },
                bulletsz / 2.f, style.fgcolor);
                drawList->AddCircleFilled(ImVec2{ startpos.x + offsetx,
                currpos.y + availsz.second / 2.f - bulletsz / 2.f },
                    bulletsz / 3.f, style.fgcolor);
                break;

            default:
                break;
            }

            currpos.x += offsetx + config->ListItemOffset + bulletsz;
            offsetx = 0;
        }
        else
        {
            auto textend = token.Content.data() + token.Content.size();
            auto sz = token.Size;
            auto width = left + right + sz.x;
            ImVec2 textpos{ currpos.x + left, currpos.y + (availsz.second / 2.f) - (sz.y / 2.f) };

            if (style.bgcolor.Value != config->DefaultBgColor.Value)
            {
                drawList->AddRectFilled(currpos + ImVec2{ left, right },
                    currpos + ImVec2{ width - right, availsz.second - bottom }, style.bgcolor);
            }

            drawList->AddText(textpos, style.fgcolor, token.Content.data(), textend);
            DrawBorder(style, currpos, width, availsz.second, drawList);
            currpos.x += width;
            offsetx = 0;
            DrawDebugRect(drawList, startpos, width, availsz.second, *config);

            if (currpos.x > (config->Bounds.x + startpos.x)) return false;
        }

        return true;
    }

    void DrawSegment(ImDrawList* drawList, ImVec2& currpos, const SegmentDetails& segment,
        std::pair<float, float> linesz, ImVec2 startpos, float& offsetx, int& listDepth,
        int& blockquoteDepth, RenderConfig* config)
    {
        ImGui::PushFont(segment.Style.font.font);

        for (const auto& token : segment.Tokens)
        {
            const auto& style = segment.Style;
            auto top = style.border[TopSide].thickness + style.offsetv.x;
            auto bottom = style.border[BottomSide].thickness + style.offsetv.y;
            auto left = style.border[LeftSide].thickness + offsetx;
            auto right = style.border[RightSide].thickness;

            if (token.Type == TokenType::ListStart) listDepth++;
            else if (token.Type == TokenType::ListEnd) listDepth--;
            else if (token.Type == TokenType::BlockquoteStart) blockquoteDepth++;
            else if (token.Type == TokenType::BlockquoteEnd) blockquoteDepth--;
            else if (!DrawToken(drawList, currpos, token, startpos, linesz, top, bottom, left, right, offsetx, style, config)) break;
        }

        ImGui::PopFont();
    }

    void Draw(std::deque<DrawableLine>& lines, RenderConfig* config)
    {
        config = GetRenderConfig(config);

        //PrintAllTokens(lines);
        auto currpos = ImGui::GetCursorScreenPos(), startpos = currpos;
        auto drawList = ImGui::GetWindowDrawList();
        drawList->AddRectFilled(currpos, currpos + config->Bounds, config->DefaultBgColor);
        int listDepth = 0, blockquoteDepth = 0;
        ImGui::PushClipRect(currpos, currpos + config->Bounds, true);

        for (auto& line : lines)
        {
            if (line.Segments.empty()) continue;

            auto offsetx = (config->ListItemIndent * (float)listDepth) + (config->BlockquoteOffset + (float)blockquoteDepth);
            auto linesz = GetLineSize(line, *config);
            auto isRightAligned = line.Segments.size() == 1u &&
                line.Segments.front().Style.alignmentH == HorizontalAlignment::Right;

            for (const auto& segment : line.Segments)
            {
                DrawSegment(drawList, currpos, segment, linesz, startpos, offsetx, listDepth, blockquoteDepth, config);
            }

            currpos.y += linesz.second + config->LineGap;
            currpos.x = startpos.x;

            if (currpos.y > (config->Bounds.y + startpos.y)) break;
        }

        ImGui::PopClipRect();
    }
}