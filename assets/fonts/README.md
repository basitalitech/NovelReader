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
FreeType and falls back to the Vita system PGF font for missing glyphs. Unavailable
bundled fonts also fall back and show a startup notice; the system font's coverage
is the final limit.

Measurement and drawing share font selection, glyph runs, and size-specific
vita2d font objects. Separate atlases prevent one raster size being reused for
another. Home titles use a small cache of whole-line FreeType textures. Caches
are released with the app. Actual rasterization and readability need device
validation; [font tests](../../tests/vita_font_tests.cpp) check routing and fallback
through stubs.
