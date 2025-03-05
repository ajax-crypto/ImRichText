# ImRichText

## 🚧 Work in Progress!

**⚠ This is not a general purpose HTML/CSS renderer, only the specified tags/properties below are targeted**

Implementation of Rich Text Rendering with flexible renderer/platform integrations. Use it as follows:
```c++
auto id1 = ImRichText::CreateRichText("<blink>This is blinking</blink>"
    "<marquee>This is moving...</marquee>"
    "<meter value='3' max='10'></meter>"
    "<s><q>Quotation </q><cite>Citation</cite></s>"
    "<br>Powered by: <a href='https://https://github.com/ajax-crypto/ImRichText'>ImRichText</a>"
    "<ul style='font-size: 36px;'><li>item</li><li>item</li></ul>");

auto id2 = ImRichText::CreateRichText("2<sup>2</sup> equals 4  <hr style=\"height: 4px; color: sienna;\"/>"
    "<p style=\"color: rgb(150, 0, 0);\">Paragraph <b>bold <i>italics</i> bold2 </b></p>"
    "<h1 style=\"color: darkblue;\">Heading&Tab;</h1>"
    "<span style='background: teal; color: white;'>White on Teal</span><br/>"
    "<mark>This is highlighted! <small>This is small...</small></mark>");

ImRichText::DefaultConfigParams params;
params.DefaultFontSize = 24.f;
params.FontLoadFlags = ImRichText::FLT_HasSmall | ImRichText::FLT_Proportional | ImRichText::FLT_HasH1 | 
    ImRichText::FLT_HasSuperscript;
params.FontScale = 1.5f;
auto config = ImRichText::GetDefaultConfig(params);

#ifdef IM_RICHTEXT_TARGET_IMGUI

ImRichText::ImGuiRenderer renderer{ *config };
ImRichText::ImGuiGLFWPlatform platform;

config->Renderer = &renderer;
config->Platform = &platform;

#elif defined(IM_RICHTEXT_TARGET_BLEND2D)

Blend2DRenderer renderer{ context };
config->Renderer = &renderer;

#endif

config->ListItemBullet = ImRichText::BulletType::Arrow;
ImRichText::PushConfig(*config);

while (<event-loop>)
{
    if (ImGui::Begin(...))
    {
        // ... other widgets
        ImRichText::GetCurrentConfig()->DefaultBgColor = ImColor{ 255, 255, 255 };
        ImRichText::Show(id1);

        ImRichText::GetCurrentConfig()->DefaultBgColor = ImColor{ 200, 200, 200 };
        ImRichText::Show(id2);
        // ... other widgets
    }
}
```
![Basic screenshot](https://raw.githubusercontent.com/ajax-crypto/ImRichText/refs/heads/main/screenshots/imrichtext.gif)

## How to use it?
Just include the .h and .cpp files in your project. (You will need a C++17 compiler)

## What is supported?
The following subset of HTML tags/CSS properties are supported:

| Tags | Description | Implementation Status |
|------|:------------------|:----------------|
| span  | A region of text with certain style properties | Yes |
| p | Start a paragraph in new line (paragraph indent can be specified in `RenderConfig::ParagraphStop`) | Yes |
| font  | Specify size, family, weight, style for a block of text | Yes |
| sup/sub | Superscript/Subscript | Yes[^1] |
| hr | Horizontal line | Yes |
| h1...h6 | Header (bold) text with a line underneath | Yes |
| ul | Un-numbered list (with bullets) | Yes |
| ol | Numbered list (with nested numberings i.e. 1.2.3) | Yes |
| li | List Item | Yes |
| br | Line Break | Yes |
| b/strong | Bold block of text | Yes |
| i/em/cite | Italics block of text | Yes |
| mark | Highlight current block of text | Yes |
| small | Reduce font size to 80% of current block | Yes |
| q | Wrap text inside quotation mark | Yes |
| u | Underline current block of text | Yes[^3] |
| a | Make current block of text a hyperlink (handle click events) | Yes |
| abbr | Mark current block as an abbreviation, `title` attribute contains tooltip | Yes |
| s/del | Draw a horizontal line through the text content | Yes |
| blink | Make current block of text blink | Yes |
| marquee | Make current block of text scroll horizontally | Yes |
| meter | Create a progress bar inline | Yes |
| font | Specify custom font with family/size/weight/etc. | Yes |
| center | Center align text | Yes |
| pre | Preformatted text with monospaced font | _Under progress_ |
| code | Use monospace font for this block of text | _Under progress_ |
| blockquote | Blockquote as in HTML | _Under progress_ |

### General Style Properties
| Property Name(s) | Value/Example |
|------------------|:---------------|
| background/background-color/color | `rgb(r, g, b)`/`rgba(r, g, b, a)`/`hsl(h, s, l)`/`linear-gradient(color-stops)`[^5] [CSS color name](https://developer.mozilla.org/en-US/docs/Web/CSS/named-color) |
| padding/padding-top/etc. | `px`/`em` units |
| font-size | `pt`/`px`/`em` (_absolute_) / % (_of parent font size_) / `xx-small`, `x-small`, `medium`, `large`, etc. |
| font-family | _name of font family_ |
| font-weight | _value between 0-800_ or `light`/`normal`/`bold` |
| font-style | italics/oblique |
| height/width | `px`/`em` |
| list-style-type | (_Only for list items_) `circle`/`disk`/`square`/`custom`[^2] |
| border/border-top/etc. | `2px solid gray`[^6] |
| border-radius | `px`/`em` |
| alignment | `left`/`right`/`center`/`justify` _(Horizontal text alignment)_ |
| vertical-align | `top`/`bottom`/`center` _(Vertical text alignment)_ |
| text-overflow | _Under progress_ |
| text-wrap | _Under progress_ |
| word-break | _Under progress_ |
| white-space-collapse | _Under progress_ |
| white-space | _Under progress_ |
| box-shadow | _Under progress_ |

In order to handle rich text as specified above, fonts need to be managed i.e. different family, weights, sizes, etc. 
The library internally uses default fonts (on Windows, Segoe UI family for proportional and Consolas for monospace).
However, user can provide their own font provider through `IRenderer` interface.

*NOTE* : The default ImGui renderer implementation doest not support dynamic font loading right now. All fonts must be
loaded by `ImRichText::LoadDefaultFonts` functions before rendering.

## Immediate Goals
* Support for class/id with stylesheets
* Remove pointers from API
* Add support for `margin`
* Add support for line style (solid, dotted, dashed) for `border`
* Implement baseline text alignment (May need to use FreeType backend)
* Integration example with [Clay layout library](https://github.com/nicbarker/clay?tab=readme-ov-file)
* Roman numerals for numbered lists
* Tables (`<table>`, `<tr>`, `<th>`, `<td>` tags)
* Thread safety

## Future Goals
* Use a library (roll your own?) to lookup font(s) based on glyph ranges.
* Internationalization support by integrating [Harfbuzz](https://github.com/harfbuzz/harfbuzz) (Unicode Bidir algorithm)
* Add ways to remove C++ standard library dependencies
* Conical gradient fills for backgrounds
* Clipping mask support (requires ImGui changes?)
* Text effects like "glow", "shadow", etc.
* Add a tooltip property as a replacement for title property
* Thread safety

## Non-Goals
* Build scripts like cmake, build2, make, etc. This library is intended to be used by simply copying the .h/.cpp files.
* Full-fledged support for CSS3 styling with layout
* Support alternate syntax i.e. Markdown, Restructured Text, MathML, etc.
* Integrating scripting languages

## Build Dependencies
The library depends on ImGui and C++17 standard library. It can be compiled using any C++17 compiler.
The following headers are used: `vector`, `deque`, `unordered_map`, `tuple`, `chrono`, `string_view`, `optional`.
If using the default font manager, additionally, `map`, `unordered_set`, `filesystem` is also used.

## Build Macros 
In order to customize certain behavior at build-time, the following macros can be used
| Macro name | Functionality | Default Value |
|------------|:--------------|:--------------|
| `IM_RICHTEXT_MAXDEPTH` | Maximum depth of nested blocks/tags in Rich Text | 32 |
| `IM_RICHTEXT_MAX_LISTDEPTH` | Maximum depth of nested lists | 16 |
| `IM_RICHTEXT_MAX_LISTITEM` | Maxmimum number of list items at a specific depth | 128 |
| `IM_RICHTEXT_MAXTABSTOP` | Maxmimum number of nested `<p>`/paragraphs | 32 |
| `IM_RICHTEXT_ENABLE_PARSER_LOGS` | Enable printing parsing + layout logs in console in debug builds | Not defined |
| `IM_RICHTEXT_BLINK_ANIMATION_INTERVAL` | Specify blink animation interval | 500ms |
| `IM_RICHTEXT_MARQUEE_ANIMATION_INTERVAL` | Specify interval (`1/FPS`) for marquee animation | 18ms |
| `IM_RICHTEXT_MAX_COLORSTOPS` | Specify maximum color stops in gradients | 8 |
| `IM_RICHTEXT_TARGET_IMGUI` | Specify if target is ImGui | undefined |
| `IM_RICHTEXT_TARGET_BLEND2D` | Specify if target is Blend2D | undefined |

## Error Reporting
When `_DEBUG` macro is defined, if a console is present, error messages will be printed along
with the parsing state i.e. entering/exiting tags. Custom properties or unknonw tags are ignored, but reported.

## Contributions
Contributions welcome, especially in getting Linux/MacOS examples added! 
Prefer build.sh build scripts, over cmake/ninja/etc. solutions.

The coding style is C++ with minimal OOP constructs i.e. no classes, inheritance, runtime polymorphism etc.
The only exception being the interfaces which enable runtime polymorphism for rendering and platform integration.
Features such as attributes, vocabulary types (`std::optional` & `std::tuple`) & non-capturing lambdas can be used. There are no 
exceptions or RTTI used in the library. Minimal uses of templates has "container of T" (No SFINAE shenanigans)

There is minimal use of raw pointers, mostly as a reference and no use of smart pointers. The library aims to minimize 
allocations on heap hence, most resources are pre-allocated or live on the stack. The only heap usage comes from limited 
use of standard library containers where applicable.

The current implementation is not thread-safe, and the library is not intended to be used in a multi-threaded environment.
In terms of design, the library does not construct an explicit AST, but rather uses a stack-based approach to 
parse and render the text.

## About the Implementation
The following interface is available to port it to any graphics API desired:

```c++
struct IRenderer
{
    void* UserData = nullptr;

    virtual void SetClipRect(ImVec2 startpos, ImVec2 endpos) = 0;
    virtual void ResetClipRect() = 0;

    virtual void DrawLine(ImVec2 startpos, ImVec2 endpos, uint32_t color, float thickness = 1.f) = 0;
    virtual void DrawPolyline(ImVec2* points, int sz, uint32_t color, float thickness) = 0;
    virtual void DrawTriangle(ImVec2 pos1, ImVec2 pos2, ImVec2 pos3, uint32_t color, bool filled, bool thickness = 1.f) = 0;
    virtual void DrawRect(ImVec2 startpos, ImVec2 endpos, uint32_t color, bool filled, float thickness = 1.f, float radius = 0.f, int corners = 0) = 0;
    virtual void DrawRectGradient(ImVec2 startpos, ImVec2 endpos, uint32_t topleftcolor, uint32_t toprightcolor, uint32_t bottomrightcolor, uint32_t bottomleftcolor) = 0;
    virtual void DrawPolygon(ImVec2* points, int sz, uint32_t color, bool filled, float thickness = 1.f) = 0;
    virtual void DrawPolyGradient(ImVec2* points, uint32_t* colors, int sz) = 0;
    virtual void DrawCircle(ImVec2 center, float radius, uint32_t color, bool filled, bool thickness = 1.f) = 0;
    virtual void DrawRadialGradient(ImVec2 center, float radius, uint32_t in, uint32_t out) = 0;
    virtual void DrawBullet(ImVec2 startpos, ImVec2 endpos, uint32_t color, int index, int depth) {};
    
    virtual bool SetCurrentFont(std::string_view family, float sz, FontType type) { return false; };
    virtual bool SetCurrentFont(void* fontptr) { return false; };
    virtual void ResetFont() {};
    virtual ImVec2 GetTextSize(std::string_view text, void* fontptr) = 0;
    virtual void DrawText(std::string_view text, ImVec2 pos, uint32_t color) = 0;
    virtual void DrawText(std::string_view text, std::string_view family, ImVec2 pos, float sz, uint32_t color, FontType type) = 0;
    virtual void DrawTooltip(ImVec2 pos, std::string_view text) = 0;
    virtual float EllipsisWidth(void* fontptr);

    void DrawDefaultBullet(BulletType type, ImVec2 initpos, const BoundedBox& bounds, uint32_t color, float bulletsz);
};
```

For platform integration (to handle clicks/hover events), the following interface is available:

```c++
struct IPlatform
{
    virtual ImVec2 GetCurrentMousePos() = 0;
    virtual bool IsMouseClicked() = 0;

    virtual void HandleHyperlink(std::string_view) = 0;
    virtual void RequestFrame() = 0;
    virtual void HandleHover(bool) = 0;
};
```

For text shaping integration, the following interface is available:

```c++
struct ITextShaper
{
    struct WordProperty
    {
        void* font;
        WordBreakBehavior wb;
    };

    using StyleAccessor = WordProperty (*)(int wordIdx, void* userdata);
    using LineRecorder = void (*)(int wordIdx, void* userdata);
    using WordRecorder = void (*)(int wordIdx, std::string_view, ImVec2 dim, void* userdata);

    virtual void ShapeText(float availwidth, const Span<std::string_view>& words,
        StyleAccessor accessor, LineRecorder lineRecorder, WordRecorder wordRecorder, 
        const RenderConfig& config, void* userdata) = 0;
    virtual void SegmentText(std::string_view content, WhitespaceCollapseBehavior wsbhv, 
        LineRecorder lineRecorder, WordRecorder wordRecorder, const RenderConfig& config, 
        bool ignoreLineBreaks, bool ignoreEscapeCodes, void* userdata) = 0;

    virtual int NextGraphemeCluster(const char* from, const char* end) const = 0;
    virtual int NextWordBreak(const char* from, const char* end) const = 0;
    virtual int NextLineBreak(const char* from, const char* end) const = 0;
};
```

The library comes with multiple text shapers i.e. ASCII, ASCII Extended, Simple UTF-8 (without diacritics, kerning & ligature). 
A text shaper based on Unicode BiDir algorithm is possible with SheenBiDi + HarfBuzz.

*NOTE* : Platform integration is optional, and can be omitted. If omitted, `<blink>`, `<marquee>` will not animate, and,
`a` hyperlinks and tooltips will not be functional.

Default implementations are provided for [ImGui](https://github.com/ocornut/imgui) and [Blend2D](https://github.com/blend2d/blend2d) (_Under progress_)
Platform integration implementation provided for ImGui + GLFW (available in examples directory)

[^1]: Nested subscript/superscript is untested at the moment
[^2]: Custom bullets are also possible, set `RenderConfig::DrawBullet` function pointer and `list-style-type` property to `custom`
[^3]: Underline text due to `<u>` tag is not baseline-underlined, but underlined beneath the whole text
[^5]: Only axis aligned gradients are support as `background` property
[^6]: Border line type is parsed but not used for rendering
