#define _USE_MATH_DEFINES
#include <math.h>

#include "imrichtextimpl.h"
#include "imrichtext.h"

#include <cctype>

namespace ImRichText
{
    // This only returns the bytes size of "codepoints" as per UTF-8 encoding
    // This does not return the grapheme cluster size w.r.t bytes
    static int UTF8CharSize(unsigned char leading)
    {
        constexpr unsigned char FourByteChar = 0b11110000;
        constexpr unsigned char ThreeByteChar = 0b11100000;
        constexpr unsigned char TwoByteChar = 0b11000000;
        constexpr unsigned char OneByteChar = 0b01111111;
        return (leading & OneByteChar) == 0 ? 1 :
            (leading & FourByteChar) == FourByteChar ? 4 :
            (leading & ThreeByteChar) == ThreeByteChar ? 3 : 2;
    }

    std::tuple<bool, bool, std::string_view> AddEscapeSequences(const std::string_view content,
        Span<std::pair<std::string_view, std::string_view>> escapeCodes, int& curridx, 
        char escapeStart, char escapeEnd)
    {
        auto hasEscape = false;
        auto isNewLine = false;
        std::string_view value;

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
                    value = pair.second;
                }

                curridx += sz + 1;
                hasEscape = true;
                break;
            }
        }

        return { hasEscape, isNewLine, value };
    }

    ASCIITextShaper* ASCIITextShaper::Instance()
    {
        static ASCIITextShaper shaper;
        return &shaper;
    }

    void ASCIITextShaper::ShapeText(float availwidth, const Span<std::string_view>& words, 
        StyleAccessor accessor, LineRecorder lineRecorder, WordRecorder wordRecorder, const RenderConfig& config, 
        void* userdata)
    {
        auto currentx = 0.f;

        for (auto idx = 0; idx < words.sz; ++idx)
        {
            auto font = accessor(idx, userdata);
            auto sz = config.Renderer->GetTextSize(words[idx], font.font, font.sz);

            if ((sz.x > availwidth) && (font.wb == WordBreakBehavior::BreakWord || font.wb == WordBreakBehavior::BreakAll))
            {
                lineRecorder(idx, userdata);
                currentx = 0.f;

                ImVec2 currsz{ 0.f, 0.f };
                auto lastidx = 0, chidx = 1;

                for (; chidx < (int)words[idx].size(); chidx++)
                {
                    auto chsz = config.Renderer->GetTextSize(words[idx].substr(chidx, 1), font.font, font.sz);

                    if (currsz.x + chsz.x > availwidth)
                    {
                        wordRecorder(idx, words[idx].substr(lastidx, (std::size_t)(chidx - lastidx)), currsz, userdata);
                        lineRecorder(idx, userdata);
                        currsz.x = 0.f;
                        lastidx = chidx - 1;
                    }
                }

                wordRecorder(idx, words[idx].substr(lastidx, (std::size_t)(chidx - lastidx)), currsz, userdata);
                currentx += currsz.x;
            }
            else
            {
                if (currentx + sz.x > availwidth)
                {
                    lineRecorder(idx, userdata);
                    currentx = 0.f;
                }

                wordRecorder(idx, words[idx], sz, userdata);
                currentx += sz.x;
            }
        }
    }

    void ASCIITextShaper::SegmentText(std::string_view content, WhitespaceCollapseBehavior wsbhv, 
        LineRecorder lineRecorder, WordRecorder wordRecorder, const RenderConfig& config, 
        bool ignoreLineBreaks, bool ignoreEscapeCodes, void* userdata)
    {
        auto to = 0;

        while (to < (int)content.size())
        {
            // If this assert hits, then text is not ASCII!
            assert(content[to] > 0);

            if (content[to] == '\n')
            {
                if (ignoreLineBreaks) { to++; continue; }

                switch (wsbhv)
                {
                case ImRichText::WhitespaceCollapseBehavior::PreserveSpaces: [[fallthrough]];
                case ImRichText::WhitespaceCollapseBehavior::BreakSpaces: [[fallthrough]];
                case ImRichText::WhitespaceCollapseBehavior::Collapse:
                {
                    auto from = to;
                    while ((to < (int)content.size()) && (content[to] == '\n')) to++;
                    to--;
                    break;
                } 
                case ImRichText::WhitespaceCollapseBehavior::PreserveBreaks: [[fallthrough]];
                case ImRichText::WhitespaceCollapseBehavior::Preserve:
                {
                    while ((to < (int)content.size()) && (content[to] == '\n'))
                    {
                        lineRecorder(-1, userdata);
                        to++;
                    }

                    to--;
                    break;
                }
                default: break;
                }
                
            }
            else if (std::isspace(content[to]))
            {
                switch (wsbhv)
                {
                case ImRichText::WhitespaceCollapseBehavior::PreserveBreaks: [[fallthrough]];
                case ImRichText::WhitespaceCollapseBehavior::Collapse:
                {
                    auto from = to;
                    to = SkipSpace(content, to);
                    to--;
                    break;
                }
                case ImRichText::WhitespaceCollapseBehavior::Preserve: [[fallthrough]];
                case ImRichText::WhitespaceCollapseBehavior::PreserveSpaces: [[fallthrough]];
                case ImRichText::WhitespaceCollapseBehavior::BreakSpaces:
                {
                    auto from = to;
                    to = SkipSpace(content, to);
                    wordRecorder(-1, content.substr(from, (std::size_t)(to - from)), {}, userdata);
                    to--;
                    break;
                }
                default:
                    break;
                }
            }
            else if (!ignoreEscapeCodes && (content[to] == config.EscapeSeqStart))
            {
                to++;
                auto [hasEscape, isNewLine, substitute] = AddEscapeSequences(content, 
                    Span{ EscapeCodes, 6 }, to, config.EscapeSeqStart, config.EscapeSeqEnd);

                if (isNewLine && !ignoreLineBreaks)
                {
                    lineRecorder(-1, userdata);
                }
                else if (hasEscape)
                {
                    wordRecorder(-1, substitute, {}, userdata);
                }
                else
                {
                    auto from = to;
                    while ((to < (int)content.size()) && std::isgraph(content[to]) &&
                        (content[to] != config.EscapeSeqStart)) to++;
                    if ((to < (int)content.size()) && !std::isspace(content[to])) to--;

                    wordRecorder(-1, content.substr(from, (std::size_t)(to - from + 1)), {}, userdata);
                }
            }
            else
            {
                auto from = to;
                while ((to < (int)content.size()) && std::isgraph(content[to]) &&
                    (ignoreEscapeCodes || (!ignoreEscapeCodes  && (content[to] != config.EscapeSeqStart)))) to++;
                if ((to < (int)content.size()) && !std::isspace(content[to])) to--;

                wordRecorder(-1, content.substr(from, (std::size_t)(to - from + 1)), {}, userdata);
            }

            to++;
        }
    }

    // Dummy impl for ASCII, unused
    int ASCIITextShaper::NextGraphemeCluster(const char* from, const char* end) const
    {
        return 1;
    }

    // Dummy impl for ASCII, unused
    int ASCIITextShaper::NextWordBreak(const char* from, const char* end) const
    {
        return 1;
    }

    // Dummy impl for ASCII, unused
    int ASCIITextShaper::NextLineBreak(const char* from, const char* end) const
    {
        return 1;
    }

    const std::pair<std::string_view, std::string_view> ASCIITextShaper::EscapeCodes[6]{
        { "Tab", u8"\t" }, { "NewLine", u8"\n" }, { "nbsp", u8" " },
        { "gt", u8">" }, { "lt", u8"<" }, { "amp", u8"&" }
    };

    ASCIISymbolTextShaper* ASCIISymbolTextShaper::Instance()
    {
        static ASCIISymbolTextShaper shaper;
        return &shaper;
    }

    void ASCIISymbolTextShaper::ShapeText(float availwidth, const Span<std::string_view>& words, 
        StyleAccessor accessor, LineRecorder lineRecorder, WordRecorder wordRecorder, 
        const RenderConfig& config, void* userdata)
    {
        auto currentx = 0.f;

        for (auto idx = 0; idx < words.sz; ++idx)
        {
            auto font = accessor(idx, userdata);
            auto sz = config.Renderer->GetTextSize(words[idx], font.font, font.sz);

            if ((sz.x > availwidth) && (font.wb == WordBreakBehavior::BreakWord || font.wb == WordBreakBehavior::BreakAll))
            {
                lineRecorder(idx, userdata);
                currentx = 0.f;

                ImVec2 currsz{ 0.f, 0.f };
                auto lastidx = 0, chidx = 1;

                for (; chidx < (int)words[idx].size();)
                {
                    auto charsz = NextGraphemeCluster(words[idx].data() + lastidx, words[idx].data() + words[idx].size());
                    auto chsz = config.Renderer->GetTextSize(words[idx].substr(chidx, charsz), font.font, font.sz);

                    if (currsz.x + chsz.x > availwidth)
                    {
                        wordRecorder(idx, words[idx].substr(lastidx, (std::size_t)(chidx - lastidx)), currsz, userdata);
                        lineRecorder(idx, userdata);
                        currsz.x = 0.f;
                        lastidx = chidx;
                    }

                    chidx += charsz;
                }

                wordRecorder(idx, words[idx].substr(lastidx, (std::size_t)(chidx - lastidx)), currsz, userdata);
                currentx += currsz.x;
            }
            else
            {
                if (currentx + sz.x > availwidth)
                {
                    lineRecorder(idx, userdata);
                    currentx = 0.f;
                }

                wordRecorder(idx, words[idx], sz, userdata);
                currentx += sz.x;
            }
        }
    }

    void ASCIISymbolTextShaper::SegmentText(std::string_view content, WhitespaceCollapseBehavior wsbhv, 
        LineRecorder lineRecorder, WordRecorder wordRecorder, const RenderConfig& config, bool ignoreLineBreaks, 
        bool ignoreEscapeCodes, void* userdata)
    {
        auto to = 0;

        while (to < (int)content.size())
        {
            if (content[to] == '\n')
            {
                if (ignoreLineBreaks) { to++; continue; }

                switch (wsbhv)
                {
                case ImRichText::WhitespaceCollapseBehavior::PreserveSpaces: [[fallthrough]];
                case ImRichText::WhitespaceCollapseBehavior::BreakSpaces: [[fallthrough]];
                case ImRichText::WhitespaceCollapseBehavior::Collapse:
                {
                    auto from = to;
                    while ((to < (int)content.size()) && (content[to] == '\n')) to++;
                    to--;
                    break;
                }
                case ImRichText::WhitespaceCollapseBehavior::PreserveBreaks: [[fallthrough]];
                case ImRichText::WhitespaceCollapseBehavior::Preserve:
                {
                    while ((to < (int)content.size()) && (content[to] == '\n'))
                    {
                        lineRecorder(-1, userdata);
                        to++;
                    }

                    to--;
                    break;
                }
                default: break;
                }

            }
            else if (std::isspace(content[to]))
            {
                switch (wsbhv)
                {
                case ImRichText::WhitespaceCollapseBehavior::PreserveBreaks: [[fallthrough]];
                case ImRichText::WhitespaceCollapseBehavior::Collapse:
                {
                    auto from = to;
                    to = SkipSpace(content, to);
                    to--;
                    break;
                }
                case ImRichText::WhitespaceCollapseBehavior::Preserve: [[fallthrough]];
                case ImRichText::WhitespaceCollapseBehavior::PreserveSpaces: [[fallthrough]];
                case ImRichText::WhitespaceCollapseBehavior::BreakSpaces:
                {
                    auto from = to;
                    to = SkipSpace(content, to);
                    wordRecorder(-1, content.substr(from, (std::size_t)(to - from)), {}, userdata);
                    to--;
                    break;
                }
                default:
                    break;
                }
            }
            else if (!ignoreEscapeCodes && (content[to] == config.EscapeSeqStart))
            {
                to++;
                auto [hasEscape, isNewLine, substitute] = AddEscapeSequences(content,
                    Span{ EscapeCodes, 6 }, to, config.EscapeSeqStart, config.EscapeSeqEnd);

                if (isNewLine && !ignoreLineBreaks)
                {
                    lineRecorder(-1, userdata);
                }
                else if (hasEscape)
                {
                    wordRecorder(-1, substitute, {}, userdata);
                }
                else
                {
                    auto from = to;
                    while ((to < (int)content.size()) && std::isgraph(content[to]) &&
                        (content[to] != config.EscapeSeqStart)) to++;
                    if ((to < (int)content.size()) && !std::isspace(content[to])) to--;

                    wordRecorder(-1, content.substr(from, (std::size_t)(to - from + 1)), {}, userdata);
                }
            }
            else
            {
                auto from = to;
                while ((to < (int)content.size()) && std::isgraph(content[to]) &&
                    (ignoreEscapeCodes || (!ignoreEscapeCodes && (content[to] != config.EscapeSeqStart)))) to++;
                if ((to < (int)content.size()) && !std::isspace(content[to])) to--;

                wordRecorder(-1, content.substr(from, (std::size_t)(to - from + 1)), {}, userdata);
            }

            to++;
        }
    }

    int ASCIISymbolTextShaper::NextGraphemeCluster(const char* from, const char* end) const
    {
        return UTF8CharSize(*from);
    }

    int ASCIISymbolTextShaper::NextWordBreak(const char* from, const char* end) const
    {
        auto initial = from;
        while ((from < end) && (*from != ' ' && *from != '\t')) from += UTF8CharSize(*from);
        return from - initial;
    }

    int ASCIISymbolTextShaper::NextLineBreak(const char* from, const char* end) const
    {
        auto initial = from;
        while ((from < end) && (*from != '\n')) from += UTF8CharSize(*from);
        return from - initial;
    }

    const std::pair<std::string_view, std::string_view> ASCIISymbolTextShaper::EscapeCodes[11] = {
        { "Tab", u8"\t" }, { "NewLine", u8"\n" }, { "nbsp", u8" " },
        { "gt", u8">" }, { "lt", u8"<" },
        { "amp", u8"&" }, { "copy", u8"©" }, { "reg", u8"®" },
        { "deg", u8"°" }, { "micro", u8"μ" }, { "trade", u8"™" }
    };

