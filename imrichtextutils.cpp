#include "imrichtextutils.h"
#include "imgui_internal.h"

#include <cctype>
#include <unordered_map>

namespace ImRichText
{
#pragma optimize( "", on )
    [[nodiscard]] int SkipSpace(const char* text, int idx, int end)
    {
        while ((idx < end) && std::isspace(text[idx])) idx++;
        return idx;
    }

    [[nodiscard]] int SkipSpace(const std::string_view text, int from)
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

    [[nodiscard]] int SkipDigits(const std::string_view text, int from)
    {
        auto end = (int)text.size();
        while ((from < end) && (std::isdigit(text[from]))) from++;
        return from;
    }

    [[nodiscard]] int SkipFDigits(const std::string_view text, int from)
    {
        auto end = (int)text.size();
        while ((from < end) && ((std::isdigit(text[from])) || (text[from] == '.'))) from++;
        return from;
    }
#pragma optimize( "", off )

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

    [[nodiscard]] bool StartsWith(const std::string_view lhs, const char* rhs)
    {
        auto rlimit = (int)std::strlen(rhs);
        auto llimit = (int)lhs.size();
        if (rlimit > llimit)
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

    [[nodiscard]] int ExtractInt(std::string_view input, int defaultVal)
    {
        int result = defaultVal;
        int base = 1;
        auto idx = (int)input.size() - 1;
        while (!std::isdigit(input[idx]) && idx >= 0) idx--;

        for (; idx >= 0; --idx)
        {
            result += (input[idx] - '0') * base;
            base *= 10;
        }

        return result;
    }

    [[nodiscard]] int ExtractIntFromHex(std::string_view input, int defaultVal)
    {
        int result = defaultVal;
        int base = 1;
        auto idx = (int)input.size() - 1;
        while (!std::isdigit(input[idx]) && !std::isalpha(input[idx]) && idx >= 0) idx--;

        for (; idx >= 0; --idx)
        {
            result += std::isdigit(input[idx]) ? (input[idx] - '0') * base :
                std::islower(input[idx]) ? ((input[idx] - 'a') + 10) * base :
                ((input[idx] - 'A') + 10) * base;
            base *= 16;
        }

        return result;
    }

    [[nodiscard]] IntOrFloat ExtractNumber(std::string_view input, float defaultVal)
    {
        float result = 0.f, base = 1.f;
        bool isInt = false;
        auto idx = (int)input.size() - 1;

        while (idx >= 0 && input[idx] != '.') idx--;
        auto decimal = idx;

        if (decimal != -1)
        {
            for (auto midx = decimal; midx >= 0; --midx)
            {
                result += (input[midx] - '0') * base;
                base *= 10.f;
            }

            base = 0.1f;
            for (auto midx = decimal + 1; midx < (int)input.size(); ++midx)
            {
                result += (input[midx] - '0') * base;
                base *= 0.1f;
            }
        }
        else
        {
            for (auto midx = (int)input.size() - 1; midx >= 0; --midx)
            {
                result += (input[midx] - '0') * base;
                base *= 10.f;
            }

            isInt = true;
        }

        return { result, !isInt };
    }

    [[nodiscard]] float ExtractFloatWithUnit(std::string_view input, float defaultVal, float ems, float parent, float scale)
    {
        float result = defaultVal, base = 1.f;
        auto idx = (int)input.size() - 1;
        while (!std::isdigit(input[idx]) && idx >= 0) idx--;

        if (AreSame(input.substr(idx + 1), "pt")) scale = 1.3333f;
        else if (AreSame(input.substr(idx + 1), "em")) scale = ems;
        else if (input[idx + 1] == '%') scale = parent * 0.01f;

        auto num = ExtractNumber(input.substr(0, idx + 1), defaultVal);
        result = num.value;

        return result * scale;
    }

    uint32_t ToRGBA(int r, int g, int b, int a)
    {
        return (((uint32_t)(a) << 24) |
            ((uint32_t)(b) << 16) |
            ((uint32_t)(g) << 8) |
            ((uint32_t)(r) << 0));
    }

    uint32_t ToRGBA(float r, float g, float b, float a)
    {
        return ToRGBA((int)(r * 255.f), (int)(g * 255.f), (int)(b * 255.f), (int)(a * 255.f));
    }

#ifdef IM_RICHTEXT_NO_IMGUI
    ColorConvertHSVtoRGB(float h, float s, float v, float& out_r, float& out_g, float& out_b)
    {
        if (s == 0.0f)
        {
            // gray
            out_r = out_g = out_b = v;
            return;
        }

        h = std::fmodf(h, 1.0f) / (60.0f / 360.0f);
        int   i = (int)h;
        float f = h - (float)i;
        float p = v * (1.0f - s);
        float q = v * (1.0f - s * f);
        float t = v * (1.0f - s * (1.0f - f));

        switch (i)
        {
        case 0: out_r = v; out_g = t; out_b = p; break;
        case 1: out_r = q; out_g = v; out_b = p; break;
        case 2: out_r = p; out_g = v; out_b = t; break;
        case 3: out_r = p; out_g = q; out_b = v; break;
        case 4: out_r = t; out_g = p; out_b = v; break;
        case 5: default: out_r = v; out_g = p; out_b = q; break;
        }
    }
#endif

    [[nodiscard]] std::tuple<IntOrFloat, IntOrFloat, IntOrFloat, IntOrFloat> GetCommaSeparatedNumbers(std::string_view stylePropVal, int& curr)
    {
        std::tuple<IntOrFloat, IntOrFloat, IntOrFloat, IntOrFloat> res;
        auto hasFourth = curr == 4;
        curr = SkipSpace(stylePropVal, curr);
        assert(stylePropVal[curr] == '(');
        curr++;

        auto valstart = curr;
        curr = SkipFDigits(stylePropVal, curr);
        std::get<0>(res) = ExtractNumber(stylePropVal.substr(valstart, curr - valstart), 0);
        curr = SkipSpace(stylePropVal, curr);
        assert(stylePropVal[curr] == ',');
        curr++;
        curr = SkipSpace(stylePropVal, curr);

        valstart = curr;
        curr = SkipFDigits(stylePropVal, curr);
        std::get<1>(res) = ExtractNumber(stylePropVal.substr(valstart, curr - valstart), 0);
        curr = SkipSpace(stylePropVal, curr);
        assert(stylePropVal[curr] == ',');
        curr++;
        curr = SkipSpace(stylePropVal, curr);

        valstart = curr;
        curr = SkipFDigits(stylePropVal, curr);
        std::get<2>(res) = ExtractNumber(stylePropVal.substr(valstart, curr - valstart), 0);
        curr = SkipSpace(stylePropVal, curr);

        if (hasFourth)
        {
            assert(stylePropVal[curr] == ',');
            curr++;
            curr = SkipSpace(stylePropVal, curr);

            valstart = curr;
            curr = SkipFDigits(stylePropVal, curr);
            std::get<3>(res) = ExtractNumber(stylePropVal.substr(valstart, curr - valstart), 0);
        }

        return res;
    }

    [[nodiscard]] uint32_t ExtractColor(std::string_view stylePropVal, uint32_t(*NamedColor)(const char*, void*), void* userData)
    {
        if (stylePropVal.size() >= 3u && AreSame(stylePropVal.substr(0, 3), "rgb"))
        {
            IntOrFloat r, g, b, a;
            auto hasAlpha = stylePropVal[3] == 'a' || stylePropVal[3] == 'A';
            int curr = hasAlpha ? 4 : 3;
            std::tie(r, g, b, a) = GetCommaSeparatedNumbers(stylePropVal, curr);
            auto isRelative = r.isFloat && g.isFloat && b.isFloat;
            a.value = isRelative ? hasAlpha ? a.value : 1.f :
                hasAlpha ? a.value : 255;

            assert(stylePropVal[curr] == ')');
            return isRelative ? ToRGBA(r.value, g.value, b.value, a.value) :
                ToRGBA((int)r.value, (int)g.value, (int)b.value, (int)a.value);
        }
        else if (stylePropVal.size() >= 3u && AreSame(stylePropVal.substr(0, 3), "hsv"))
        {
            IntOrFloat h, s, v;
            auto curr = 3;
            std::tie(h, s, v, std::ignore) = GetCommaSeparatedNumbers(stylePropVal, curr);

            assert(stylePropVal[curr] == ')');
            return ImColor::HSV(h.value, s.value, v.value);
        }
        else if (stylePropVal.size() >= 3u && AreSame(stylePropVal.substr(0, 3), "hsl"))
        {
            IntOrFloat h, s, l;
            auto curr = 3;
            std::tie(h, s, l, std::ignore) = GetCommaSeparatedNumbers(stylePropVal, curr);
            auto v = l.value + s.value * std::min(l.value, 1.f - l.value);
            s.value = v == 0.f ? 0.f : 2.f * (1.f - (l.value / v));

            assert(stylePropVal[curr] == ')');
            return ImColor::HSV(h.value, s.value, v);
        }
        else if (stylePropVal.size() >= 1u && stylePropVal[0] == '#')
        {
            return (uint32_t)ExtractIntFromHex(stylePropVal.substr(1), 0);
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

    std::pair<uint32_t, float> ExtractColorStop(std::string_view input, uint32_t(*NamedColor)(const char*, void*), void* userData)
    {
        auto idx = 0;
        std::pair<uint32_t, float> colorstop;

        idx = WholeWord(input, idx);
        colorstop.first = ExtractColor(input.substr(0, idx), NamedColor, userData);
        idx = SkipSpace(input, idx);

        if ((idx < (int)input.size()) && std::isdigit(input[idx]))
        {
            auto start = idx;
            idx = SkipDigits(input, start);
            colorstop.second = ExtractNumber(input.substr(start, idx - start), -1.f).value;
        }
        else colorstop.second = -1.f;

        return colorstop;
    }

    ColorGradient ExtractLinearGradient(std::string_view input, uint32_t(*NamedColor)(const char*, void*), void* userData)
    {
        ColorGradient gradient;
        auto idx = 15; // size of "linear-gradient" string

        if (idx < (int)input.size())
        {
            idx = SkipSpace(input, idx);
            assert(input[idx] == '(');
            idx++;
            idx = SkipSpace(input, idx);

            std::optional<std::pair<uint32_t, float>> lastStop = std::nullopt;
            auto firstPart = true;
            auto start = idx;
            auto total = 0.f, unspecified = 0.f;

            do
            {
                idx = SkipSpace(input, idx);

                auto start = idx;
                while ((idx < (int)input.size()) && (input[idx] != ',') && (input[idx] != ')')
                    && !std::isspace(input[idx])) idx++;
                auto part = input.substr(start, idx - start);
                idx = SkipSpace(input, idx);
                auto isEnd = input[idx] == ')';

                if ((idx < (int)input.size()) && (input[idx] == ',' || isEnd)) {
                    if (firstPart)
                    {
                        if (AreSame(input, "to right")) {
                            gradient.dir = ImGuiDir::ImGuiDir_Right;
                        }
                        else if (AreSame(input, "to left")) {
                            gradient.dir = ImGuiDir::ImGuiDir_Left;
                        }
                        else {
                            auto colorstop = ExtractColorStop(part, NamedColor, userData);
                            if (colorstop.second != -1.f) total += colorstop.second;
                            else unspecified += 1.f;
                            lastStop = colorstop;
                        }
                        firstPart = false;
                    }
                    else {
                        auto colorstop = ExtractColorStop(part, NamedColor, userData);
                        if (colorstop.second != -1.f) total += colorstop.second;
                        else unspecified += 1.f;

                        if (lastStop.has_value())
                        {
                            gradient.colorStops[gradient.totalStops] =
                                ColorStop{ lastStop.value().first, colorstop.first, colorstop.second };
                            gradient.totalStops = std::min(gradient.totalStops + 1, IM_RICHTEXT_MAX_COLORSTOPS-1);
                        }

                        lastStop = colorstop;
                    }
                }
                else break;

                if (isEnd) break;
                else if (input[idx] == ',') idx++;
            } while ((idx < (int)input.size()) && (input[idx] != ')'));

            unspecified -= 1.f;
            for (auto& colorstop : gradient.colorStops)
                if (colorstop.pos == -1.f) colorstop.pos = (100.f - total) / (100.f * unspecified);
                else colorstop.pos /= 100.f;
        }

        return gradient;
    }

    template <int maxsz>
    struct CaseInsensitiveHasher
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

    uint32_t GetColor(const char* name, void*)
    {
        const static std::unordered_map<std::string_view, uint32_t, CaseInsensitiveHasher<32>> Colors{
            { "black", ToRGBA(0, 0, 0) },
            { "silver", ToRGBA(192, 192, 192) },
            { "gray", ToRGBA(128, 128, 128) },
            { "white", ToRGBA(255, 255, 255) },
            { "maroon", ToRGBA(128, 0, 0) },
            { "red", ToRGBA(255, 0, 0) },
            { "purple", ToRGBA(128, 0, 128) },
            { "fuchsia", ToRGBA(255, 0, 255) },
            { "green", ToRGBA(0, 128, 0) },
            { "lime", ToRGBA(0, 255, 0) },
            { "olive", ToRGBA(128, 128, 0) },
            { "yellow", ToRGBA(255, 255, 0) },
            { "navy", ToRGBA(0, 0, 128) },
            { "blue", ToRGBA(0, 0, 255) },
            { "teal", ToRGBA(0, 128, 128) },
            { "aqua", ToRGBA(0, 255, 255) },
            { "aliceblue", ToRGBA(240, 248, 255) },
            { "antiquewhite", ToRGBA(250, 235, 215) },
            { "aquamarine", ToRGBA(127, 255, 212) },
            { "azure", ToRGBA(240, 255, 255) },
            { "beige", ToRGBA(245, 245, 220) },
            { "bisque", ToRGBA(255, 228, 196) },
            { "blanchedalmond", ToRGBA(255, 235, 205) },
            { "blueviolet", ToRGBA(138, 43, 226) },
            { "brown", ToRGBA(165, 42, 42) },
            { "burlywood", ToRGBA(222, 184, 135) },
            { "cadetblue", ToRGBA(95, 158, 160) },
            { "chartreuse", ToRGBA(127, 255, 0) },
            { "chocolate", ToRGBA(210, 105, 30) },
            { "coral", ToRGBA(255, 127, 80) },
            { "cornflowerblue", ToRGBA(100, 149, 237) },
            { "cornsilk", ToRGBA(255, 248, 220) },
            { "crimson", ToRGBA(220, 20, 60) },
            { "darkblue", ToRGBA(0, 0, 139) },
            { "darkcyan", ToRGBA(0, 139, 139) },
            { "darkgoldenrod", ToRGBA(184, 134, 11) },
            { "darkgray", ToRGBA(169, 169, 169) },
            { "darkgreen", ToRGBA(0, 100, 0) },
            { "darkgrey", ToRGBA(169, 169, 169) },
            { "darkkhaki", ToRGBA(189, 183, 107) },
            { "darkmagenta", ToRGBA(139, 0, 139) },
            { "darkolivegreen", ToRGBA(85, 107, 47) },
            { "darkorange", ToRGBA(255, 140, 0) },
            { "darkorchid", ToRGBA(153, 50, 204) },
            { "darkred", ToRGBA(139, 0, 0) },
            { "darksalmon", ToRGBA(233, 150, 122) },
            { "darkseagreen", ToRGBA(143, 188, 143) },
            { "darkslateblue", ToRGBA(72, 61, 139) },
            { "darkslategray", ToRGBA(47, 79, 79) },
            { "darkslategray", ToRGBA(47, 79, 79) },
            { "darkturquoise", ToRGBA(0, 206, 209) },
            { "darkviolet", ToRGBA(148, 0, 211) },
            { "deeppink", ToRGBA(255, 20, 147) },
            { "deepskyblue", ToRGBA(0, 191, 255) },
            { "dimgray", ToRGBA(105, 105, 105) },
            { "dimgrey", ToRGBA(105, 105, 105) },
            { "dodgerblue", ToRGBA(30, 144, 255) },
            { "firebrick", ToRGBA(178, 34, 34) },
            { "floralwhite", ToRGBA(255, 250, 240) },
            { "forestgreen", ToRGBA(34, 139, 34) },
            { "gainsboro", ToRGBA(220, 220, 220) },
            { "ghoshtwhite", ToRGBA(248, 248, 255) },
            { "gold", ToRGBA(255, 215, 0) },
            { "goldenrod", ToRGBA(218, 165, 32) },
            { "greenyellow", ToRGBA(173, 255, 47) },
            { "honeydew", ToRGBA(240, 255, 240) },
            { "hotpink", ToRGBA(255, 105, 180) },
            { "indianred", ToRGBA(205, 92, 92) },
            { "indigo", ToRGBA(75, 0, 130) },
            { "ivory", ToRGBA(255, 255, 240) },
            { "khaki", ToRGBA(240, 230, 140) },
            { "lavender", ToRGBA(230, 230, 250) },
            { "lavenderblush", ToRGBA(255, 240, 245) },
            { "lawngreen", ToRGBA(124, 252, 0) },
            { "lemonchiffon", ToRGBA(255, 250, 205) },
            { "lightblue", ToRGBA(173, 216, 230) },
            { "lightcoral", ToRGBA(240, 128, 128) },
            { "lightcyan", ToRGBA(224, 255, 255) },
            { "lightgoldenrodyellow", ToRGBA(250, 250, 210) },
            { "lightgray", ToRGBA(211, 211, 211) },
            { "lightgreen", ToRGBA(144, 238, 144) },
            { "lightgrey", ToRGBA(211, 211, 211) },
            { "lightpink", ToRGBA(255, 182, 193) },
            { "lightsalmon", ToRGBA(255, 160, 122) },
            { "lightseagreen", ToRGBA(32, 178, 170) },
            { "lightskyblue", ToRGBA(135, 206, 250) },
            { "lightslategray", ToRGBA(119, 136, 153) },
            { "lightslategrey", ToRGBA(119, 136, 153) },
            { "lightsteelblue", ToRGBA(176, 196, 222) },
            { "lightyellow", ToRGBA(255, 255, 224) },
            { "limegreen", ToRGBA(50, 255, 50) },
            { "linen", ToRGBA(250, 240, 230) },
            { "mediumaquamarine", ToRGBA(102, 205, 170) },
            { "mediumblue", ToRGBA(0, 0, 205) },
            { "mediumorchid", ToRGBA(186, 85, 211) },
            { "mediumpurple", ToRGBA(147, 112, 219) },
            { "mediumseagreen", ToRGBA(60, 179, 113) },
            { "mediumslateblue", ToRGBA(123, 104, 238) },
            { "mediumspringgreen", ToRGBA(0, 250, 154) },
            { "mediumturquoise", ToRGBA(72, 209, 204) },
            { "mediumvioletred", ToRGBA(199, 21, 133) },
            { "midnightblue", ToRGBA(25, 25, 112) },
            { "mintcream", ToRGBA(245, 255, 250) },
            { "mistyrose", ToRGBA(255, 228, 225) },
            { "moccasin", ToRGBA(255, 228, 181) },
            { "navajowhite", ToRGBA(255, 222, 173) },
            { "oldlace", ToRGBA(253, 245, 230) },
            { "olivedrab", ToRGBA(107, 142, 35) },
            { "orange", ToRGBA(255, 165, 0) },
            { "orangered", ToRGBA(255, 69, 0) },
            { "orchid", ToRGBA(218, 112, 214) },
            { "palegoldenrod", ToRGBA(238, 232, 170) },
            { "palegreen", ToRGBA(152, 251, 152) },
            { "paleturquoise", ToRGBA(175, 238, 238) },
            { "palevioletred", ToRGBA(219, 112, 147) },
            { "papayawhip", ToRGBA(255, 239, 213) },
            { "peachpuff", ToRGBA(255, 218, 185) },
            { "peru", ToRGBA(205, 133, 63) },
            { "pink", ToRGBA(255, 192, 203) },
            { "plum", ToRGBA(221, 160, 221) },
            { "powderblue", ToRGBA(176, 224, 230) },
            { "rosybrown", ToRGBA(188, 143, 143) },
            { "royalblue", ToRGBA(65, 105, 225) },
            { "saddlebrown", ToRGBA(139, 69, 19) },
            { "salmon", ToRGBA(250, 128, 114) },
            { "sandybrown", ToRGBA(244, 164, 96) },
            { "seagreen", ToRGBA(46, 139, 87) },
            { "seashell", ToRGBA(255, 245, 238) },
            { "sienna", ToRGBA(160, 82, 45) },
            { "skyblue", ToRGBA(135, 206, 235) },
            { "slateblue", ToRGBA(106, 90, 205) },
            { "slategray", ToRGBA(112, 128, 144) },
            { "slategrey", ToRGBA(112, 128, 144) },
            { "snow", ToRGBA(255, 250, 250) },
            { "springgreen", ToRGBA(0, 255, 127) },
            { "steelblue", ToRGBA(70, 130, 180) },
            { "tan", ToRGBA(210, 180, 140) },
            { "thistle", ToRGBA(216, 191, 216) },
            { "tomato", ToRGBA(255, 99, 71) },
            { "violet", ToRGBA(238, 130, 238) },
            { "wheat", ToRGBA(245, 222, 179) },
            { "whitesmoke", ToRGBA(245, 245, 245) },
            { "yellowgreen", ToRGBA(154, 205, 50) }
        };

        auto it = Colors.find(name);
        return it != Colors.end() ? it->second : uint32_t{ IM_COL32_BLACK };
    }

    Border ExtractBorder(std::string_view input, float ems, float percent, 
        uint32_t(*NamedColor)(const char*, void*), void* userData)
    {
        Border result;

        auto idx = WholeWord(input);
        result.thickness = ExtractFloatWithUnit(input.substr(0, idx), 1.f, ems, percent, 1.f);
        
        auto idx2 = WholeWord(input, idx + 1);
        auto type = input.substr(idx + 1, idx2);
        if (AreSame(type, "solid")) result.lineType = LineType::Solid;
        else if (AreSame(type, "dashed")) result.lineType = LineType::Dashed;
        else if (AreSame(type, "dotted")) result.lineType = LineType::Dotted;

        auto idx3 = WholeWord(input, idx2 + 1);
        auto color = input.substr(idx2 + 1, idx3);
        result.color = ExtractColor(color, NamedColor, userData);

        return result;
    }
    
    std::pair<std::string_view, bool> ExtractTag(const char* text, int end, char TagEnd,
        int& idx, bool& tagStart)
    {
        std::pair<std::string_view, bool> result;
        result.second = true;

        if (text[idx] == '/')
        {
            tagStart = false;
            idx++;
        }
        else if (!std::isalnum(text[idx]))
        {
            result.second = false;
            return result;
        }

        auto begin = idx;
        while ((idx < end) && !std::isspace(text[idx]) && (text[idx] != TagEnd)) idx++;

        if (idx - begin == 0)
        {
            result.second = false;
            return result;
        }

        result.first = std::string_view{ text + begin, (std::size_t)(idx - begin) };
        if (result.first.back() == '/') result.first = result.first.substr(0, result.first.size() - 1u);

        if (!tagStart)
        {
            if (text[idx] == TagEnd) idx++;

            if (result.first.empty())
            {
                result.second = false;
                return result;
            }
        }

        idx = SkipSpace(text, idx, end);
        return result;
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
            //ERROR("Quoted string invalid... [%.*s] at %d\n", (int)(idx - begin), text + begin, idx);
        }

        return std::nullopt;
    }

    void ParseRichText(const char* text, const char* textend, char TagStart, char TagEnd, ITagVisitor& visitor)
    {
        int end = (int)(textend - text), start = 0;
        start = SkipSpace(text, start, end);
        auto isPreformattedContent = false;
        std::string_view lastTag = "";

        for (auto idx = start; idx < end;)
        {
            if (text[idx] == TagStart)
            {
                idx++;
                auto tagStart = true, selfTerminatingTag = false;
                auto [currTag, status] = ExtractTag(text, end, TagEnd, idx, tagStart);
                if (!status) { visitor.Error(currTag); return; }

                isPreformattedContent = visitor.IsPreformattedContent(currTag);
                lastTag = currTag;

                if (tagStart)
                {
                    if (!visitor.TagStart(currTag)) return;

                    while ((idx < end) && (text[idx] != TagEnd) && (text[idx] != '/'))
                    {
                        auto begin = idx;
                        while ((idx < end) && (text[idx] != '=') && !std::isspace(text[idx]) && (text[idx] != '/')) idx++;

                        if (text[idx] != '/')
                        {
                            auto attribName = std::string_view{ text + begin, (std::size_t)(idx - begin) };
                            
                            idx = SkipSpace(text, idx, end);
                            if (text[idx] == '=') idx++;
                            idx = SkipSpace(text, idx, end);
                            auto attribValue = GetQuotedString(text, idx, end);
                            if (!visitor.Attribute(attribName, attribValue)) return;
                        }
                    }

                    if (text[idx] == TagEnd) idx++;
                    if (text[idx] == '/' && ((idx + 1) < end) && text[idx + 1] == TagEnd) idx += 2;
                }

                selfTerminatingTag = (text[idx - 2] == '/' && text[idx - 1] == TagEnd) || visitor.IsSelfTerminating(currTag);

                if (selfTerminatingTag || !tagStart) {
                    if (!visitor.TagEnd(currTag, selfTerminatingTag)) return;
                }
                else if (!selfTerminatingTag && tagStart)
                    if (!visitor.TagStartDone()) return;
            }
            else
            {
                auto begin = idx;

                if (isPreformattedContent)
                {
                    static char EndTag[64] = { 0 };
                    EndTag[0] = TagStart; EndTag[1] = '/';
                    std::memcpy(EndTag + 2, lastTag.data(), lastTag.size());
                    EndTag[2u + lastTag.size()] = TagEnd;
                    EndTag[3u + lastTag.size()] = 0;

                    while (((idx + (int)(lastTag.size() + 3u)) < end) &&
                        AreSame(std::string_view{ text + idx, lastTag.size() + 3u }, EndTag)) idx++;
                    std::string_view content{ text + begin, (std::size_t)(idx - begin) };

                    if (!visitor.Content(content)) return;
                }
                else
                {
                    while ((idx < end) && (text[idx] != TagStart)) idx++;
                    std::string_view content{ text + begin, (std::size_t)(idx - begin) };
                    if (!visitor.Content(content)) return;
                }
            }
        }

        visitor.Finalize();
    }

    void IRenderer::DrawDefaultBullet(BulletType type, ImVec2 initpos, const BoundedBox& bounds, uint32_t color, float bulletsz)
    {
        switch (type)
        {
        case BulletType::Circle: {
            ImVec2 center = bounds.center(initpos);
            DrawCircle(center, bulletsz * 0.5f, color, false);
            break;
        }

        case BulletType::Disk: {
            ImVec2 center = bounds.center(initpos);
            DrawCircle(center, bulletsz * 0.5f, color, true);
            break;
        }

        case BulletType::Square: {
            DrawRect(bounds.start(initpos), bounds.end(initpos), color, true);
            break;
        }

        case BulletType::Concentric: {
            ImVec2 center = bounds.center(initpos);
            DrawCircle(center, bulletsz * 0.5f, color, false);
            DrawCircle(center, bulletsz * 0.4f, color, true);
            break;
        }

        case BulletType::Triangle: {
            auto startpos = bounds.start(initpos);
            auto offset = bulletsz * 0.25f;
            ImVec2 a{ startpos.x, startpos.y },
                b{ startpos.x + bulletsz, startpos.y + (bulletsz * 0.5f) },
                c{ startpos.x, startpos.y + bulletsz };
            DrawTriangle(a, b, c, color, true);
            break;
        }

        case BulletType::Arrow: {
            auto startpos = bounds.start(initpos);
            auto bsz2 = bulletsz * 0.5f;
            auto bsz3 = bulletsz * 0.33333f;
            auto bsz6 = bsz3 * 0.5f;
            auto bsz38 = bulletsz * 0.375f;
            ImVec2 points[7];
            points[0] = { startpos.x, startpos.y + bsz38 };
            points[1] = { startpos.x + bsz2, startpos.y + bsz38 };
            points[2] = { startpos.x + bsz2, startpos.y + bsz6 };
            points[3] = { startpos.x + bulletsz, startpos.y + bsz2 };
            points[4] = { startpos.x + bsz2, startpos.y + bulletsz - bsz6 };
            points[5] = { startpos.x + bsz2, startpos.y + bulletsz - bsz38 };
            points[6] = { startpos.x, startpos.y + bulletsz - bsz38 };
            DrawRect(points[0], points[5], color, true);
            DrawTriangle(points[2], points[3], points[4], color, true);
            break;
        }

        case BulletType::CheckMark: {
            auto startpos = bounds.start(initpos);
            auto bsz3 = (bulletsz * 0.25f);
            auto thickness = bulletsz * 0.2f;
            ImVec2 points[3];
            points[0] = { startpos.x, startpos.y + (2.5f * bsz3) };
            points[1] = { startpos.x + (bulletsz * 0.3333f), startpos.y + bulletsz };
            points[2] = { startpos.x + bulletsz, startpos.y + bsz3 };
            DrawPolyline(points, 3, color, thickness);
            break;
        }

        // TODO: Fix this
        case BulletType::CheckBox: {
            auto startpos = bounds.start(initpos);
            auto checkpos = ImVec2{ startpos.x + (bulletsz * 0.25f), startpos.y + (bulletsz * 0.25f) };
            bulletsz *= 0.75f;
            auto bsz3 = (bulletsz * 0.25f);
            auto thickness = bulletsz * 0.25f;
            ImVec2 points[3];
            points[0] = { checkpos.x, checkpos.y + (2.5f * bsz3) };
            points[1] = { checkpos.x + (bulletsz * 0.3333f), checkpos.y + bulletsz };
            points[2] = { checkpos.x + bulletsz, checkpos.y + bsz3 };
            DrawPolyline(points, 3, color, thickness);
            DrawRect(startpos, bounds.end(initpos), color, false, thickness);
            break;
        }

        default:
            break;
        }
    }
}