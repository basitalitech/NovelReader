#include "reader.hpp"
#include "performance.hpp"
#include "file_io.hpp"
#include "unicode_marks.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <sstream>
#include <stdexcept>

namespace reader
{
    namespace
    {

        std::size_t next_character(const std::string &text, std::size_t at)
        {
            ++at;
            while (at < text.size() && (static_cast<unsigned char>(text[at]) & 0xc0) == 0x80)
                ++at;
            return at;
        }

        unsigned codepoint(const std::string &text, std::size_t at)
        {
            const unsigned lead = static_cast<unsigned char>(text[at++]);
            if (lead < 0x80)
                return lead;
            const unsigned length = lead < 0xe0 ? 2 : lead < 0xf0 ? 3
                                                                  : 4;
            unsigned code = lead & (0x7f >> length);
            for (unsigned i = 1; i < length && at < text.size(); ++i)
                code = (code << 6) | (static_cast<unsigned char>(text[at++]) & 0x3f);
            return code;
        }

        bool between(unsigned code, unsigned first, unsigned last) { return code >= first && code <= last; }

        bool combining_mark(unsigned code)
        {
            const auto end = std::end(unicode_mark_ranges);
            const auto found = std::lower_bound(std::begin(unicode_mark_ranges), end, code,
                                                [](const unsigned (&range)[2], unsigned value)
                                                { return range[1] < value; });
            return found != end && (*found)[0] <= code;
        }

        unsigned hangul_class(unsigned code)
        {
            if (between(code, 0x1100, 0x115f) || between(code, 0xa960, 0xa97c))
                return 1; // L
            if (between(code, 0x1160, 0x11a7) || between(code, 0xd7b0, 0xd7c6))
                return 2; // V
            if (between(code, 0x11a8, 0x11ff) || between(code, 0xd7cb, 0xd7fb))
                return 3; // T
            if (between(code, 0xac00, 0xd7a3))
                return (code - 0xac00) % 28 ? 5 : 4; // LVT or LV
            return 0;
        }

        std::size_t next_cluster(const std::string &text, std::size_t at)
        {
            auto previous = codepoint(text, at);
            auto end = next_character(text, at);
            bool join_flag = between(previous, 0x1f1e6, 0x1f1ff);
            while (end < text.size() && text[end] != '\n' && static_cast<unsigned char>(text[end]) >= 0x80)
            {
                const auto next = codepoint(text, end);
                const auto left = hangul_class(previous), right = hangul_class(next);
                const bool hangul = (left == 1 && (right == 1 || right == 2 || right >= 4)) ||
                                    ((left == 2 || left == 4) && (right == 2 || right == 3)) ||
                                    ((left == 3 || left == 5) && right == 3);
                if (!hangul && previous != 0x200d && next != 0x200d && next != 0x200c &&
                    !between(next, 0x1f3fb, 0x1f3ff) && !between(next, 0xe0020, 0xe007f) &&
                    !(join_flag && between(next, 0x1f1e6, 0x1f1ff)) && !combining_mark(next))
                    break;
                join_flag = false;
                previous = next;
                end = next_character(text, end);
            }
            return end;
        }

        bool east_asian(unsigned code)
        {
            return between(code, 0x2e80, 0x9fff) || between(code, 0xf900, 0xfaff) ||
                   between(code, 0x20000, 0x323af) || between(code, 0xff61, 0xff9f);
        }

        bool opens_punctuation(unsigned code)
        {
            static const std::u32string openings = U"([{‘“〈《「『【〔〖〘〚（［｛｟｢";
            return openings.find(code) != std::u32string::npos;
        }

        bool closes_punctuation(unsigned code)
        {
            static const std::u32string closings = U")]}!?,.:;’”〉》」』】〕〗〙〛）、。！？：；］｝｠｣､｡ぁぃぅぇぉっゃゅょゎァィゥェォッャュョヮヵヶー々…・";
            return closings.find(code) != std::u32string::npos;
        }

