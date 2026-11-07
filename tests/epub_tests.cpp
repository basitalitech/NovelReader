#include "reader.hpp"
#include <zlib.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include <unistd.h>

namespace {
void check(bool value, const std::string& message) { if (!value) throw std::runtime_error(message); }
void word(std::string& out, std::uint32_t value, int bytes) {
    for (int i = 0; i < bytes; ++i) out += static_cast<char>(value >> (8 * i));
}
struct Entry { std::string name, text; bool compressed = true; unsigned flags = 0; bool bad_crc = false; };

// Tiny fixture writer, independent of the ZIP reader. Deflated entries also use
// data descriptors, as produced by streaming EPUB packagers.
void zip(const std::string& path, const std::vector<Entry>& entries) {
    std::string data, central;
    for (const auto& entry : entries) {
        std::string payload = entry.text;
        if (entry.compressed) {
            z_stream stream{};
            check(deflateInit2(&stream, 6, Z_DEFLATED, -MAX_WBITS, 8, Z_DEFAULT_STRATEGY) == Z_OK, "deflate init");
            payload.resize(compressBound(entry.text.size()));
            stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(entry.text.data()));
            stream.avail_in = static_cast<uInt>(entry.text.size());
            stream.next_out = reinterpret_cast<Bytef*>(&payload[0]);
            stream.avail_out = static_cast<uInt>(payload.size());
            const auto status = deflate(&stream, Z_FINISH);
            payload.resize(stream.total_out);
            deflateEnd(&stream);
            check(status == Z_STREAM_END, "deflate failed");
        }
        const auto crc = static_cast<std::uint32_t>(crc32(0,
            reinterpret_cast<const Bytef*>(entry.text.data()), static_cast<uInt>(entry.text.size()))) + (entry.bad_crc ? 1 : 0);
        const auto offset = data.size();
        const auto flags = entry.flags | (entry.compressed ? 8u : 0u);
        word(data, 0x04034b50, 4); word(data, 20, 2); word(data, flags, 2);
        word(data, entry.compressed ? 8 : 0, 2); word(data, 0, 4);
        word(data, entry.compressed ? 0 : crc, 4);
        word(data, entry.compressed ? 0 : payload.size(), 4);
        word(data, entry.compressed ? 0 : entry.text.size(), 4);
        word(data, entry.name.size(), 2); word(data, 0, 2); data += entry.name; data += payload;
        if (entry.compressed) {
            word(data, 0x08074b50, 4); word(data, crc, 4); word(data, payload.size(), 4); word(data, entry.text.size(), 4);
        }
        word(central, 0x02014b50, 4); word(central, 20, 2); word(central, 20, 2);
        word(central, flags, 2); word(central, entry.compressed ? 8 : 0, 2); word(central, 0, 4);
        word(central, crc, 4); word(central, payload.size(), 4); word(central, entry.text.size(), 4);
        word(central, entry.name.size(), 2); word(central, 0, 2); word(central, 0, 2);
        word(central, 0, 2); word(central, 0, 2); word(central, 0, 4); word(central, offset, 4);
        central += entry.name;
    }
    const auto offset = data.size(); data += central;
    word(data, 0x06054b50, 4); word(data, 0, 4);
    word(data, entries.size(), 2); word(data, entries.size(), 2);
    word(data, central.size(), 4); word(data, offset, 4); word(data, 0, 2);
    std::ofstream file(path, std::ios::binary);
    file.write(data.data(), static_cast<std::streamsize>(data.size()));
    check(static_cast<bool>(file), "Could not write ZIP fixture");
}

const std::string container = R"(<container xmlns="urn:oasis:names:tc:opendocument:xmlns:container"><rootfiles>
<rootfile full-path="OPS/package.opf" media-type="application/oebps-package+xml"/></rootfiles></container>)";
const std::string package = R"(<package xmlns="http://www.idpf.org/2007/opf" version="3.0"><metadata xmlns:dc="http://purl.org/dc/elements/1.1/">
<dc:title>A Novel &amp; an Adventure</dc:title><dc:creator>Sample Author</dc:creator></metadata><manifest>
<item id="second" href="Text/two.xhtml" media-type="application/xhtml+xml"/>
<item id="first" href="Text/one%20chapter.xhtml" media-type="application/xhtml+xml"/>
<item id="nav" href="Nav/nav.xhtml" media-type="application/xhtml+xml" properties="nav scripted"/>
<item id="ncx" href="toc.ncx" media-type="application/x-dtbncx+xml"/>
<item id="extra" href="extra.xhtml" media-type="application/xhtml+xml"/>
</manifest><spine toc="ncx"><itemref idref="first"/><itemref idref="extra" linear="no"/><itemref idref="second"/></spine></package>)";
const std::string first = R"(<?xml version="1.0"?><!DOCTYPE html><html xmlns="http://www.w3.org/1999/xhtml"><head><title>Hidden title</title><style>Hidden style</style></head>
<body><h1>First chapter</h1><p>Hello <em>quiet <strong>world</strong></em> &amp; welcome&nbsp;home.</p>
<p>Next paragraph.<br/>Another line. &#x65E5;&#26412;</p><script>Hidden script</script><!-- Hidden comment -->
<p><ruby>Base<rt>Hidden ruby</rt></ruby></p><img alt="A moonlit lake" src="../Images/cover.jpg"/></body></html>)";
const std::string second = R"(<html xmlns="http://www.w3.org/1999/xhtml"><body><h2>Second chapter</h2><p>The end.</p></body></html>)";
const std::string nav = R"(<html xmlns="http://www.w3.org/1999/xhtml" xmlns:epub="http://www.idpf.org/2007/ops"><body>
<nav epub:type="toc"><ol><li><a href="../Text/one%20chapter.xhtml#start">The beginning</a><ol>
<li><a href="../Text/two.xhtml">The ending</a></li></ol></li></ol></nav>
<nav epub:type="landmarks"><a href="../Text/one%20chapter.xhtml">Wrong label</a></nav></body></html>)";
const std::string ncx = R"(<ncx><navMap><navPoint><navLabel><text>NCX beginning</text></navLabel><content src="Text/one%20chapter.xhtml#start"/>
<navPoint><navLabel><text>NCX ending</text></navLabel><content src="Text/two.xhtml"/></navPoint></navPoint></navMap></ncx>)";

