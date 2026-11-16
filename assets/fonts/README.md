# Bundled fonts

[README](../../README.md) · [Interface](../../docs/interface.md) ·
[Third-party notices](../../third_party/README.md)

These unmodified static TrueType fonts and their SIL Open Font License 1.1
notices are included in the Vita package. Checksums are recorded in
[SHA256SUMS](SHA256SUMS). The app does not download fonts at build time or startup.

| Family | Included faces | Upstream revision | License |
| --- | --- | --- | --- |
| Source Sans 3 | Regular, Semibold | [Adobe Source Sans](https://github.com/adobe-fonts/source-sans/tree/87b37a2daaed80fcb8e8ccb0085c4d72ddade12e/TTF) | [OFL](SourceSans3-LICENSE.md) |
| Literata | Regular | [Google Fonts Literata](https://github.com/googlefonts/literata/tree/0c2761b727a1b3a7cffd313c37f0f5163dfc7a63/fonts/ttf) | [OFL](Literata-OFL.txt) |
| Atkinson Hyperlegible Next | Regular | [Atkinson Hyperlegible Next](https://github.com/googlefonts/atkinson-hyperlegible-next/tree/7925f50f649b3813257faf2f4c0b381011f434f1/fonts/ttf) | [OFL](AtkinsonHyperlegibleNext-OFL.txt) |

Source Sans 3 supplies interface text and headings. Reader Settings selects
Literata, Atkinson Hyperlegible Next, or the Vita system font. Saved reading-font
identifiers remain 0 = Literata, 1 = Atkinson, 2 = system. Bold publication text
uses a second pass in the selected reading family; emphasis is underlined.
Publisher-embedded and italic font faces are not loaded.

[The font renderer](../../src/vita_fonts.cpp) checks glyph coverage through
FreeType. A missing glyph first tries the other bundled faces, then an optional
user font, then the Vita firmware's Latin, Japanese, Korean, and Chinese PGF
fonts. The system reading face uses firmware fonts directly. Unavailable bundled
fonts show a startup notice. Firmware glyphs outside the BMP use a visible
replacement; a supplied TrueType face can cover supplementary characters.

Measurement uses cached FreeType/PGF metrics without allocating glyph textures.
Bundled and custom text draw through shared vita2d glyph atlases per face and
pixel size. Different text lines reuse the same glyphs; complete-line textures
are reserved for Home headings, which also apply kerning. Atlases rotate after
128 distinct characters to keep drawing within their finite capacity. Firmware
glyphs share up to four 512 × 512 atlas pages, and each language font opens only
when needed. Evicted atlases are released after the GPU finishes the frame.
Actual rasterization and readability need device validation;
[font tests](../../tests/vita_font_tests.cpp) and
[firmware tests](../../tests/firmware_font_tests.cpp) check metrics, routing,
language selection, and cache lifetimes through stubs.

## Language coverage and custom fallback

UTF-8 books and BOM-marked UTF-16 TXT files normalize to the same UTF-8 byte
positions, including Latin accents, Cyrillic, Greek, CJK, and supplementary
characters. Display depends on available font coverage. The reader does not
perform Arabic joining, bidirectional layout, Indic shaping, or script-specific
hyphenation and punctuation rules. Menus and the Flite voice remain English.

For additional coverage, copy a static Unicode TrueType font you are licensed
to use to `ux0:data/NovelReader/fonts/fallback.ttf`, then restart the app. It is
used automatically for characters absent from the chosen and bundled families;
no settings migration is needed. A font adds glyphs, not complex-script shaping.
The file is optional and is not downloaded or distributed by the app.
