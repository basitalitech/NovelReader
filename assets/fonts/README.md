# Bundled fonts

Unmodified static TrueType fonts, distributed under the SIL Open Font License
1.1. All three families and their licenses are included in the installable Vita package.

| Family | Files | Upstream revision |
| --- | --- | --- |
| Source Sans 3 | Regular, Semibold | [adobe-fonts/source-sans](https://github.com/adobe-fonts/source-sans/tree/87b37a2daaed80fcb8e8ccb0085c4d72ddade12e/TTF) |
| Literata | Regular | [googlefonts/literata](https://github.com/googlefonts/literata/tree/0c2761b727a1b3a7cffd313c37f0f5163dfc7a63/fonts/ttf) |
| Atkinson Hyperlegible Next | Regular | [googlefonts/atkinson-hyperlegible-next](https://github.com/googlefonts/atkinson-hyperlegible-next/tree/7925f50f649b3813257faf2f4c0b381011f434f1/fonts/ttf) |

Source Sans 3 Regular is used for interface text and Semibold for headings and
emphasized controls. Reader typefaces are Literata, Atkinson Hyperlegible Next,
and the Vita system font. Reader Settings previews the selected family, size,
spacing, and theme; Apply saves and Cancel discards. Existing saved identifiers
remain 0 = Literata, 1 = Atkinson, 2 = system. Bold publication text uses a second
pass in the selected reading family; emphasis is underlined. The app does not
load italic or publisher-embedded fonts.

FreeType checks glyph coverage before measurement and drawing. Missing glyphs
use the Vita system PGF font, with the same run boundaries in both operations.
Missing or unloadable font files fall back to PGF and show a startup notice.
The system font's own coverage is still the limit for fallback characters.

Each loaded TrueType face uses a separate, lazily cached vita2d font object for
each requested pixel size. vita2d's atlas caches by glyph index alone, including
during measurement; sharing one atlas across sizes would rescale previously
cached glyphs and produce uneven digits and soft text. Measurement and drawing
use the same size-specific object. Atlases are reused until application shutdown.

Home's two-line book title uses a separate native-size label path: FreeType
measures and rasterizes each complete line at 25 pixels, with matching kerning
and integer baseline placement. Each line is stored in a transparent padded
texture and drawn without scaling using point sampling. This avoids vita2d's
packed glyph atlas for the title. Up to eight line textures are reused across
book changes; missing glyphs retain the existing system-font fallback.

Font downloads happen during development, never at build time or app startup.
File checksums are recorded in [SHA256SUMS](SHA256SUMS).
