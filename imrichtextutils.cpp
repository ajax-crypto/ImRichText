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

    [[nodiscard]] ImColor ExtractColor(std::string_view stylePropVal, ImColor(*NamedColor)(const char*, void*), void* userData)
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
            return isRelative ? ImColor{ r.value, g.value, b.value, a.value } :
                ImColor{ (int)r.value, (int)g.value, (int)b.value, (int)a.value };
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
            ImU32 color32 = (ImU32)ExtractIntFromHex(stylePropVal.substr(1), 0);
            return ImColor{ color32 };
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

    std::pair<ImColor, float> ExtractColorStop(std::string_view input, ImColor(*NamedColor)(const char*, void*), void* userData)
    {
        auto idx = 0;
        std::pair<ImColor, float> colorstop;

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

    ColorGradient ExtractLinearGradient(std::string_view input, ImColor(*NamedColor)(const char*, void*), void* userData)
    {
        ColorGradient gradient;
        auto idx = 15; // size of "linear-gradient" string

        if (idx < (int)input.size())
        {
            idx = SkipSpace(input, idx);
            assert(input[idx] == '(');
            idx++;
            idx = SkipSpace(input, idx);

            std::optional<std::pair<ImColor, float>> lastStop = std::nullopt;
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
                            gradient.reverseOrder = true;
                            gradient.dir = ImGuiDir::ImGuiDir_Left;
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
                        if (lastStop.has_value()) gradient.colorStops.emplace_back(
                            ColorStop{ lastStop.value().first, colorstop.first, colorstop.second });
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

    ImColor GetColor(const char* name, void*)
    {
        const static std::unordered_map<std::string_view, ImColor, CaseInsensitiveHasher<32>> Colors{
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
            { "darkgreen", ImColor(0, 100, 0) },
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

        auto it = Colors.find(name);
        return it != Colors.end() ? it->second : ImColor{ IM_COL32_BLACK };
    }

    void DrawPolyFilledMultiColor(ImDrawList* drawList, const ImVec2* points, const ImU32* col, const int points_count)
    {
        const ImVec2 uv = drawList->_Data->TexUvWhitePixel;

        if (drawList->Flags & ImDrawListFlags_AntiAliasedFill)
        {
            // Anti-aliased Fill
            const float AA_SIZE = 1.0f;
            const int idx_count = (points_count - 2) * 3 + points_count * 6;
            const int vtx_count = (points_count * 2);
            drawList->PrimReserve(idx_count, vtx_count);

            // Add indexes for fill
            unsigned int vtx_inner_idx = drawList->_VtxCurrentIdx;
            unsigned int vtx_outer_idx = drawList->_VtxCurrentIdx + 1;
            for (int i = 2; i < points_count; i++)
            {
                drawList->_IdxWritePtr[0] = (ImDrawIdx)(vtx_inner_idx); 
                drawList->_IdxWritePtr[1] = (ImDrawIdx)(vtx_inner_idx + ((i - 1) << 1)); 
                drawList->_IdxWritePtr[2] = (ImDrawIdx)(vtx_inner_idx + (i << 1));
                drawList->_IdxWritePtr += 3;
            }

            // Compute normals
            ImVec2* temp_normals = (ImVec2*)alloca(points_count * sizeof(ImVec2));
            for (int i0 = points_count - 1, i1 = 0; i1 < points_count; i0 = i1++)
            {
                const ImVec2& p0 = points[i0];
                const ImVec2& p1 = points[i1];
                ImVec2 diff = p1 - p0;
                diff *= ImInvLength(diff, 1.0f);
                temp_normals[i0].x = diff.y;
                temp_normals[i0].y = -diff.x;
            }

            for (int i0 = points_count - 1, i1 = 0; i1 < points_count; i0 = i1++)
            {
                // Average normals
                const ImVec2& n0 = temp_normals[i0];
                const ImVec2& n1 = temp_normals[i1];
                ImVec2 dm = (n0 + n1) * 0.5f;
                float dmr2 = dm.x * dm.x + dm.y * dm.y;
                if (dmr2 > 0.000001f)
                {
                    float scale = 1.0f / dmr2;
                    if (scale > 100.0f) scale = 100.0f;
                    dm *= scale;
                }
                dm *= AA_SIZE * 0.5f;

                // Add vertices
                drawList->_VtxWritePtr[0].pos = (points[i1] - dm); 
                drawList->_VtxWritePtr[0].uv = uv; drawList->_VtxWritePtr[0].col = col[i1];        // Inner
                drawList->_VtxWritePtr[1].pos = (points[i1] + dm); 
                drawList->_VtxWritePtr[1].uv = uv; drawList->_VtxWritePtr[1].col = col[i1] & ~IM_COL32_A_MASK;  // Outer
                drawList->_VtxWritePtr += 2;

                // Add indexes for fringes
                drawList->_IdxWritePtr[0] = (ImDrawIdx)(vtx_inner_idx + (i1 << 1)); 
                drawList->_IdxWritePtr[1] = (ImDrawIdx)(vtx_inner_idx + (i0 << 1)); 
                drawList->_IdxWritePtr[2] = (ImDrawIdx)(vtx_outer_idx + (i0 << 1));
                drawList->_IdxWritePtr[3] = (ImDrawIdx)(vtx_outer_idx + (i0 << 1)); 
                drawList->_IdxWritePtr[4] = (ImDrawIdx)(vtx_outer_idx + (i1 << 1)); 
                drawList->_IdxWritePtr[5] = (ImDrawIdx)(vtx_inner_idx + (i1 << 1));
                drawList->_IdxWritePtr += 6;
            }

            drawList->_VtxCurrentIdx += (ImDrawIdx)vtx_count;
        }
        else
        {
            // Non Anti-aliased Fill
            const int idx_count = (points_count - 2) * 3;
            const int vtx_count = points_count;
            drawList->PrimReserve(idx_count, vtx_count);
            for (int i = 0; i < vtx_count; i++)
            {
                drawList->_VtxWritePtr[0].pos = points[i]; 
                drawList->_VtxWritePtr[0].uv = uv; 
                drawList->_VtxWritePtr[0].col = col[i];
                drawList->_VtxWritePtr++;
            }
            for (int i = 2; i < points_count; i++)
            {
                drawList->_IdxWritePtr[0] = (ImDrawIdx)(drawList->_VtxCurrentIdx);
                drawList->_IdxWritePtr[1] = (ImDrawIdx)(drawList->_VtxCurrentIdx + i - 1); 
                drawList->_IdxWritePtr[2] = (ImDrawIdx)(drawList->_VtxCurrentIdx + i);
                drawList->_IdxWritePtr += 3;
            }

            drawList->_VtxCurrentIdx += (ImDrawIdx)vtx_count;
        }
    }

    void DrawBullet(ImDrawList* drawList, ImVec2 initpos, const BoundedBox& bounds, BulletType type, ImColor color, float bulletsz)
    {
        switch (type)
        {
        case BulletType::Circle: {
            ImVec2 center = bounds.center(initpos);
            drawList->AddCircle(center, bulletsz * 0.5f, color);
            break;
        }

        case BulletType::Disk: {
            ImVec2 center = bounds.center(initpos);
            drawList->AddCircleFilled(center, bulletsz * 0.5f, color);
            break;
        }

        case BulletType::Square: {
            drawList->AddRectFilled(bounds.start(initpos), bounds.end(initpos), color);
            break;
        }

        case BulletType::Concentric: {
            ImVec2 center = bounds.center(initpos);
            drawList->AddCircle(center, bulletsz * 0.5f, color);
            drawList->AddCircleFilled(center, bulletsz * 0.4f, color);
            break;
        }

        case BulletType::Triangle: {
            auto startpos = bounds.start(initpos);
            auto offset = bulletsz * 0.25f;
            ImVec2 a{ startpos.x, startpos.y },
                b{ startpos.x + bulletsz, startpos.y + (bulletsz * 0.5f) },
                c{ startpos.x, startpos.y + bulletsz };
            drawList->AddTriangleFilled(a, b, c, color);
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
            drawList->AddRectFilled(points[0], points[5], color);
            drawList->AddTriangleFilled(points[2], points[3], points[4], color);
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
            drawList->AddPolyline(points, 3, color, 0, thickness);
            break;
        }

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
            drawList->AddPolyline(points, 3, color, 0, thickness);
            drawList->AddRect(startpos, bounds.end(initpos), color, thickness);
            break;
        }

        default:
            break;
        }
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
                    //LOG("Entering Tag: <%.*s>\n", (int)currTag.size(), currTag.data());
                    if (!visitor.TagStart(currTag)) return;

                    while ((idx < end) && (text[idx] != TagEnd) && (text[idx] != '/'))
                    {
                        auto begin = idx;
                        while ((idx < end) && (text[idx] != '=') && !std::isspace(text[idx]) && (text[idx] != '/')) idx++;

                        if (text[idx] != '/')
                        {
                            auto attribName = std::string_view{ text + begin, (std::size_t)(idx - begin) };
                            //LOG("Attribute: %.*s\n", (int)attribName.size(), attribName.data());

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
}