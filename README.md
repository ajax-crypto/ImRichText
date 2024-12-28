# ImRichText

## 🚧 Work in Progress!

*NOTE* : *This is not a general purpose HTML renderer, only the specified tags/properties below are targeted*
---

Implementation of Rich Text Rendering for [ImGui](https://github.com/ocornut/imgui) (**ASCII text only**) akin to Qt support for it. Use it as follows:
```c++
// Do not cache anything
std::string rtf = "<s><q>Quotation </q><cite>Citation</cite></s><ul style='font-size: 36px;'><li>item</li><li>item</li></ul>";

// This creates cacheable drawables after parsing
auto id = ImRichText::CreateRichText("2<sup>2</sup> equals 4  <hr style=\"height: 4px; color: sienna;\"/>"
    "<p style=\"color: rgb(150, 0, 0);\">Paragraph <b>bold <i>italics</i> bold2 </b></p>"
    "<h1 style=\"color: darkblue;\">Heading&Tab;</h1>"
    "<span style='background: teal; color: white;'>White on Teal</span><br/>"
    "<mark>This is highlighted! <small>This is small...</small></mark>");

auto config = ImRichText::GetDefaultConfig({ -1.f, -1.f }, 24.f, 1.5f);
config->Scale = 2.f;
ImRichText::PushConfig(*config);

while (<event-loop>)
{
    if (ImGui::Begin(...))
    {
        // ... other widgets
        ImRichText::GetCurrentConfig()->DefaultBgColor = ImColor{ 255, 255, 255 };
        ImRichText::Show(rtf.data(), rtf.data() + rtf.size());

        ImRichText::GetCurrentConfig()->DefaultBgColor = ImColor{ 200, 200, 200 };
        ImRichText::Show(id);
        // ... other widgets
    }
}
```
![Basic screenshot](https://raw.githubusercontent.com/ajax-crypto/ImRichText/refs/heads/main/screenshots/basic.png)

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
| blockquote | Blockquote as in HTML | _Under progress_ |
| pre | Preformatted text with monospaced font | _Under progress_ |
| code | Use monospace font for this block of text | _Under progress_ |
| s/del | Draw a horizontal line in current block of text | Yes |
| u | Underline current block of text | Yes[^3] |
| a | Make current block of text a hyperlink (handle click events) | **Not Implemented** |
| blink | Make current block of text blink | **Not Implemented** |
| marquee | Make current block of text scroll horizontally | **Not Implemented** |

### General Style Properties
| Property Name(s) | Value/Example |
|------------------|:---------------|
| background/background-color/color | `rgb(r, g, b)`/`rgba(r, g, b, a)`/`hsl(h, s, l)`/ [CSS color name](https://developer.mozilla.org/en-US/docs/Web/CSS/named-color) |
| padding/padding-top/etc. | `px`/`em` units |
| font-size | `pt`/`px`/`em` (_absolute_) / % (_of parent font size_) / `xx-small`, `x-small`, `medium`, `large`, etc. |
| font-family | _name of font family_ |
| font-weight | _value between 0-800_ or `light`/`normal`/`bold` |
| font-style | italics/oblique |
| height/width | `px`/`em` |
| list-style-type | (_Only for list items_) `circle`/`disk`/`square`/`custom`[^2] |

In order to handle rich text as specified above, fonts need to be managed i.e. different family, weights, sizes, etc. 
The library internally uses default fonts (for Windows Segoe UI family for proportional and Consolas for monospace).
However, user can provide their own font provider through `RenderConfig::GetFont` function pointer.

## Immediate Goals
* Word wrapping support
* Add support for `a` tag (hyperlinks with click handling)
* Add support for `blink` and `marquee` (Requires saving current animation state)
* Add support for `margin` and possibly `border` (_although the utility of border is debatable_)
* Implement support for vertical/horizontal text alignment including baseline alignment (May need to use FreeType backend)
* Integration example with [Clay layout library](https://github.com/nicbarker/clay?tab=readme-ov-file)
* Roman numerals for numeberd lists
* Gradient fills for backgrounds

## Future Goals
* Use a library (roll your own?) to lookup font(s) based on requirements i.e. fuzzy match on family, etc.
* Internationalization support by integrating [Harfbuzz](https://github.com/harfbuzz/harfbuzz) (Unicode Bidir algo)
* Add ways to remove C++ standard library dependencies
* Text effects like "glow", "shadow", etc.

## Non-Goals
* Build scripts like cmake, build2, make, etc. This library is intended to be used by simply copying the .h/.cpp files.
* Full-fledged support for CSS3 styling with layout
* Although Qt's rich text in labels supports tables, tables are not supported by design, use ImGui tables instead.
* Support alternate syntax i.e. Markdown, Restructured Text, MathML, etc.

## Build Dependencies
The library depends on ImGui and C++17 standard library. It can be compiled using any C++17 compiler.

## Build Macros 
In order to customize certain behavior at build-time, the following macros can be used
| Macro name | Functionality | Default Value |
|------------|:--------------|:--------------|
| `IM_RICHTEXT_MAXDEPTH` | Maximum depth of nested blocks/tags in Rich Text | 32 |
| `IM_RICHTEXT_MAX_LISTDEPTH` | Maximum depth of nested lists | 16 |
| `IM_RICHTEXT_MAX_LISTITEM` | Maxmimum number of list items at a specific depth | 128 |
| `IM_RICHTEXT_MAXTABSTOP` | Maxmimum number of nested `<p>`/paragraphs | 32 |
| `IM_RICHTEXT_ENABLE_PARSER_LOGS` | Enable printing parsing + layout logs in console in debug builds | Not defined |

## Error Reporting
When `_DEBUG` macro is defined, if a console is present, error messages will be printed along
with the parsing state i.e. entering/exiting tags. Custom properties or unknonw tags are ignored, but reported.

## Contributions
Since it is work in progress, no contributions are accepted at the moment. Once I stabilize and create a release, contributions
will be accepted! In the meantime, feel free to browse the source...

## About the Implementation
The current implementation intentionally forgoes the creation of any form of AST (Abstract Syntax Tree) or
a well-defined phase of tokenization for lexical analysis. In order to keep the codebase simple and not 
ending up creating a HTML/CSS engine, the scope the arbitrarily cutdown. 
The algorithm simply breaks down the rich text specified into lines, and each line into segments. Each segment 
is defined as multiple blocks of text containing the same style i.e. background/foreground/font properties, etc.
A "block of text" is simply a run of glyphs without any "space"/"blank" characters in between. Once rich text is 
broken down to lines, it is rendered in two phases i.e. first the background is drawn i.e. blockquote background 
can span multiple lines. After that, the foreground i.e. text is drawn (with background/foreground colors).

[^1]: Nested subscript/superscript is untested at the moment
[^2]: Custom bullets are also possible, set `RenderConfig::DrawBullet` function pointer and `list-style-type` property to `custom`
[^3]: Underline text due to `<u>` tag is not baseline-underlined, but underlined beneath the whole text