#ifdef IM_RICHTEXT_TARGET_IMGUI

    ImGuiRenderer::ImGuiRenderer(RenderConfig& cfg)
        : config{ cfg } {}

    void ImGuiRenderer::SetClipRect(ImVec2 startpos, ImVec2 endpos)
    {
        ImGui::PushClipRect(startpos, endpos, true);
    }

    void ImGuiRenderer::ResetClipRect()
    {
        ImGui::PopClipRect();
    }

    void ImGuiRenderer::DrawLine(ImVec2 startpos, ImVec2 endpos, uint32_t color, float thickness)
    {
        ((ImDrawList*)UserData)->AddLine(startpos, endpos, color, thickness);
    }

    void ImGuiRenderer::DrawPolyline(ImVec2* points, int sz, uint32_t color, float thickness)
    {
        ((ImDrawList*)UserData)->AddPolyline(points, sz, color, 0, thickness);
    }

    void ImGuiRenderer::DrawTriangle(ImVec2 pos1, ImVec2 pos2, ImVec2 pos3, uint32_t color, bool filled, bool thickness)
    {
        filled ? ((ImDrawList*)UserData)->AddTriangleFilled(pos1, pos2, pos3, color) :
            ((ImDrawList*)UserData)->AddTriangle(pos1, pos2, pos3, color, thickness);
    }

    void ImGuiRenderer::DrawRect(ImVec2 startpos, ImVec2 endpos, uint32_t color, bool filled, float thickness)
    {
        if (thickness > 0.f || filled)
        {
            filled ? ((ImDrawList*)UserData)->AddRectFilled(startpos, endpos, color) :
                ((ImDrawList*)UserData)->AddRect(startpos, endpos, color, 0.f, 0, thickness);
        }
    }

    void ImGuiRenderer::DrawRoundedRect(ImVec2 startpos, ImVec2 endpos, uint32_t color, bool filled, 
        float topleftr, float toprightr, float bottomrightr, float bottomleftr, float thickness)
    {
        auto isUniformRadius = (topleftr == toprightr && toprightr == bottomrightr && bottomrightr == bottomleftr) ||
            ((topleftr + toprightr + bottomrightr + bottomleftr) == 0.f);

        if (isUniformRadius)
        {
            auto drawflags = 0;

            if (topleftr > 0.f) drawflags |= ImDrawFlags_RoundCornersTopLeft;
            if (toprightr > 0.f) drawflags |= ImDrawFlags_RoundCornersTopRight;
            if (bottomrightr > 0.f) drawflags |= ImDrawFlags_RoundCornersBottomRight;
            if (bottomleftr > 0.f) drawflags |= ImDrawFlags_RoundCornersBottomLeft;

            filled ? ((ImDrawList*)UserData)->AddRectFilled(startpos, endpos, color, toprightr, drawflags) :
                ((ImDrawList*)UserData)->AddRect(startpos, endpos, color, toprightr, drawflags, thickness);
        }
        else
        {
            auto& dl = *((ImDrawList*)UserData);
            auto minlength = std::min(endpos.x - startpos.x, endpos.y - startpos.y);
            topleftr = std::min(topleftr, minlength);
            toprightr = std::min(toprightr, minlength);
            bottomrightr = std::min(bottomrightr, minlength);
            bottomleftr = std::min(bottomleftr, minlength);

            dl.PathClear();
            dl.PathLineTo(ImVec2{ startpos.x, endpos.y - bottomleftr });
            dl.PathLineTo(ImVec2{ startpos.x, startpos.y + topleftr });
            if (topleftr > 0.f) dl.PathArcToFast(ImVec2{ startpos.x + topleftr, startpos.y + topleftr }, topleftr, 6, 9);
            dl.PathLineTo(ImVec2{ endpos.x - toprightr, startpos.y });
            if (toprightr > 0.f) dl.PathArcToFast(ImVec2{ endpos.x - toprightr, startpos.y + toprightr }, toprightr, 9, 12);
            dl.PathLineTo(ImVec2{ endpos.x, endpos.y - bottomrightr });
            if (bottomrightr > 0.f) dl.PathArcToFast(ImVec2{ endpos.x - bottomrightr, endpos.y - bottomrightr }, bottomrightr, 0, 3);
            dl.PathLineTo(ImVec2{ startpos.x - bottomleftr, endpos.y });
            if (bottomleftr > 0.f) dl.PathArcToFast(ImVec2{ startpos.x + bottomleftr, endpos.y - bottomleftr }, bottomleftr, 3, 6);
            
            filled ? dl.PathFillConvex(color) : dl.PathStroke(color, 0, thickness);
        }
    }

    void ImGuiRenderer::DrawRectGradient(ImVec2 startpos, ImVec2 endpos, uint32_t topleftcolor, uint32_t toprightcolor, uint32_t bottomrightcolor, uint32_t bottomleftcolor)
    {
        ((ImDrawList*)UserData)->AddRectFilledMultiColor(startpos, endpos, topleftcolor, toprightcolor, bottomrightcolor, bottomleftcolor);
    }

    void ImGuiRenderer::DrawCircle(ImVec2 center, float radius, uint32_t color, bool filled, bool thickness)
    {
        filled ? ((ImDrawList*)UserData)->AddCircleFilled(center, radius, color) :
            ((ImDrawList*)UserData)->AddCircle(center, radius, color, 0, thickness);
    }

    void ImGuiRenderer::DrawPolygon(ImVec2* points, int sz, uint32_t color, bool filled, float thickness)
    {
        filled ? ((ImDrawList*)UserData)->AddConvexPolyFilled(points, sz, color) :
            ((ImDrawList*)UserData)->AddPolyline(points, sz, color, ImDrawFlags_Closed, thickness);
    }

    void ImGuiRenderer::DrawPolyGradient(ImVec2* points, uint32_t* colors, int sz)
    {
        auto drawList = ((ImDrawList*)UserData);
        const ImVec2 uv = drawList->_Data->TexUvWhitePixel;

        if (drawList->Flags & ImDrawListFlags_AntiAliasedFill)
        {
            // Anti-aliased Fill
            const float AA_SIZE = 1.0f;
            const int idx_count = (sz - 2) * 3 + sz * 6;
            const int vtx_count = (sz * 2);
            drawList->PrimReserve(idx_count, vtx_count);

            // Add indexes for fill
            unsigned int vtx_inner_idx = drawList->_VtxCurrentIdx;
            unsigned int vtx_outer_idx = drawList->_VtxCurrentIdx + 1;
            for (int i = 2; i < sz; i++)
            {
                drawList->_IdxWritePtr[0] = (ImDrawIdx)(vtx_inner_idx);
                drawList->_IdxWritePtr[1] = (ImDrawIdx)(vtx_inner_idx + ((i - 1) << 1));
                drawList->_IdxWritePtr[2] = (ImDrawIdx)(vtx_inner_idx + (i << 1));
                drawList->_IdxWritePtr += 3;
            }

            // Compute normals
            ImVec2* temp_normals = (ImVec2*)alloca(sz * sizeof(ImVec2));
            for (int i0 = sz - 1, i1 = 0; i1 < sz; i0 = i1++)
            {
                const ImVec2& p0 = points[i0];
                const ImVec2& p1 = points[i1];
                ImVec2 diff = p1 - p0;
                diff *= ImInvLength(diff, 1.0f);
                temp_normals[i0].x = diff.y;
                temp_normals[i0].y = -diff.x;
            }

            for (int i0 = sz - 1, i1 = 0; i1 < sz; i0 = i1++)
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
                drawList->_VtxWritePtr[0].uv = uv; drawList->_VtxWritePtr[0].col = colors[i1];        // Inner
                drawList->_VtxWritePtr[1].pos = (points[i1] + dm);
                drawList->_VtxWritePtr[1].uv = uv; drawList->_VtxWritePtr[1].col = colors[i1] & ~IM_COL32_A_MASK;  // Outer
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
            const int idx_count = (sz - 2) * 3;
            const int vtx_count = sz;
            drawList->PrimReserve(idx_count, vtx_count);
            for (int i = 0; i < vtx_count; i++)
            {
                drawList->_VtxWritePtr[0].pos = points[i];
                drawList->_VtxWritePtr[0].uv = uv;
                drawList->_VtxWritePtr[0].col = colors[i];
                drawList->_VtxWritePtr++;
            }
            for (int i = 2; i < sz; i++)
            {
                drawList->_IdxWritePtr[0] = (ImDrawIdx)(drawList->_VtxCurrentIdx);
                drawList->_IdxWritePtr[1] = (ImDrawIdx)(drawList->_VtxCurrentIdx + i - 1);
                drawList->_IdxWritePtr[2] = (ImDrawIdx)(drawList->_VtxCurrentIdx + i);
                drawList->_IdxWritePtr += 3;
            }

            drawList->_VtxCurrentIdx += (ImDrawIdx)vtx_count;
        }
    }

    void ImGuiRenderer::DrawRadialGradient(ImVec2 center, float radius, uint32_t in, uint32_t out, int start, int end)
    {
        auto drawList = ((ImDrawList*)UserData);
        if (((in | out) & IM_COL32_A_MASK) == 0 || radius < 0.5f)
            return;
        auto startrad = ((float)M_PI / 180.f) * (float)start;
        auto endrad = ((float)M_PI / 180.f) * (float)end;

        // Use arc with 32 segment count
        drawList->PathArcTo(center, radius, startrad, endrad, 32);
        const int count = drawList->_Path.Size - 1;

        unsigned int vtx_base = drawList->_VtxCurrentIdx;
        drawList->PrimReserve(count * 3, count + 1);

        // Submit vertices
        const ImVec2 uv = drawList->_Data->TexUvWhitePixel;
        drawList->PrimWriteVtx(center, uv, in);
        for (int n = 0; n < count; n++)
            drawList->PrimWriteVtx(drawList->_Path[n], uv, out);

        // Submit a fan of triangles
        for (int n = 0; n < count; n++)
        {
            drawList->PrimWriteIdx((ImDrawIdx)(vtx_base));
            drawList->PrimWriteIdx((ImDrawIdx)(vtx_base + 1 + n));
            drawList->PrimWriteIdx((ImDrawIdx)(vtx_base + 1 + ((n + 1) % count)));
        }

        drawList->_Path.Size = 0;
    }

    bool ImGuiRenderer::SetCurrentFont(std::string_view family, float sz, FontType type)
    {
        auto font = GetFont(family, sz, type);

        if (font != nullptr) 
        {
            _currentFontSz = sz;
            ImGui::PushFont((ImFont*)font); 
            return true;
        }

        return false;
    }

    bool ImGuiRenderer::SetCurrentFont(void* fontptr, float sz)
    {
        if (fontptr != nullptr) 
        {
            _currentFontSz = sz;
            ImGui::PushFont((ImFont*)fontptr); 
            return true;
        }

        return false;
    }

    void ImGuiRenderer::ResetFont()
    {
        ImGui::PopFont();
    }

    ImVec2 ImGuiRenderer::GetTextSize(std::string_view text, void* fontptr, float sz)
    {
        auto imfont = (ImFont*)fontptr;
        ImGui::PushFont(imfont);
        auto txtsz = ImGui::CalcTextSize(text.data(), text.data() + text.size());
        ImGui::PopFont();

        auto ratio = (sz / imfont->FontSize);
        txtsz.x *= ratio;
        txtsz.y *= ratio;
        return txtsz;
    }

    void ImGuiRenderer::DrawText(std::string_view text, ImVec2 pos, uint32_t color)
    {
        auto font = ImGui::GetFont();
        ((ImDrawList*)UserData)->AddText(font, _currentFontSz, pos, color, text.data(), text.data() + text.size());
    }

    void ImGuiRenderer::DrawText(std::string_view text, std::string_view family, ImVec2 pos, float sz, uint32_t color, FontType type)
    {
        auto popFont = false;
        auto font = GetFont(family, sz, type);

        if (font != nullptr) 
        {
            ImGui::PushFont((ImFont*)font); 
            popFont = true;
        }

        ((ImDrawList*)UserData)->AddText((ImFont*)font, sz, pos, color, text.data(), text.data() + text.size());
        if (popFont) ImGui::PopFont();
    }

    void ImGuiRenderer::DrawTooltip(ImVec2 pos, std::string_view text)
    {
        if (!text.empty())
        {
            SetCurrentFont(config.DefaultFontFamily, config.DefaultFontSize, FT_Normal);
            ImGui::SetTooltip("%.*s", (int)text.size(), text.data());
            ResetFont();
        }
    }

    float ImGuiRenderer::EllipsisWidth(void* fontptr, float sz)
    {
        return ((ImFont*)fontptr)->EllipsisWidth;
    }

    ImVec2 ImGuiPlatform::GetCurrentMousePos()
    {
        return ImGui::GetIO().MousePos;
    }

    bool ImGuiPlatform::IsMouseClicked()
    {
        return ImGui::GetIO().MouseReleased[0];
    }

    void ImGuiPlatform::HandleHyperlink(std::string_view link)
    {
        if (HyperlinkClicked != nullptr)
            HyperlinkClicked(link);
    }

    void ImGuiPlatform::HandleHover(bool hovered)
    {
        hovered ? ImGui::SetMouseCursor(ImGuiMouseCursor_Hand) :
            ImGui::SetMouseCursor(ImGuiMouseCursor_Arrow);
    }

