# Bundled fonts

Unmodified static TrueType fonts, distributed under the SIL Open Font License
1.1. Each license is included here and in the installable Vita package.

| Family | Files | Upstream revision |
| --- | --- | --- |
| Source Sans 3 | Regular, Semibold | [adobe-fonts/source-sans](https://github.com/adobe-fonts/source-sans/tree/87b37a2daaed80fcb8e8ccb0085c4d72ddade12e/TTF) |
| Literata | Regular | [googlefonts/literata](https://github.com/googlefonts/literata/tree/0c2761b727a1b3a7cffd313c37f0f5163dfc7a63/fonts/ttf) |
| Atkinson Hyperlegible Next | Regular | [googlefonts/atkinson-hyperlegible-next](https://github.com/googlefonts/atkinson-hyperlegible-next/tree/7925f50f649b3813257faf2f4c0b381011f434f1/fonts/ttf) |

Source Sans 3 Regular is used for interface text, Semibold for screen headings.
Literata is the default reading font. Atkinson Hyperlegible Next and the Vita
system font can be selected in Reading settings. Reading bold and emphasis retain
the existing second-pass/underline effects; the app does not load separate bold
or italic reading faces or publisher-embedded fonts.

FreeType checks glyph coverage before measurement and drawing. Missing glyphs
use the Vita system PGF font, with the same run boundaries in both operations.
Missing or unloadable font files fall back to PGF and show a startup notice.
The system font's own coverage is still the limit for fallback characters.

Font downloads happen during development, never at build time or app startup.
File checksums are recorded in [SHA256SUMS](SHA256SUMS).
