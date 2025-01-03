#include "imrichtextcolor.h"

#include <unordered_map>
#include <string_view>
#include <cctype>

namespace ImRichText
{
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
}