#endif
#ifdef IM_RICHTEXT_TARGET_BLEND2D

    Blend2DRenderer::Blend2DRenderer(BLContext& ctx) : context{ ctx } {}

    void Blend2DRenderer::SetClipRect(ImVec2 startpos, ImVec2 endpos)
    {
        context.clipToRect(BLRect{ startpos.x, startpos.y, endpos.x - startpos.x, endpos.y - startpos.y });
    }

    void Blend2DRenderer::ResetClipRect()
    {
        context.restoreClipping();
    }

    void Blend2DRenderer::DrawLine(ImVec2 startpos, ImVec2 endpos, uint32_t color, float thickness)
    {
        BLLine line;
        line.x0 = startpos.x;
        line.x1 = endpos.x;
        line.y0 = startpos.y;
        line.y1 = endpos.y;

        BLRgba32 rgba{ color };
        context.setStrokeWidth(thickness);
        context.setStrokeStyle(rgba);
        context.strokeLine(line);
    }

    void Blend2DRenderer::DrawPolyline(ImVec2* points, int sz, uint32_t color, float thickness)
    {
        thread_local static BLPoint blpoints[64];
        assert(sz < 64);
        std::memset(points, 0, sizeof(BLPoint) * 64);
        for (auto idx = 0; idx < sz; ++idx) {
            blpoints[idx].x = points[idx].x;
            blpoints[idx].y = points[idx].y;
        }

        BLRgba32 rgba{ color };
        context.setStrokeWidth(thickness);
        context.setStrokeStyle(rgba);
        context.strokePolyline(blpoints, sz);
    }

    void Blend2DRenderer::DrawTriangle(ImVec2 pos1, ImVec2 pos2, ImVec2 pos3, uint32_t color, bool filled, bool thickness)
    {
        BLRgba32 rgba{ color };
        context.setStrokeWidth(thickness);
        context.setStrokeStyle(rgba);
        if (filled) context.setFillStyle(rgba);
        else context.setFillStyle(IM_COL32_BLACK_TRANS);
        context.strokeTriangle(pos1.x, pos1.y, pos2.x, pos2.y, pos3.x, pos3.y);
    }

    void Blend2DRenderer::DrawRect(ImVec2 startpos, ImVec2 endpos, uint32_t color, bool filled, float thickness)
    {
        BLRgba32 rgba{ color };
        context.setStrokeWidth(thickness);
        context.setStrokeStyle(rgba);
        if (filled) context.setFillStyle(rgba);
        else context.setFillStyle(IM_COL32_BLACK_TRANS);
        context.strokeRect(startpos.x, startpos.y, endpos.x - startpos.x, endpos.y - startpos.y);
    }

    void Blend2DRenderer::DrawRectGradient(ImVec2 startpos, ImVec2 endpos, uint32_t topleftcolor, uint32_t toprightcolor, uint32_t bottomrightcolor, uint32_t bottomleftcolor)
    {
        BLGradient gradient;
        gradient.setType(BLGradientType::BL_GRADIENT_TYPE_LINEAR);
        gradient.setX0(startpos.x);
        gradient.setX1(endpos.x);
        gradient.setY0(startpos.y);
        gradient.setY1(endpos.y);
        gradient.addStop(0.f, BLRgba32{ topleftcolor });
        gradient.addStop(1.f, BLRgba32{ bottomrightcolor });
        context.setFillStyle(gradient);
        context.strokeRect(startpos.x, startpos.y, endpos.x - startpos.x, endpos.y - startpos.y);
    }

    void Blend2DRenderer::DrawRoundedRect(ImVec2 startpos, ImVec2 endpos, uint32_t color, bool filled, float topleftr, float toprightr, float bottomrightr, float bottomleftr, float thickness)
    {
        auto isUniformRadius = toprightr == toprightr && toprightr == bottomrightr && bottomrightr == bottomleftr;
        BLRgba32 rgba{ color };
        context.setStrokeWidth(thickness);
        context.setStrokeStyle(rgba);
        if (filled) context.setFillStyle(rgba);
        else context.setFillStyle(IM_COL32_BLACK_TRANS);

        if (isUniformRadius)
        {
            context.strokeRoundedRect(startpos.x, startpos.y, endpos.x - startpos.x, endpos.y - startpos.y, topleftr, bottomrightr);
        }
        else
        {
            BLPath path;
            path.moveTo(startpos.x, endpos.y - bottomleftr);
            path.lineTo(startpos.x, startpos.y + topleftr);
            if (topleftr > 0.f) path.arcTo(startpos.x + topleftr, startpos.y + topleftr, topleftr, topleftr, M_PI, 1.5 * M_PI);
            path.lineTo(endpos.x - toprightr, startpos.y);
            if (toprightr > 0.f) path.arcTo(endpos.x - toprightr, startpos.y + toprightr, toprightr, toprightr, 1.5 * M_PI, 2.0 * M_PI);
            path.lineTo(endpos.x, endpos.y - bottomrightr);
            if (bottomrightr > 0.f) path.arcTo(endpos.x - bottomrightr, endpos.y - bottomrightr, bottomrightr, bottomrightr, 0.0, 0.5 * M_PI);
            path.lineTo(startpos.x - bottomleftr, endpos.y);
            if (bottomleftr > 0.f) path.arcTo(startpos.x + bottomleftr, endpos.y - bottomleftr, bottomleftr, bottomleftr, 0.5 * M_PI, M_PI);

            context.strokePath(path);
        }
    }

    void Blend2DRenderer::DrawPolygon(ImVec2* points, int sz, uint32_t color, bool filled, float thickness)
    {
        thread_local static BLPoint blpoints[64];
        assert(sz < 64);
        std::memset(points, 0, sizeof(BLPoint) * 64);
        for (auto idx = 0; idx < sz; ++idx) {
            blpoints[idx].x = points[idx].x;
            blpoints[idx].y = points[idx].y;
        }

        BLRgba32 rgba{ color };
        context.setStrokeWidth(thickness);
        context.setStrokeStyle(rgba);
        if (filled) context.setFillStyle(rgba);
        else context.setFillStyle(IM_COL32_BLACK_TRANS);
        context.strokePolygon(blpoints, sz);
    }

    void Blend2DRenderer::DrawPolyGradient(ImVec2* points, uint32_t* colors, int sz)
    {
        // TODO: figure this out...
    }

    void Blend2DRenderer::DrawCircle(ImVec2 center, float radius, uint32_t color, bool filled, bool thickness)
    {
        BLRgba32 rgba{ color };
        context.setStrokeWidth(thickness);
        context.setStrokeStyle(rgba);
        if (filled) context.setFillStyle(rgba);
        else context.setFillStyle(IM_COL32_BLACK_TRANS);
        context.strokeCircle(center.x, center.y, radius);
    }

    void Blend2DRenderer::DrawRadialGradient(ImVec2 center, float radius, uint32_t in, uint32_t out, int start, int end)
    {
        BLGradient gradient;
        gradient.setType(BLGradientType::BL_GRADIENT_TYPE_RADIAL);
        gradient.setX0(startpos.x);
        gradient.setX1(startpos.x);
        gradient.setY0(startpos.y);
        gradient.setY1(startpos.y);
        gradient.setR0(radius);
        gradient.setR1(radius);
        gradient.addStop(0.f, BLRgba32{ in });
        gradient.addStop(1.f, BLRgba32{ out });
        context.setFillStyle(gradient);
        context.strokeCircle(startpos.x, startpos.y, radius);
    }

    bool Blend2DRenderer::SetCurrentFont(std::string_view family, float sz, FontType type) override
    {
        currentFont = GetFont(family, sz, type);
        return currentFont != nullptr;
    }

    bool Blend2DRenderer::SetCurrentFont(void* fontptr, float sz) override
    {
        currentFont = (BLFont*)fontptr;
        return fontptr != nullptr;
    }

    void Blend2DRenderer::ResetFont()
    {
        currentFont = nullptr;
    }

    ImVec2 Blend2DRenderer::GetTextSize(std::string_view text, void* fontptr, float sz)
    {
        auto& font = *((BLFont*)fontptr);
        BLGlyphBuffer buf;
        BLTextMetrics metrics;
        buf.setUtf8Text(text.data(), text.size());
        font.shape(buf);
        font.getTextMetrics(buf, metrics);

        auto& bb = metrics.boundingBox;
        return ImVec2{ bb.x1 - bb.x0, bb.y1 - bb.y0 };
    }

    void Blend2DRenderer::DrawText(std::string_view text, ImVec2 pos, uint32_t color)
    {
        assert(currentFont != nullptr);
        BLRgba32 rgba{ color };
        context.setFillStyle(rgba);
        context.fillUtf8Text(BLPoint{ pos.x, pos.y }, *currentFont, BLStringView{ text.data(), text.size() });
    }

    void Blend2DRenderer::DrawText(std::string_view text, std::string_view family, ImVec2 pos, float sz, uint32_t color, FontType type)
    {
        auto font = GetFont(family, sz, type);
        assert(font != nullptr);
        BLRgba32 rgba{ color };
        context.setFillStyle(rgba);
        context.fillUtf8Text(BLPoint{ pos.x, pos.y }, *font, BLStringView{ text.data(), text.size() });
    }

    void Blend2DRenderer::DrawTooltip(ImVec2 pos, std::string_view text)
    {
    }

#endif
}
