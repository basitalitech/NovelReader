#include "reader.hpp"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <unistd.h>

namespace {
void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void write(const std::string& path, const std::string& text) {
    std::ofstream file(path, std::ios::binary);
    file.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!file) throw std::runtime_error("Could not write test fixture");
}

int columns(const std::string& text) {
    int width = 0;
    for (unsigned char ch : text) if ((ch & 0xc0) != 0x80) ++width;
    return width;
}
}

int main() {
    char pattern[] = "./novelreader-tests-XXXXXX";
    const auto* created = mkdtemp(pattern);
    if (!created) { std::perror("Create test directory"); return 1; }
    const std::string directory(created);
    const auto fixture = directory + "/book.txt";
    const auto state = reader::progress_path(directory, "book.txt");
    const auto settings_path = directory + "/settings.txt";
    int result = 0;
    try {
        check(reader::is_text_book("Novel.TXT"), "Uppercase TXT extension rejected");
        check(!reader::is_text_book("Novel.epub"), "Unsupported book listed");
        check(!reader::is_text_book(".hidden.txt"), "Hidden book listed");
        check(reader::title_from_filename("folder/My_Novel.txt") == "My Novel", "Wrong title");

        reader::Book book;
        std::string error;
        write(fixture, "\xef\xbb\xbf" "Chapter 1\r\n\r\nCaf\xc3\xa9\tstory\rEnd");
        check(reader::load_book(fixture, book, error), "Valid UTF-8 book rejected");
        check(book.text == "Chapter 1\n\nCaf\xc3\xa9    story\nEnd", "BOM or line endings not normalized");

        write(fixture, "\xc0\xaf");
        check(!reader::load_book(fixture, book, error), "Overlong UTF-8 accepted");
        write(fixture, "\xed\xa0\x80");
        check(!reader::load_book(fixture, book, error), "UTF-8 surrogate accepted");
        write(fixture, "\xf4\x90\x80\x80");
        check(!reader::load_book(fixture, book, error), "Out-of-range code point accepted");
        write(fixture, "\xe2\x82");
        check(!reader::load_book(fixture, book, error), "Truncated UTF-8 accepted");
        write(fixture, std::string("A\0B", 3));
        check(!reader::load_book(fixture, book, error), "Binary book accepted");
        write(fixture, " \r\n\t");
        check(!reader::load_book(fixture, book, error), "Empty book accepted");
        write(fixture, std::string(reader::max_book_bytes + 1, 'a'));
        check(!reader::load_book(fixture, book, error), "Oversized book accepted");

        auto lines = reader::wrap_text("alpha beta\n\nabcdefghij", 6, columns);
        check(lines.size() == 5 && lines[0].text == "alpha" && lines[1].text == "beta" &&
              lines[2].text.empty() && lines[3].text == "abcdef" && lines[4].text == "ghij",
              "Word wrapping, paragraph spacing, or long-word splitting failed");
        lines = reader::wrap_text("\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e", 1, columns);
        check(lines.size() == 3 && lines[0].text.size() == 3 && lines[2].offset == 6,
              "UTF-8 characters split during wrapping");
        lines = reader::wrap_text("ab", 0, [](const std::string&) { return 100; });
        check(lines.size() == 2, "Layout did not advance with oversized glyphs");

        const std::string story = "One evening the rain stopped and a new chapter began.";
        const auto narrow = reader::wrap_text(story, 10, columns);
        const auto wide = reader::wrap_text(story, 20, columns);
        const auto anchor = narrow[3].offset;
        const auto restored = reader::line_at_offset(wide, anchor);
        check(wide[restored].offset <= anchor &&
              (restored + 1 == wide.size() || wide[restored + 1].offset > anchor),
              "Reading position lost when text reflows");
        check(reader::line_at_offset(wide, story.size() + 100) == wide.size() - 1,
              "Out-of-range progress not clamped");
        bool limited = false;
        try { reader::wrap_text(std::string(100001, '\n'), 10, columns); }
        catch (const std::length_error&) { limited = true; }
        check(limited, "Unbounded line allocation");

        check(reader::load_progress(state).offset == 0, "Missing progress has no default");
        check(reader::save_progress(state, {anchor, true, 7}), "Progress save failed");
        auto progress = reader::load_progress(state);
        check(progress.offset == anchor && progress.has_bookmark && progress.bookmark == 7,
              "Progress or bookmark failed round trip");
        check(reader::save_progress(state, {4, true, 8}), "Replacing an existing save failed");
        check(reader::load_progress(state).offset == 4, "Save replacement was not persisted");
        write(state, "NRP1\n-1 1 5\n");
        check(reader::load_progress(state).offset == 0, "Corrupt progress not reset");
        check(reader::progress_path(directory, "a/b.txt") != reader::progress_path(directory, "b.txt"),
              "Book identities collide");
        check(!reader::save_progress(directory + "/missing/save", {}), "Failed write reported success");

        check(reader::save_settings(settings_path, {2, 0, 1}), "Settings save failed");
        auto settings = reader::load_settings(settings_path);
        check(settings.font == 2 && settings.spacing == 0 && settings.theme == 1, "Settings failed round trip");
        settings.immersive=0;
        check(reader::save_settings(settings_path,settings) && reader::load_settings(settings_path).immersive==0,
              "Reading controls preference did not survive restart");
        settings.immersive=1;
        check(reader::save_settings(settings_path,settings) && reader::load_settings(settings_path).immersive==1,
              "Immersive preference did not survive restart");
        settings.reading_font = 1;
        check(reader::save_settings(settings_path, settings) && reader::load_settings(settings_path).reading_font == 1,
              "Sans reading font did not survive restart");
        settings.reading_font = 2;
        check(reader::save_settings(settings_path, settings) && reader::load_settings(settings_path).reading_font == 2,
              "System reading font did not survive restart");
        write(settings_path, "NRS2\n2 1 0 2 1\n");
        settings = reader::load_settings(settings_path);
        check(settings.reading_font == 0 && settings.font == 2 && settings.library_sort == 2 && settings.library_filter == 1,
              "Legacy preferences did not migrate to Literata");
        write(settings_path, "NRS3\n1 1 0 0 0 99\n");
        check(reader::load_settings(settings_path).reading_font == 2, "Reading font not bounded");
        check(reader::load_settings(settings_path).immersive==1,"Legacy settings lost the immersive default");
        write(settings_path,"NRS4\n1 1 0 0 0 0 -99\n");
        check(reader::load_settings(settings_path).immersive==0,"Negative reading-view preference was not bounded");
        write(settings_path,"NRS4\n1 1 0 0 0 0 99\n");
        check(reader::load_settings(settings_path).immersive==1,"Reading-view preference was not bounded");
        write(settings_path,"NRS4\n1 1 0 0 0 0\n");
        check(reader::load_settings(settings_path).immersive==0,"New defaults should show reading controls");
        write(settings_path, "NRS3\n2 1 0 2 1\n");
        check(reader::load_settings(settings_path).font == 1, "Truncated font settings have no defaults");
        write(settings_path, "NRS1\n99 -3 123\n");
        settings = reader::load_settings(settings_path);
        check(settings.font == 2 && settings.spacing == 0 && settings.theme == 2, "Settings not bounded");
        write(settings_path, "broken");
        check(reader::load_settings(settings_path).font == 1, "Corrupt settings have no defaults");
        std::cout << "Reader tests passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        result = 1;
    }
    std::remove(fixture.c_str());
    std::remove(state.c_str());
    std::remove(settings_path.c_str());
    rmdir(directory.c_str());
    return result;
}
