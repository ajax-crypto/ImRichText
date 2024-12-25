# ImRichText

## Work in Progress!

*NOTE* : *This is not a general purpose HTML renderer, only the specified tags/properties below are targeted*
---

Implementation of Rich Text Rendering in ImGui for **ASCII text** akin to Qt support for it. Use it as follows:
```c++
std::string rtf = "2<sup>2</sup> equals 4  <hr style=\"height: 4px; color: sienna;\"/>"
            "<blockquote><p style=\"color: rgb(150, 0, 0);\">Paragraph <b>bold <i>italics</i> bold2 </b></p></blockquote>"
            "<h1 style=\"border: none;\">Heading&Tab;</h1>"
            "<ol><li> item#1 </li><li> item#2 </li></ol>"
            "<span style='background: teal; color: white;'>Colored</span>";
auto config = ImRichText::GetDefaultConfig({ 400.f, 500.f });
config->DefaultFontSize = 24.f;
config->Scale = 2.f;
ImRichText::PushConfig(*config);
ImRichText::Draw(rtf.data(), 0, rtf.size());
```

## How to use it?
Just include the .h and .cpp files in your project.

## What is supported?
The following subset of HTML tags/CSS properties are supported:

| Tags | Description | Implementation Status |
|------|:------------------|:----------------|
| span  | A region of text with certain style properties | Yes |
| p | Start a paragraph in new line (paragraph indent can be specified in `RenderConfig::ParagraphStop`) | Yes |
| font  | Specify size, family, weight, style for a block of text | Yes |
| sup/sub | Superscript/Subscript | Yes |
| hr | Horizontal line | Yes |
| h1...h6 | Header (bold) text with a line underneath | Yes |
| ul | Un-numbered list (with bullets) | Yes |
| ol | Numbered list (with nested numberings i.e. 1.2.3) | Yes |
| li | List Item | Yes |
| br | Line Break | Yes |
| i/b | Italics/Bold block of text | Yes |
| blockquote | Blockquote as in HTML | Yes |
| pre | Preformatted text with monospaced font | Yes |
| code | Use monospace font for this block of text | _Under progress_ |

### General Style Properties
| Property Name(s) | Value/Example |
|------------------|:---------------|
| background/background-color/color | `rgb(r, g, b)`/`rgba(r, g, b, a)`/ [CSS color name](https://developer.mozilla.org/en-US/docs/Web/CSS/named-color) |
| font-size | `pt`/`px`/`em` |
| font-family | _name of font family_ |
| font-weight | _value between 0-800_ or `light`/`normal`/`bold` |
| font-style | italics/oblique |
| height/width | `px`/`em` |
| list-style-type | (_Only for list items_) Specify type for list item bullets |

In order to handle rich text as specified above, fonts need to be managed i.e. different family, weights, sizes, etc. 
The library internally uses default fonts (for Windows Segoe UI family for proportional and Consolas for monospace).
However, user can provide their own font provider through `RenderConfig::GetFont` function pointer.

## Future Goals
* Add cmake support (**Contributions welcome!**) (_I dislike cmake personally_)
* Add support for `underline` and `strikethrough`
* Add support for HSL/ARGB/etc. color specifiers (_Under progress_)
* Implement support for vertical/horizontal text alignment support (_Under progress_)
* Internationalization support by integrating [Harfbuzz](https://github.com/harfbuzz/harfbuzz) (Unicode Bidir algo)
* Support alternate syntax i.e. Markdown, Restructured Text, MathML, etc.

## Macros 
In order to customize certain behavior at build-time, the following macros can be used
| Macro name | Functionality | Default Value |
|------------|:--------------|:--------------|
| `IM_RICHTEXT_MIN_RTF_CACHESZ` | Minimum RTF string size to cache the drawables from the RTF specified and reuse | 128 |
| `IM_RICHTEXT_MAXDEPTH` | Maximum depth of nested blocks/tags in Rich Text | 256 |
| `IM_RICHTEXT_MAX_LISTDEPTH` | Maximum depth of nested lists | 256 |
| `IM_RICHTEXT_MAX_LISTITEM` | Maxmimum number of list items at a specific depth | 128 |
| `IM_RICHTEXT_MAXTABSTOP` | Maxmimum number of nested `<p>`/paragraphs | 32 |
