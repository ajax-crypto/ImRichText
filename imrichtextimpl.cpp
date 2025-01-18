#include "imrichtextimpl.h"
#include "imrichtext.h"

namespace ImRichText
{
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

    void ImGuiRenderer::DrawRect(ImVec2 startpos, ImVec2 endpos, uint32_t color, bool filled, float thickness, float radius, int corners)
    {
        auto drawflags = 0;

        if (corners & TopLeftCorner) drawflags |= ImDrawFlags_RoundCornersTopLeft;
        if (corners & TopRightCorner) drawflags |= ImDrawFlags_RoundCornersTopRight;
        if (corners & BottomRightCorner) drawflags |= ImDrawFlags_RoundCornersBottomRight;
        if (corners & BottomLeftCorner) drawflags |= ImDrawFlags_RoundCornersBottomLeft;

        filled ? ((ImDrawList*)UserData)->AddRectFilled(startpos, endpos, color, radius, drawflags) :
            ((ImDrawList*)UserData)->AddRect(startpos, endpos, color, radius, drawflags, thickness);
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

    void ImGuiRenderer::DrawRadialGradient(ImVec2 center, float radius, uint32_t in, uint32_t out)
    {
        auto drawList = ((ImDrawList*)UserData);
        if (((in | out) & IM_COL32_A_MASK) == 0 || radius < 0.5f)
            return;

        // Use arc with automatic segment count
        drawList->_PathArcToFastEx(center, radius, 0, IM_DRAWLIST_ARCFAST_SAMPLE_MAX, 0);
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
        auto font = GetFont(family, sz, type, nullptr);

        if (font != nullptr) {
            ImGui::PushFont(font); return true;
        }

        return false;
    }

    bool ImGuiRenderer::SetCurrentFont(void* fontptr)
    {
        if (fontptr != nullptr) {
            ImGui::PushFont((ImFont*)fontptr); return true;
        }

        return false;
    }

    void ImGuiRenderer::ResetFont()
    {
        ImGui::PopFont();
    }

    ImVec2 ImGuiRenderer::GetTextSize(std::string_view text, void* fontptr)
    {
        ImGui::PushFont((ImFont*)fontptr);
        auto sz = ImGui::CalcTextSize(text.data(), text.data() + text.size());
        ImGui::PopFont();
        return sz;
    }

    void ImGuiRenderer::DrawText(std::string_view text, ImVec2 pos, uint32_t color)
    {
        ((ImDrawList*)UserData)->AddText(pos, color, text.data(), text.data() + text.size());
    }

    void ImGuiRenderer::DrawText(std::string_view text, std::string_view family, ImVec2 pos, float sz, uint32_t color, FontType type)
    {
        auto popFont = false;
        auto font = GetFont(family, sz, type, nullptr);

        if (font != nullptr) {
            ImGui::PushFont(font); popFont = true;
        }

        ((ImDrawList*)UserData)->AddText(pos, color, text.data(), text.data() + text.size());
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

    ImVec2 ImGuiGLFWPlatform::GetCurrentMousePos()
    {
        return ImGui::GetIO().MousePos;
    }

    bool ImGuiGLFWPlatform::IsMouseClicked()
    {
        return ImGui::GetIO().MouseReleased[0];
    }

    void ImGuiGLFWPlatform::HandleHyperlink(std::string_view link)
    {
        if (HyperlinkClicked != nullptr)
            HyperlinkClicked(link);
    }

    void ImGuiGLFWPlatform::HandleHover(bool hovered)
    {
        hovered ? ImGui::SetMouseCursor(ImGuiMouseCursor_Hand) :
            ImGui::SetMouseCursor(ImGuiMouseCursor_Arrow);
    }

#elif defined(IM_RICHTEXT_TARGET_BLEND2D)

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

    void Blend2DRenderer::DrawRect(ImVec2 startpos, ImVec2 endpos, uint32_t color, bool filled, float thickness, float radius, int corners)
    {
        BLRgba32 rgba{ color };
        context.setStrokeWidth(thickness);
        context.setStrokeStyle(rgba);
        if (filled) context.setFillStyle(rgba);
        else context.setFillStyle(IM_COL32_BLACK_TRANS);
        context.strokeRoundRect(startpos.x, startpos.y, endpos.x - startpos.x, endpos.y - startpos.y, radius, radius);
    }

    void Blend2DRenderer::DrawRectGradient(ImVec2 startpos, ImVec2 endpos, uint32_t topleftcolor, uint32_t toprightcolor, uint32_t bottomrightcolor, uint32_t bottomleftcolor)
    {
        BLGradient gradient;
        gradient.setType(BLGradientType::BL_GRADIENT_TYPE_LINEAR);
        //gradient.setAngle();
        gradient.addStop(0.f, BLRgba32{ topleftcolor });
        gradient.addStop(1.f, BLRgba32{ bottomrightcolor });
        context.setFillStyle(gradient);
        context.strokeRect(startpos.x, startpos.y, endpos.x - startpos.x, endpos.y - startpos.y);
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

    bool Blend2DRenderer::SetCurrentFont(std::string_view text, std::string_view family, float sz, FontType type) override
    {
        currentFont = GetFont(family, sz, type, nullptr);
        return currentFont != nullptr;
    }

    bool Blend2DRenderer::SetCurrentFont(void* fontptr) override
    {
        currentFont = (BLFont*)fontptr;
        return fontptr != nullptr;
    }

    ImVec2 Blend2DRenderer::GetTextSize(std::string_view text, void* fontptr)
    {
        // TODO: Implement this
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
        auto font = GetFont(family, sz, type, nullptr);
        BLRgba32 rgba{ color };
        context.setFillStyle(rgba);
        context.fillUtf8Text(BLPoint{ pos.x, pos.y }, *font, BLStringView{ text.data(), text.size() });
    }

#endif
}