std::vector<Entry> fixture() {
    return {{"mimetype", "application/epub+zip", false}, {"META-INF/container.xml", container},
        {"OPS/package.opf", package}, {"OPS/Text/two.xhtml", second},
        {"OPS/Text/one chapter.xhtml", first}, {"OPS/Nav/nav.xhtml", nav}, {"OPS/toc.ncx", ncx}};
}
void replace(std::string& text, const std::string& before, const std::string& after) {
    const auto at = text.find(before); check(at != std::string::npos, "Fixture replacement not found");
    text.replace(at, before.size(), after);
}
bool has_style(const reader::Book& book, const std::string& text, unsigned style) {
    const auto begin = book.text.find(text);
    return begin != std::string::npos && std::any_of(book.styles.begin(), book.styles.end(), [&](const reader::StyleSpan& span) {
        return span.begin <= begin && span.end >= begin + text.size() && (span.style & style) == style;
    });
}
}

int main() {
    char pattern[] = "./epub-tests-XXXXXX";
    const auto* temporary = mkdtemp(pattern);
    if (!temporary) { std::perror("Test directory"); return 1; }
    const std::string directory(temporary), path = directory + "/novel.EPUB", save = directory + "/save.pos";
    int result = 0;
    try {
        check(reader::is_supported_book("Novel.EPUB") && !reader::is_supported_book(".hidden.epub"), "EPUB recognition");
        check(reader::title_from_filename("My_Novel.epub") == "My Novel", "EPUB filename title");
        auto entries = fixture(); zip(path, entries);
        reader::Publication publication;
        reader::Book book;
        std::string error;
        check(reader::open_publication(path, publication, error), "Open EPUB: " + error);
        check(publication.title == "A Novel & an Adventure" && publication.author == "Sample Author", "EPUB metadata");
        check(publication.chapters.size() == 2 && publication.chapters[0].path == "OPS/Text/one chapter.xhtml" &&
              publication.chapters[1].path == "OPS/Text/two.xhtml", "Spine order, relative paths or linear=no");
        check(publication.chapters[0].title == "The beginning" && publication.chapters[1].title == "The ending", "EPUB3 labels");
        check(reader::load_chapter(publication, 0, book, error), "Load EPUB chapter: " + error);
        check(book.title == "First chapter" && book.text.find("Hello quiet world & welcome home.") != std::string::npos,
              "Inline whitespace or entities");
        check(book.text.find("\n\nNext paragraph.\nAnother line.") != std::string::npos, "Paragraphs and line breaks");
        check(book.text.find("\xe6\x97\xa5\xe6\x9c\xac") != std::string::npos, "Numeric UTF-8 entities");
        check(book.text.find("Hidden") == std::string::npos && book.text.find("Base") != std::string::npos, "Hidden content filtering");
        check(book.text.find("[Illustration: A moonlit lake]") != std::string::npos, "Image placeholder");
        check(has_style(book, "First chapter", reader::Heading | reader::Strong), "Heading formatting");
        check(has_style(book, "world", reader::Emphasis | reader::Strong), "Nested emphasis formatting");
        check(reader::load_chapter(publication, 1, book, error) && book.text.find("The end.") != std::string::npos,
              "Second chapter load");
        check(!reader::load_chapter(publication, 99, book, error) && book.title == "Second chapter", "Failed chapter changed current book");

        check(reader::save_progress(save, {8, true, 12, 1, 0}), "Chapter progress save");
        const auto progress = reader::load_progress(save);
        check(progress.chapter == 1 && progress.offset == 8 && progress.bookmark_chapter == 0 &&
              progress.bookmark == 12 && progress.has_bookmark, "Chapter/bookmark round trip");
        { std::ofstream file(save); file << "NRP1\n21 1 9\n"; }
        const auto legacy = reader::load_progress(save);
        check(legacy.offset == 21 && legacy.bookmark == 9 && legacy.chapter == 0 && legacy.bookmark_chapter == 0,
              "Legacy TXT progress migration");
        { std::ofstream file(save); file << "NRP2\n1 1 2 99999 0\n"; }
        check(reader::load_progress(save).offset == 0, "Corrupt chapter position");

        entries = fixture(); entries[5].text = "invalid optional nav"; zip(path, entries);
        check(reader::open_publication(path, publication, error) && publication.chapters[1].title == "NCX ending", "EPUB2 NCX fallback");
        entries[6].text = "invalid optional ncx"; zip(path, entries);
        check(reader::open_publication(path, publication, error) && publication.chapters[0].title == "Chapter 1", "Spine label fallback");

        entries = fixture(); entries[3].text = "broken XHTML"; zip(path, entries);
        check(reader::open_publication(path, publication, error) && reader::load_chapter(publication, 0, book, error), "Chapter loading is not lazy");
        const auto old_text = book.text;
        check(!reader::load_chapter(publication, 1, book, error) && book.text == old_text, "Malformed chapter changed current chapter");

        entries = fixture(); entries[3].text = std::string(reader::max_chapter_bytes + 1, 'x'); zip(path, entries);
        check(reader::open_publication(path, publication, error) && !reader::load_chapter(publication, 1, book, error), "ZIP expansion limit");
        entries = fixture(); entries[3].bad_crc = true; zip(path, entries);
        check(reader::open_publication(path, publication, error) && !reader::load_chapter(publication, 1, book, error), "Bad chapter checksum accepted");
        for (const auto& bad_text : {std::string("<html><body><p>\xed\xa0\x80</p></body></html>"),
             std::string("<html><body><p>&#xD800;</p></body></html>"),
             std::string("<html><head><title>No body</title></head></html>")}) {
            entries = fixture(); entries[3].text = bad_text; zip(path, entries);
            check(reader::open_publication(path, publication, error) && !reader::load_chapter(publication, 1, book, error),
                  "Invalid chapter text accepted");
        }
        entries = fixture(); entries[3].text = "<html><body><img src=\"illustration.jpg\"/></body></html>"; zip(path, entries);
        check(reader::open_publication(path, publication, error) && reader::load_chapter(publication, 1, book, error) &&
              book.text == "[Illustration]", "Image-only chapter cannot be navigated");
        entries[3].text = "<html><body>  </body></html>"; zip(path, entries);
        check(reader::open_publication(path, publication, error) && reader::load_chapter(publication, 1, book, error) &&
              !book.text.empty(), "Empty chapter cannot be navigated");
        entries = fixture(); entries[3].flags = 1; zip(path, entries);
        check(reader::open_publication(path, publication, error) && !reader::load_chapter(publication, 1, book, error), "Encrypted entry accepted");
        entries = fixture(); entries.push_back({"META-INF/encryption.xml",
            R"(<encryption><EncryptedData><CipherData><CipherReference URI="OPS/Text/two.xhtml"/></CipherData></EncryptedData></encryption>)"});
        zip(path, entries); check(!reader::open_publication(path, publication, error), "DRM chapter accepted");
        entries.back().text = R"(<encryption><EncryptedData><CipherData><CipherReference URI="OPS/font.otf"/></CipherData></EncryptedData></encryption>)";
        zip(path, entries); check(reader::open_publication(path, publication, error), "Obfuscated font blocked text reading");

        for (const auto& bad_path : {"../../../escape.xhtml", "https://example.com/chapter", "%2Fabsolute.xhtml", "bad%00name"}) {
            entries = fixture(); replace(entries[2].text, "Text/two.xhtml", bad_path); zip(path, entries);
            check(!reader::open_publication(path, publication, error), "Unsafe spine path accepted");
        }
        entries = fixture(); replace(entries[2].text, "idref=\"first\"", "idref=\"missing\""); zip(path, entries);
        check(!reader::open_publication(path, publication, error), "Missing manifest reference accepted");
        entries = fixture(); replace(entries[2].text, "id=\"first\"", "id=\"second\""); zip(path, entries);
        check(!reader::open_publication(path, publication, error), "Duplicate manifest ID accepted");
        entries = fixture(); entries.erase(entries.begin() + 3); zip(path, entries);
        check(!reader::open_publication(path, publication, error), "Missing chapter accepted");
        entries = fixture(); entries[1].text = "<container>"; zip(path, entries);
        check(!reader::open_publication(path, publication, error), "Malformed XML accepted");
        entries = fixture(); entries[0].text = "not an epub"; zip(path, entries);
        check(!reader::open_publication(path, publication, error), "Wrong mimetype accepted");
        { std::ofstream file(path); file << "not a zip"; }
        check(!reader::open_publication(path, publication, error), "Non-ZIP EPUB accepted");
        std::cout << "EPUB tests passed\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; result = 1; }
    std::remove(path.c_str()); std::remove(save.c_str()); rmdir(directory.c_str());
    return result;
}