        bool decode_utf16(std::string &raw)
        {
            const bool little = static_cast<unsigned char>(raw[0]) == 0xff;
            if (raw.size() % 2)
                return false;
            const auto unit = [&](std::size_t at) -> unsigned
            {
                const auto a = static_cast<unsigned char>(raw[at]);
                const auto b = static_cast<unsigned char>(raw[at + 1]);
                return little ? a | (b << 8) : (a << 8) | b;
            };
            std::string utf8;
            utf8.reserve(raw.size());
            for (std::size_t at = 2; at < raw.size(); at += 2)
            {
                unsigned code = unit(at);
                if (code >= 0xd800 && code <= 0xdbff)
                {
                    if (at + 3 >= raw.size())
                        return false;
                    const auto low = unit(at + 2);
                    if (low < 0xdc00 || low > 0xdfff)
                        return false;
                    code = 0x10000 + ((code - 0xd800) << 10) + low - 0xdc00;
                    at += 2;
                }
                else if (code >= 0xdc00 && code <= 0xdfff)
                    return false;
                if (code < 0x80)
                    utf8 += static_cast<char>(code);
                else
                {
                    if (code >= 0x10000)
                    {
                        utf8 += static_cast<char>(0xf0 | (code >> 18));
                        utf8 += static_cast<char>(0x80 | ((code >> 12) & 0x3f));
                    }
                    else if (code >= 0x800)
                        utf8 += static_cast<char>(0xe0 | (code >> 12));
                    else
                        utf8 += static_cast<char>(0xc0 | (code >> 6));
                    if (code >= 0x800)
                        utf8 += static_cast<char>(0x80 | ((code >> 6) & 0x3f));
                    utf8 += static_cast<char>(0x80 | (code & 0x3f));
                }
                if (utf8.size() > max_book_bytes)
                    return false;
            }
            raw = std::move(utf8);
            return true;
        }

    } // namespace

