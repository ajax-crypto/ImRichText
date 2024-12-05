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

    struct EscapeCodeInfo
    {
        const char* code;
        const char* replacement;
        int codesz;
    };

    static EscapeCodeInfo EscapeCodes[10] = {
        { "Tab", "\t", 3 }, { "NewLine", "\n", 7 }, { "nbsp", " ", 4 }, 
        { "gt", ">", 2 }, { "lt", "<", 2 }, 
        { "amp", "&", 3 }, { "copy", "©", 4 }, { "reg", "®", 3 }, 
        { "deg", "°", 3 }, { "micro", "μ", 5 }
    };

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
            auto idx = SkipDigits(stylePropVal);
            auto thickness = ExtractInt(stylePropVal.substr(0u, idx), 0);

            idx = WholeWord(stylePropVal, idx);
            idx = SkipSpace(stylePropVal, idx);
            idx = WholeWord(stylePropVal, idx);
            idx = SkipSpace(stylePropVal, idx);
            // TODO: Obey line Type in border

            auto colstart = idx;
            idx = WholeWord(stylePropVal, idx);
            auto color = ExtractColor(stylePropVal.substr(colstart, idx),
                config.NamedColor, config.UserData);

            for (auto corner = 0; corner < 4; ++corner)
            {
                el.border[corner].thickness = thickness;
                el.border[corner].color = color;
            }
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

            files.Normal = copyFileName(names->Proportional.Normal, fontpath, startidx);
            files.Light = copyFileName(names->Proportional.Light, fontpath, startidx);
            files.Bold = copyFileName(names->Proportional.Bold, fontpath, startidx);
            files.Italics = copyFileName(names->Proportional.Italics, fontpath, startidx);
            files.BoldItalics = copyFileName(names->Proportional.BoldItalics, fontpath, startidx);
            LoadFonts(IM_RICHTEXT_DEFAULT_FONTFAMILY, files, sz, fconfig);

            files.Normal = copyFileName(names->Monospace.Normal, fontpath, startidx);
            files.Bold = copyFileName(names->Monospace.Bold, fontpath, startidx);
            files.Italics = copyFileName(names->Monospace.Italics, fontpath, startidx);
            files.BoldItalics = copyFileName(names->Monospace.BoldItalics, fontpath, startidx);
            LoadFonts(IM_RICH_TEXT_MONOSPACE_FONTFAMILY, files, sz, fconfig);
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

                /*ImFontConfig fconfig;
                fconfig.OversampleH = 3.0;
                fconfig.OversampleV = 1.0;

                ImGuiIO& io = ImGui::GetIO();
                szit = famit->second.emplace(size, FontCollection{}).first;
                const auto& existing = famit->second.begin()->second;

                if (bold && italics)
                {
                    PendingFonts[family][size].BoldItalics = existing.Files.BoldItalics;
                }
                else if (bold)
                {
                    PendingFonts[family][size].Bold = existing.Files.Bold;
                }
                else if (italics)
                {
                    PendingFonts[family][size].Italics = existing.Files.Italics;
                }
                else if (light)
                {
                    PendingFonts[family][size].Light = existing.Files.Light;
                }
                else
                {
                    PendingFonts[family][size].Normal = existing.Files.Normal;
                }*/
            }

            if (bold && italics) return szit->second.BoldItalics;
            else if (bold) return szit->second.Bold;
            else if (italics) return szit->second.Italics;
            else if (light) return szit->second.Light;
            else return szit->second.Normal;
        }
    }

    struct CaseInsensitieHasher
    {
        std::size_t operator()(std::string_view key) const
        {
            thread_local static char buffer[32] = { 0 };
            std::memset(buffer, 0, 32);
            strncpy_s(buffer, key.data(), std::min(31, (int)key.size()));
            return std::hash<std::string_view>()(buffer);
        }
    };

    ImColor GetColor(const char* name, void*)
    {
        const static std::unordered_map<std::string_view, ImColor, CaseInsensitieHasher> Colors{
            { "black", ImColor(0, 0, 0, 255) },
            { "silver", ImColor(192, 192, 192, 255) },
            { "gray", ImColor(128, 128, 128, 255) },
            { "white", ImColor(255, 255, 255, 255) },
            { "maroon", ImColor(128, 0, 0, 255) },
            { "red", ImColor(255, 0, 0, 255) },
            { "purple", ImColor(128, 0, 128, 255) },
            { "fuchsia", ImColor(255, 0, 255, 255) },
            { "green", ImColor(0, 128, 0, 255) },
            { "lime", ImColor(0, 255, 0, 255) },
            { "olive", ImColor(128, 128, 0, 255) },
            { "yellow", ImColor(255, 255, 0, 255) },
            { "navy", ImColor(0, 0, 128, 255) },
            { "blue", ImColor(0, 0, 255, 255) },
            { "teal", ImColor(0, 128, 128, 255) },
            { "aqua", ImColor(0, 255, 255, 255) },
        };

        return Colors.at(name);
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
        result.font.font = config.GetFont(config.DefaultFontFamily, config.DefaultFontSize, false, false, false, config.UserData);
        result.font.family = config.DefaultFontFamily;
        result.font.size = config.DefaultFontSize;
        result.bgcolor = config.DefaultBgColor;
        result.fgcolor = config.DefaultFgColor;
        result.list.itemStyle = config.ListItemBullet;
        return result;
    }

    [[nodiscard]] float GetLineHeight(const SegmentStyle& styleprops, const RenderConfig& config, SegmentDetails& segment)
    {
        auto border = styleprops.border[TopSide].thickness + styleprops.border[BottomSide].thickness;

        if (!segment.HasText) return styleprops.height + border;
        else
        {
            float height = 0.f;

            for (auto& token : segment.Tokens)
            {
                token.Size = ImGui::CalcTextSize(token.Content.data(), token.Content.data() + token.Content.size());
                height = std::max(height, token.Size.y);
            }

            return height + border + config.LineGap;
        }
    }

    void AddToken(DrawableLine& line, const Token& token, const RenderConfig& config)
    {
        auto& segment = line.Segments.back();
        segment.Tokens.emplace_back(token);
        segment.HasText = segment.HasText || (!token.Content.empty());
        line.HasText = line.HasText || segment.HasText;

        LOG("Added token: %.*s [isline: %s] [itemtype: %s]\n", 
            (int)token.Content.size(), token.Content.data(),
            token.IsHorizontalRule ? "yes" : "no",
            token.ListItemStart ? "start" : token.ListItemEnd ? "end" : "none");
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
        std::deque<DrawableLine>& result, const RenderConfig& config)
    {
        result.push_back(line);
        LOG("Created new line...\n");
        return CreateNewLine(styleprops, config);
    }

    float GetLineHeight(DrawableLine& line, const RenderConfig& config)
    {
        auto height = 0.f;

        for (auto& segment : line.Segments)
        {
            ImGui::PushFont(segment.Style.font.font);
            height = std::max(height, GetLineHeight(segment.Style, config, segment));
            ImGui::PopFont();
        }

        return height;
    }

    void GenerateToken(DrawableLine& line, std::string_view content, 
        int begin, int curridx, int start, 
        const RenderConfig& config, bool& paragraphBeginning)
    {
        Token token;
        token.Extent = std::make_pair(begin + start, begin + curridx);
        token.Content = content.substr(start, curridx - start);
        token.ParagraphBeginning = paragraphBeginning;
        paragraphBeginning = false;
        AddToken(line, token, config);
    }

    [[nodiscard]] std::pair<bool, bool> AddEscapeSequences(const std::string_view content, 
        int curridx, DrawableLine& line, int& start)
    {
        Token token;
        auto hasEscape = false;
        auto isNewLine = false;

        for (const auto& code : EscapeCodes)
        {
            if ((curridx + code.codesz) < (int)content.size() &&
                AreSame(content.substr(curridx, code.codesz), code.code) &&
                content[curridx + code.codesz] == ';')
            {
                if (code.replacement == "\n") isNewLine = true;
                else
                {
                    token.Extent = std::make_pair(-1, -1);
                    token.Content = code.replacement;
                    line.Segments.back().Tokens.emplace_back(token);
                    LOG("Added token: (replacement of) %s [isline: %s] [itemtype: %s]\n",
                        code.code,
                        token.IsHorizontalRule ? "yes" : "no",
                        token.ListItemStart ? "start" : token.ListItemEnd ? "end" : "none");
                }
                
                curridx += code.codesz + 1;
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
        auto paragraphBeginning = false;

        for (auto idx = start; idx < end;)
        {
            if (text[idx] == '<')
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
                while ((idx < end) && !std::isspace(text[idx]) && (text[idx] != '>')) idx++;

                if (idx - begin == 0)
                {
                    ERROR("Empty tag at %d...\n", begin);
                    return result;
                }

                currTag = std::string_view{ text + begin, (std::size_t)(idx - begin) };

                if (!tagStart)
                {
                    if (text[idx] == '>') idx++;

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

                auto isListItem = AreSame(currTag, "ul") || AreSame(currTag, "ol");
                auto isParagraph = AreSame(currTag, "p");
                auto isHeader = currTag.size() == 2u && (currTag[0] == 'h' || currTag[0] == 'H') && std::isdigit(currTag[1]);
                auto isRawText = AreSame(currTag, "pre");

                if (text[idx] == '/' && ((idx + 1) < end) && text[idx + 1] == '>')
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
                    while ((idx < end) && (text[idx] != '>'))
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
                    
                    if (text[idx] == '>') idx++;

                    auto& styleprops = TagStack[CurrentStackPos].second;
                    auto isLineBreak = AreSame(currTag, "br");

                    if (isHeader)
                    {
                        styleprops.font.size = config.HFontSizes[currTag[1] - '1'];
                    }
                    else if (isRawText)
                    {
                        styleprops.font.family = IM_RICH_TEXT_MONOSPACE_FONTFAMILY;
                        line = MoveToNextLine(styleprops, line, result, config);
                    }
                    else if (AreSame(currTag, "i"))
                    {
                        styleprops.font.italics = true;
                        AddSegment(line, TagStack[CurrentStackPos].second, config);
                    }
                    else if (AreSame(currTag, "b"))
                    {
                        styleprops.font.bold = true;
                        AddSegment(line, TagStack[CurrentStackPos].second, config);
                    }

                    if (isLineBreak || isParagraph || isListItem || isHeader || isRawText)
                    {
                        paragraphBeginning = isParagraph;
                        line = MoveToNextLine(styleprops, line, result, config);

                        if (isListItem)
                        {
                            Token token;
                            token.ListItemStart = true;
                            AddToken(line, token, config);
                        }
                    }
                    else if (AreSame(currTag, "hr"))
                    {
                        line = MoveToNextLine(styleprops, line, result, config);

                        Token token;
                        token.IsHorizontalRule = true;
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

                    if (isListItem || isParagraph || isHeader || isRawText)
                    {
                        line = MoveToNextLine(CurrentStackPos > 0 ?
                            TagStack[CurrentStackPos - 1].second : initialStyle, 
                            line, result, config);
                    }
                    else if (AreSame(currTag, "sup") || AreSame(currTag, "sub"))
                    {
                        AddSegment(line, CurrentStackPos > 0 ?
                            TagStack[CurrentStackPos - 1].second : initialStyle, config);
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
                            GenerateToken(line, content, begin, curridx, start, config, paragraphBeginning);
                            line = MoveToNextLine(styleprops, line, result, config);
                            start = curridx;
                        }

                        ++curridx;
                    }

                    if (curridx > start)
                    {
                        GenerateToken(line, content, begin, curridx, start, config, paragraphBeginning);
                    }
                }
                else
                {
                    while ((idx < end) && (text[idx] != '<')) idx++;
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
                                GenerateToken(line, content, begin, curridx, start, config, paragraphBeginning);
                                line = MoveToNextLine(styleprops, line, result, config);
                                start = curridx;
                            }
                            else if (content[curridx] == '\t')
                            {
                                GenerateToken(line, content, begin, curridx, start, config, paragraphBeginning);

                                // Token for tabs replaced with tabstop in config
                                token.Extent = std::make_pair(-1, -1);
                                token.Content = TabSpaces;
                                AddToken(line, token, config);
                                start = curridx;
                            }
                            else if (content[curridx] == '&')
                            {
                                GenerateToken(line, content, begin, curridx, start, config, paragraphBeginning);

                                curridx++;
                                auto [hasEscape, isNewLine] = AddEscapeSequences(content, curridx, line, start);
                                curridx = start;

                                if (isNewLine) line = MoveToNextLine(styleprops, line, result, config);
                                if (hasEscape) continue;
                            }

                            curridx++;
                        }

                        if (curridx > start)
                        {
                            GenerateToken(line, content, begin, curridx, start, config, paragraphBeginning);
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
                                GenerateToken(line, content, begin, curridx, start, config, paragraphBeginning);
                                while (curridx < (int)content.size() && content[curridx] == '\n') curridx++;
                                start = curridx;
                            }
                            else if (content[curridx] == '\t')
                            {
                                GenerateToken(line, content, begin, curridx, start, config, paragraphBeginning);
                                while (curridx < (int)content.size() && content[curridx] == '\t') curridx++;
                                start = curridx;
                            }
                            else if (content[curridx] == '&')
                            {
                                GenerateToken(line, content, begin, curridx, start, config, paragraphBeginning);

                                curridx++;
                                auto [hasEscape, isNewLine] = AddEscapeSequences(content, curridx, line, start);
                                curridx = start;

                                if (isNewLine) line = MoveToNextLine(styleprops, line, result, config);
                                if (hasEscape) continue;
                            }
                            else if (content[curridx] == ' ')
                            {
                                curridx++;
                                GenerateToken(line, content, begin, curridx, start, config, paragraphBeginning);

                                curridx = SkipSpace(content, curridx);
                                start = curridx;
                                continue;
                            }

                            curridx++;
                        }

                        if (curridx > start)
                        {
                            GenerateToken(line, content, begin, curridx, start, config, paragraphBeginning);
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
                it = RenderConfigs.emplace(ctx, std::deque<RenderConfig>{}).first;
                config = &(it->second.emplace_back());
                config->Bounds = ImGui::GetWindowSize();
                config->GetFont = &GetFont;
                config->NamedColor = &GetColor;
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
        assert(config != nullptr);
        assert(!FontStore.empty());

        if (text == nullptr || config->Bounds.x == 0 || config->Bounds.y == 0) return;
        end = end == -1 ? (int)std::strlen(text + start) : end;
        if ((end - start) == 0) return;

        config = GetRenderConfig(config);
        
        if ((end - start) > 128)
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

    void Draw(std::deque<DrawableLine>& lines, RenderConfig* config)
    {
        config = GetRenderConfig(config);

        //PrintAllTokens(lines);
        auto currpos = ImGui::GetCursorScreenPos(), startpos = currpos;
        auto drawList = ImGui::GetWindowDrawList();
        drawList->AddRectFilled(currpos, currpos + config->Bounds, config->DefaultBgColor);
        int listDepth = 0;
        ImGui::PushClipRect(currpos, currpos + config->Bounds, true);

        for (auto& line : lines)
        {
            if (line.Segments.empty()) continue;

            auto offsetx = (config->ListItemIndent * (float)listDepth);
            auto height = GetLineHeight(line, *config);

            for (const auto& segment : line.Segments)
            {
                ImGui::PushFont(segment.Style.font.font);

                for (const auto& token : segment.Tokens)
                {
                    const auto& style = segment.Style;

                    if (token.ListItemStart) listDepth++;
                    else if (token.ListItemEnd) listDepth--;
                    else if (token.IsHorizontalRule)
                    {
                        drawList->AddRectFilled(startpos + ImVec2{ style.border[LeftSide].thickness,
                            style.border[TopSide].thickness },
                            startpos + ImVec2{ config->Bounds.x - style.border[RightSide].thickness,
                            height - style.border[BottomSide].thickness }, style.fgcolor);
                        DrawBorder(style, startpos, config->Bounds.x, height, drawList);
                        DrawDebugRect(drawList, startpos, config->Bounds.x, height, *config);
                    }
                    else
                    {
                        if (token.ParagraphBeginning)
                        {
                            static char buffer[32] = { 0 };
                            std::memset(buffer, 0, 32);
                            std::memset(buffer, ' ', std::min(31, config->ParagraphStop));
                            auto sz = ImGui::CalcTextSize(buffer);
                            offsetx += sz.x;
                        }

                        auto textend = token.Content.data() + token.Content.size();
                        auto sz = token.Size;
                        auto width = style.border[RightSide].thickness + style.border[LeftSide].thickness + offsetx + sz.x;

                        ImVec2 textpos{ currpos.x + offsetx + style.border[LeftSide].thickness,
                            currpos.y + (height / 2.f) };

                        if (style.bgcolor.Value != config->DefaultBgColor.Value)
                        {
                            drawList->AddRectFilled(currpos + ImVec2{ style.border[LeftSide].thickness,  style.border[TopSide].thickness },
                                currpos + ImVec2{ width, height }, style.bgcolor);
                        }

                        drawList->AddText(textpos, style.fgcolor, token.Content.data(), textend);
                        DrawBorder(style, currpos, width, height, drawList);
                        currpos.x += width;
                        offsetx = 0;
                        DrawDebugRect(drawList, startpos, width, height, *config);

                        if (currpos.x > (config->Bounds.x + startpos.x)) break;
                    }
                }

                ImGui::PopFont();
            }

            currpos.y += height + config->LineGap;
            currpos.x = startpos.x;

            if (currpos.y > (config->Bounds.y + startpos.y)) break;
        }

        ImGui::PopClipRect();
    }
}