    bool valid_utf8(const std::string &text)
    {
        for (std::size_t i = 0; i < text.size();)
        {
            const auto first = static_cast<unsigned char>(text[i++]);
            if (first < 0x80)
                continue;
            unsigned count = 0;
            std::uint32_t value = 0, minimum = 0;
            if (first >= 0xc2 && first <= 0xdf)
            {
                count = 1;
                value = first & 0x1f;
                minimum = 0x80;
            }
            else if (first >= 0xe0 && first <= 0xef)
            {
                count = 2;
                value = first & 0x0f;
                minimum = 0x800;
            }
            else if (first >= 0xf0 && first <= 0xf4)
            {
                count = 3;
                value = first & 0x07;
                minimum = 0x10000;
            }
            else
                return false;
            if (count > text.size() - i)
                return false;
            while (count--)
            {
                const auto byte = static_cast<unsigned char>(text[i++]);
                if ((byte & 0xc0) != 0x80)
                    return false;
                value = (value << 6) | (byte & 0x3f);
            }
            if (value < minimum || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff))
                return false;
        }
        return true;
    }

    namespace
    {

        bool has_extension(const std::string &name, const std::string &suffix)
        {
            const auto slash = name.find_last_of("/\\");
            const auto start = slash == std::string::npos ? 0 : slash + 1;
            if (name.size() - start < suffix.size() || name[start] == '.')
                return false;
            auto extension = name.substr(name.size() - suffix.size());
            for (auto &ch : extension)
                ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            return extension == suffix;
        }

        int bounded(int value) { return std::max(0, std::min(2, value)); }

    } // namespace

    bool is_text_book(const std::string &name)
    {
        return has_extension(name, ".txt");
    }

    bool is_epub_book(const std::string &name)
    {
        return has_extension(name, ".epub");
    }

    bool is_supported_book(const std::string &name)
    {
        return is_text_book(name) || is_epub_book(name) || is_online_book(name);
    }
    bool is_online_book(const std::string &name)
    {
        return name.size() > 4 && name.compare(name.size() - 4, 4, ".nvr") == 0;
    }

    std::string title_from_filename(const std::string &name)
    {
        const auto slash = name.find_last_of("/\\");
        auto title = name.substr(slash == std::string::npos ? 0 : slash + 1);
        if (is_text_book(title))
            title.resize(title.size() - 4);
        else if (is_epub_book(title))
            title.resize(title.size() - 5);
        std::replace(title.begin(), title.end(), '_', ' ');
        return title.empty() ? "Untitled" : title;
    }

    bool load_book(const std::string &path, Book &book, std::string &error)
    {
        const perf::Scope timing(perf::Event::TextRead);
        error.clear();
        std::ifstream input(path, std::ios::binary | std::ios::ate);
        if (!input)
        {
            error = "Cannot open this book.";
            return false;
        }
        const auto size = input.tellg();
        if (size < 0 || size > static_cast<std::streamoff>(max_book_bytes))
        {
            error = "Book exceeds the 8 MiB limit.";
            return false;
        }
        std::string raw(static_cast<std::size_t>(size), '\0');
        input.seekg(0);
        if (!raw.empty() && !input.read(&raw[0], size))
        {
            error = "Could not read the complete book.";
            return false;
        }
        if (raw.compare(0, 2, "\xff\xfe") == 0 || raw.compare(0, 2, "\xfe\xff") == 0)
        {
            if (!decode_utf16(raw))
            {
                error = "Invalid UTF-16 text or decoded text exceeds the 8 MiB limit.";
                return false;
            }
        }
        else if (raw.compare(0, 3, "\xef\xbb\xbf") == 0)
            raw.erase(0, 3);
        if (!valid_utf8(raw) || raw.find('\0') != std::string::npos)
        {
            error = "Save this book as UTF-8 or UTF-16 text (with a BOM) first.";
            return false;
        }
        std::string normalized;
        normalized.reserve(raw.size());
        for (std::size_t i = 0; i < raw.size(); ++i)
        {
            const auto ch = static_cast<unsigned char>(raw[i]);
            if (ch == '\r')
            {
                normalized += '\n';
                if (i + 1 < raw.size() && raw[i + 1] == '\n')
                    ++i;
            }
            else if (ch == '\t')
                normalized += "    ";
            else if (ch == '\n' || ch >= 0x20)
                normalized += static_cast<char>(ch);
            if (normalized.size() > max_book_bytes)
            {
                error = "Expanded text exceeds the 8 MiB limit.";
                return false;
            }
        }
        if (normalized.find_first_not_of(" \n") == std::string::npos)
        {
            error = "This book contains no readable text.";
            return false;
        }
        book = {title_from_filename(path), std::move(normalized), {}, {}};
        return true;
    }

    std::vector<Line> wrap_text(const std::string &text, int width, const Measure &measure)
    {
        return wrap_text(text, width, [&](const std::string &source, std::size_t begin, std::size_t end, bool)
                         { return measure(source.substr(begin, end - begin)); });
    }

    std::vector<Line> wrap_text(const std::string &text, int width, const LineMeasure &measure)
    {
        std::vector<Line> lines;
        std::vector<std::size_t> ends;
        ends.reserve(128);
        width = std::max(1, width);
        std::size_t start = 0;
        while (start < text.size())
        {
            if (lines.size() >= 100000)
                throw std::length_error("Too many display lines");
            if (text[start] == '\n')
            {
                lines.push_back({start++, ""});
                continue;
            }
            // Probe exponentially, then bisect on UTF-8 boundaries. Measuring
            // every growing prefix made each line quadratic in glyph work.
            // Only scan the current probe, even for a book with no newlines.
            ends.clear();
            ends.push_back(start);
            std::size_t fit = 0, probe = 16;
            bool overflow = false;
            bool first = true;
            const auto measure_prefix = [&](std::size_t end)
            {
                const auto result = measure(text, start, end, first);
                first = false;
                return result;
            };
            for (;;)
            {
                while (ends.size() <= probe && ends.back() < text.size() && text[ends.back()] != '\n')
                    ends.push_back(next_cluster(text, ends.back()));
                const auto count = ends.size() - 1;
                if (measure_prefix(ends.back()) > width)
                {
                    std::size_t high = count;
                    while (fit + 1 < high)
                    {
                        const auto mid = fit + (high - fit) / 2;
                        if (measure_prefix(ends[mid]) <= width)
                            fit = mid;
                        else
                            high = mid;
                    }
                    overflow = true;
                    break;
                }
                fit = count;
                if (count < probe || ends.back() == text.size() || text[ends.back()] == '\n')
                    break;
                probe *= 2;
            }
            std::size_t end = ends[std::max<std::size_t>(1, fit)];
            if (overflow && fit)
            {
                // Prefer word boundaries and East Asian breaks that keep punctuation with its text.
                for (auto boundary = fit; boundary > 0; --boundary)
                {
                    const auto left = codepoint(text, ends[boundary - 1]);
                    if (ends[boundary] >= text.size())
                        break;
                    const auto right = codepoint(text, ends[boundary]);
                    if (left == ' ' || right == ' ' || left == 0x3000 || right == 0x3000 ||
                        ((east_asian(left) || east_asian(right)) && !opens_punctuation(left) && !closes_punctuation(right) &&
                         left != 0xa0 && right != 0xa0 && left != 0x2060 && right != 0x2060))
                    {
                        end = ends[boundary];
                        break;
                    }
                }
            }
            auto visible_end = end;
            while (visible_end > start && text[visible_end - 1] == ' ')
                --visible_end;
            lines.push_back({start, text.substr(start, visible_end - start)});
            start = end;
            while (start < text.size() && text[start] == ' ')
                ++start;
            if (start < text.size() && text[start] == '\n')
                ++start;
        }
        if (lines.empty())
            lines.push_back({0, ""});
        return lines;
    }

    std::size_t line_at_offset(const std::vector<Line> &lines, std::size_t offset)
    {
        const auto found = std::upper_bound(lines.begin(), lines.end(), offset,
                                            [](std::size_t position, const Line &line)
                                            { return position < line.offset; });
        return found == lines.begin() ? 0 : static_cast<std::size_t>(found - lines.begin() - 1);
    }

    std::string progress_path(const std::string &directory, const std::string &filename)
    {
        // Stable across launches and toolchains; avoids book names becoming save paths.
        std::uint64_t hash = UINT64_C(14695981039346656037);
        for (unsigned char ch : filename)
        {
            hash ^= ch;
            hash *= UINT64_C(1099511628211);
        }
        std::ostringstream result;
        result << directory << '/' << std::hex << std::setw(16) << std::setfill('0') << hash << ".pos";
        return result.str();
    }

    BookmarkPosition Progress::bookmark_at(std::size_t index) const
    {
        return index ? other_bookmarks[index - 1] : BookmarkPosition{bookmark_chapter, bookmark, bookmark_id};
    }

    bool Progress::add_bookmark(const BookmarkPosition &position)
    {
        for (std::size_t i = 0; i < bookmark_count(); ++i)
        {
            if (bookmark_at(i) == position)
            {
                if (!i)
                    return true;
                remove_bookmark(i);
                break;
            }
        }
        if (bookmark_count() >= max_bookmarks)
            return false;
        if (has_bookmark)
            other_bookmarks.insert(other_bookmarks.begin(), bookmark_at(0));
        bookmark_chapter = position.chapter;
        bookmark = position.offset;
        bookmark_id = position.chapter_id;
        has_bookmark = true;
        return true;
    }

    void Progress::remove_bookmark(std::size_t index)
    {
        if (index)
            other_bookmarks.erase(other_bookmarks.begin() + index - 1);
        else if (other_bookmarks.empty())
        {
            has_bookmark = false;
            bookmark = bookmark_chapter = 0;
            bookmark_id.clear();
        }
        else
        {
            auto first = std::move(other_bookmarks.front());
            other_bookmarks.erase(other_bookmarks.begin());
            bookmark = first.offset;
            bookmark_chapter = first.chapter;
            bookmark_id = std::move(first.chapter_id);
        }
    }

    Progress load_progress(const std::string &path)
    {
        std::ifstream input(path);
        std::string magic;
        std::uint64_t offset = 0, bookmark = 0;
        std::uint64_t chapter = 0, bookmark_chapter = 0;
        int has_bookmark = 0;
        if (!(input >> magic >> offset >> has_bookmark >> bookmark) ||
            (magic != "NRP1" && magic != "NRP2" && magic != "NRP3" && magic != "NRP4" && magic != "NRP5") ||
            offset > max_book_bytes * 4 || bookmark > max_book_bytes * 4 ||
            (has_bookmark != 0 && has_bookmark != 1))
            return {};
        if (magic != "NRP1" && (!(input >> chapter >> bookmark_chapter) ||
                                chapter >= 20000 || bookmark_chapter >= 20000))
            return {};
        int percent = -1;
        std::uint64_t last_read = 0;
        if ((magic == "NRP3" || magic == "NRP4" || magic == "NRP5") && (!(input >> percent >> last_read) || percent < -1 || percent > 100 ||
                                                                        last_read > UINT64_C(32503680000)))
            return {};
        Progress result{static_cast<std::size_t>(offset), has_bookmark == 1, static_cast<std::size_t>(bookmark),
                        static_cast<std::size_t>(chapter), static_cast<std::size_t>(bookmark_chapter), percent, last_read};
        if ((magic == "NRP4" || magic == "NRP5") && (!(input >> std::quoted(result.chapter_id) >> std::quoted(result.bookmark_id)) || result.chapter_id.size() > 1024 || result.bookmark_id.size() > 1024))
            return {};
        if (magic == "NRP5")
        {
            std::size_t count = 0;
            if (!(input >> count) || count >= max_bookmarks || !result.has_bookmark)
                return {};
            for (std::size_t i = 0; i < count; ++i)
            {
                BookmarkPosition mark;
                if (!(input >> mark.chapter >> mark.offset >> std::quoted(mark.chapter_id)) ||
                    mark.chapter >= 20000 || mark.offset > max_book_bytes * 4 || mark.chapter_id.size() > 1024)
                    return {};
                result.other_bookmarks.push_back(std::move(mark));
            }
        }
        return result;
    }

    bool save_progress(const std::string &path, const Progress &progress)
    {
        const perf::Scope timing(perf::Event::ProgressWrite);
        std::ostringstream output;
        const bool multiple = progress.has_bookmark && !progress.other_bookmarks.empty();
        output << (multiple ? "NRP5\n" : "NRP4\n")
               << progress.offset << ' ' << progress.has_bookmark << ' ' << progress.bookmark
               << ' ' << progress.chapter << ' ' << progress.bookmark_chapter << ' ' << progress.percent
               << ' ' << progress.last_read << '\n'
               << std::quoted(progress.chapter_id) << ' ' << std::quoted(progress.bookmark_id) << '\n';
        if (multiple)
        {
            output << progress.other_bookmarks.size() << '\n';
            for (const auto &mark : progress.other_bookmarks)
                output << mark.chapter << ' ' << mark.offset << ' ' << std::quoted(mark.chapter_id) << '\n';
        }
        return detail::atomic_write(path, output.str());
    }

    Settings load_settings(const std::string &path)
    {
        std::ifstream input(path);
        std::string magic;
        Settings result;
        if (!(input >> magic >> result.font >> result.spacing >> result.theme) ||
            (magic != "NRS1" && magic != "NRS2" && magic != "NRS3" && magic != "NRS4"))
            return {};
        if (magic != "NRS1" && !(input >> result.library_sort >> result.library_filter))
            return {};
        if ((magic == "NRS3" || magic == "NRS4") && !(input >> result.reading_font))
            return {};
        if (magic == "NRS4" && !(input >> result.immersive))
            return {};
        if (magic != "NRS4")
            result.immersive = 1; // Preserve the view used by older installations.
        int speech_rate = 175;
        if (magic == "NRS4" && (input >> speech_rate))
            result.speech_rate = std::max(100, std::min(350, speech_rate));
        int page_effect;
        if (magic == "NRS4" && (input >> page_effect))
            result.page_effect = bounded(page_effect);
        int font_half_step = 0, margin = 1, speech_auto_advance = 0;
        if (magic == "NRS4" && (input >> font_half_step >> margin))
        {
            result.font_half_step = result.font < 2 && font_half_step == 1 ? 1 : 0;
            result.margin = bounded(margin);
            if (input >> speech_auto_advance)
                result.speech_auto_advance = speech_auto_advance == 1;
        }
        result.font = bounded(result.font);
        result.spacing = bounded(result.spacing);
        result.theme = std::max(0, std::min(page_theme_count - 1, result.theme));
        result.library_sort = bounded(result.library_sort);
        result.library_filter = bounded(result.library_filter);
        result.reading_font = bounded(result.reading_font);
        result.immersive = std::max(0, std::min(1, result.immersive));
        return result;
    }

    bool save_settings(const std::string &path, const Settings &settings)
    {
        const perf::Scope timing(perf::Event::SettingsWrite);
        std::ostringstream output;
        output << "NRS4\n"
               << bounded(settings.font) << ' ' << bounded(settings.spacing) << ' '
               << std::max(0, std::min(page_theme_count - 1, settings.theme)) << ' ' << bounded(settings.library_sort) << ' '
               << bounded(settings.library_filter) << ' ' << bounded(settings.reading_font) << ' '
               << std::max(0, std::min(1, settings.immersive)) << ' '
               << std::max(100, std::min(350, settings.speech_rate)) << ' '
               << bounded(settings.page_effect) << ' '
               << (settings.font < 2 && settings.font_half_step == 1) << ' '
               << bounded(settings.margin) << ' ' << settings.speech_auto_advance << '\n';
        return detail::atomic_write(path, output.str());
    }

} // namespace reader
