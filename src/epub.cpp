#include "reader.hpp"
#include "performance.hpp"
#include "online.hpp"
#include "tinyxml2.h"
#include "unzip.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <stdexcept>
#include <sys/stat.h>
#include <tuple>
#include <utility>

namespace reader
{
    namespace
    {
        struct ArchiveStamp
        {
            std::uint64_t size = 0, device = 0, inode = 0;
            std::int64_t modified = 0, changed = 0;
            long modified_ns = 0, changed_ns = 0;
            bool valid = false;

            bool matches(const ArchiveStamp &other) const
            {
                return valid && other.valid &&
                       std::tie(size, device, inode, modified, changed, modified_ns, changed_ns) ==
                       std::tie(other.size, other.device, other.inode, other.modified, other.changed, other.modified_ns, other.changed_ns);
            }
        };

        ArchiveStamp archive_stamp(const std::string &path)
        {
            struct stat info{};
            if (stat(path.c_str(), &info) != 0 || info.st_size < 0)
                return {};
            ArchiveStamp result;
            result.size = info.st_size;
            result.device = info.st_dev;
            result.inode = info.st_ino;
            result.modified = info.st_mtime;
            result.changed = info.st_ctime;
#if defined(__linux__)
            result.modified_ns = info.st_mtim.tv_nsec;
            result.changed_ns = info.st_ctim.tv_nsec;
#endif
            result.valid = true;
            return result;
        }
    }

    struct EpubDirectory
    {
        struct Entry
        {
            std::string path;
            unz64_file_pos position;
        };
        ArchiveStamp stamp;
        std::vector<Entry> entries;

        const Entry *find(const std::string &path) const
        {
            const auto found = std::lower_bound(entries.begin(), entries.end(), path,
                                               [](const Entry &entry, const std::string &name) { return entry.path < name; });
            return found != entries.end() && found->path == path ? &*found : nullptr;
        }
    };

    namespace
    {
        using tinyxml2::XMLDocument;
        using tinyxml2::XMLElement;
        using tinyxml2::XMLNode;

        constexpr std::size_t metadata_limit = 512 * 1024;
        constexpr std::size_t archive_limit = 512 * 1024 * 1024;
        constexpr std::size_t directory_name_limit = 1024 * 1024;
        constexpr std::size_t lookup_name_limit = 256; // The vendored unzLocateFile limit, including the terminator.

        class Archive
        {
        public:
            explicit Archive(const std::string &path, const std::shared_ptr<const EpubDirectory> &directory = {}) : stamp_(archive_stamp(path))
            {
                auto size = stamp_.size;
                if (!stamp_.valid)
                {
                    std::ifstream input(path, std::ios::binary | std::ios::ate);
                    const auto length = input.tellg();
                    if (!input || length < 0)
                        throw std::runtime_error("Cannot open this EPUB.");
                    size = static_cast<std::uint64_t>(length);
                }
                if (size > archive_limit)
                    throw std::runtime_error("EPUB exceeds the 512 MiB archive limit.");
                file_ = unzOpen64(path.c_str());
                perf::add_zip_work({1, 0, 0});
                if (!file_)
                    throw std::runtime_error("This EPUB is not a readable ZIP archive.");
                unz_global_info64 info{};
                if (unzGetGlobalInfo64(file_, &info) != UNZ_OK || info.number_entry > 20000)
                {
                    unzClose(file_);
                    file_ = nullptr;
                    throw std::runtime_error("EPUB archive has too many entries or is damaged.");
                }
                entry_count_ = static_cast<std::size_t>(info.number_entry);
                if (!stamp_.matches(archive_stamp(path)))
                    stamp_ = {};
                if (directory && stamp_.matches(directory->stamp))
                    directory_ = directory;
            }
            ~Archive()
            {
                if (file_)
                    unzClose(file_);
            }
            Archive(const Archive &) = delete;
            Archive &operator=(const Archive &) = delete;

            void build_index()
            {
                if (!stamp_.valid || !entry_count_)
                    return;
                std::size_t scanned = 0;
                try
                {
                    directory_ = scan_directory(scanned);
                }
                catch (const std::bad_alloc &)
                {
                    // A ZIP remains readable through the existing bounded lookup path.
                }
                perf::add_zip_work({0, scanned, 0});
                if (!directory_)
                    unzGoToFirstFile(file_);
            }

            const std::shared_ptr<const EpubDirectory> &directory() const { return directory_; }

            bool contains(const std::string &path) { return directory_ ? directory_->find(path) != nullptr : locate(path); }

            std::string read(const std::string &path, std::size_t limit)
            {
                Entry entry(file_, open_entry(path, limit), false);
                std::string result;
                entry.read_to(result, entry.size);
                entry.finish();
                return result;
            }

            bool probe_image(const std::string &path, ImageInfo &info, std::string &error)
            {
                Entry entry(file_, open_entry(path, max_image_bytes), true);
                std::string bytes;
                entry.read_to(bytes, std::min<std::size_t>(33, entry.size));
                bool valid = inspect_image(bytes, info, error);
                const bool jpeg = bytes.size() >= 2 && bytes.compare(0, 2, "\xff\xd8", 2) == 0;
                if (!valid && jpeg)
                {
                    constexpr std::size_t jpeg_probe_limit = 64 * 1024;
                    for (std::size_t target = 4 * 1024; target <= jpeg_probe_limit && bytes.size() < entry.size; target *= 2)
                    {
                        entry.read_to(bytes, std::min(target, entry.size));
                        valid = inspect_image(bytes, info, error);
                        if (valid)
                            break;
                    }
                    // Keep compatibility with JPEGs that have large metadata segments before their dimensions.
                    if (!valid && bytes.size() < entry.size)
                    {
                        entry.read_to(bytes, entry.size);
                        valid = inspect_image(bytes, info, error);
                    }
                }
                entry.finish();
                return valid;
            }

        private:
            class Entry
            {
            public:
                const std::size_t size;
                Entry(unzFile file, std::size_t length, bool probe) : size(length), file_(file), probe_(probe) {}
                Entry(const Entry &) = delete;
                Entry &operator=(const Entry &) = delete;
                ~Entry()
                {
                    if (file_)
                        unzCloseCurrentFile(file_);
                    if (probe_)
                        perf::add_zip_work({0, 0, inflated_});
                }

                void read_to(std::string &bytes, std::size_t target)
                {
                    auto at = bytes.size();
                    bytes.resize(target);
                    while (at < target)
                    {
                        const auto count = unzReadCurrentFile(file_, &bytes[at], static_cast<unsigned>(target - at));
                        if (count <= 0)
                            throw std::runtime_error("EPUB document is truncated or damaged.");
                        at += static_cast<std::size_t>(count);
                        inflated_ += static_cast<std::size_t>(count);
                    }
                }
                void finish()
                {
                    const auto status = unzCloseCurrentFile(file_);
                    file_ = nullptr;
                    if (status != UNZ_OK)
                        throw std::runtime_error("EPUB document failed its checksum.");
                }

            private:
                unzFile file_;
                bool probe_;
                std::size_t inflated_ = 0;
            };

            std::size_t open_entry(const std::string &path, std::size_t limit)
            {
                if (!locate(path))
                    throw std::runtime_error("An EPUB document is missing from the archive.");
                unz_file_info64 info{};
                if (unzGetCurrentFileInfo64(file_, &info, nullptr, 0, nullptr, 0, nullptr, 0) != UNZ_OK)
                    throw std::runtime_error("Cannot read EPUB entry information.");
                if (info.flag & 1)
                    throw std::runtime_error("Encrypted EPUB entries are not supported.");
                if (info.uncompressed_size > limit)
                    throw std::runtime_error("EPUB document exceeds its memory limit.");
                if (info.compression_method != 0 && info.compression_method != Z_DEFLATED)
                    throw std::runtime_error("Unsupported EPUB ZIP compression.");
                if (unzOpenCurrentFile(file_) != UNZ_OK)
                    throw std::runtime_error("Cannot decompress EPUB document.");
                return static_cast<std::size_t>(info.uncompressed_size);
            }

            std::shared_ptr<const EpubDirectory> scan_directory(std::size_t &scanned)
            {
                if (unzGoToFirstFile(file_) != UNZ_OK)
                    return {};
                auto index = std::make_shared<EpubDirectory>();
                index->stamp = stamp_;
                index->entries.reserve(entry_count_);
                std::size_t name_bytes = 0;
                for (std::size_t i = 0; i < entry_count_; ++i)
                {
                    ++scanned;
                    char name[lookup_name_limit]{};
                    unz_file_info64 info{};
                    if (unzGetCurrentFileInfo64(file_, &info, name, sizeof(name), nullptr, 0, nullptr, 0) != UNZ_OK)
                        return {};
                    if (info.size_filename < sizeof(name))
                    {
                        const auto length = static_cast<std::size_t>(info.size_filename);
                        unz64_file_pos position{};
                        if (std::memchr(name, '\0', length) || length + 1 > directory_name_limit - name_bytes ||
                            unzGetFilePos64(file_, &position) != UNZ_OK)
                            return {};
                        name_bytes += length + 1;
                        index->entries.push_back({std::string(name, length), position});
                    }
                    if (i + 1 < entry_count_ && unzGoToNextFile(file_) != UNZ_OK)
                        return {};
                }
                std::sort(index->entries.begin(), index->entries.end(), [](const EpubDirectory::Entry &a, const EpubDirectory::Entry &b)
                          { return std::tie(a.path, a.position.num_of_file) < std::tie(b.path, b.position.num_of_file); });
                return index;
            }

            bool locate(const std::string &path)
            {
                if (directory_)
                {
                    const auto *entry = directory_->find(path);
                    if (!entry)
                        return false;
                    char name[lookup_name_limit]{};
                    if (unzGoToFilePos64(file_, &entry->position) == UNZ_OK &&
                        unzGetCurrentFileInfo64(file_, nullptr, name, sizeof(name), nullptr, 0, nullptr, 0) == UNZ_OK &&
                        std::memcmp(name, path.c_str(), path.size() + 1) == 0)
                        return true;
                    directory_.reset();
                    unzGoToFirstFile(file_);
                }
#ifdef NOVELREADER_PROFILE
                unsigned scanned = 0;
                const auto result = unzLocateFileCounted(file_, path.c_str(), 1, &scanned);
                perf::add_zip_work({0, scanned, 0});
                return result == UNZ_OK;
#else
                return unzLocateFile(file_, path.c_str(), 1) == UNZ_OK;
#endif
            }

            unzFile file_ = nullptr;
            ArchiveStamp stamp_;
            std::size_t entry_count_ = 0;
            std::shared_ptr<const EpubDirectory> directory_;
        };

        void validate_image_path(const std::string &path)
        {
            if (path.empty() || path.front() == '/' || path.find_first_of(":\\") != std::string::npos ||
                path == ".." || path.compare(0, 3, "../") == 0 || path.find("/../") != std::string::npos ||
                path.find('\0') != std::string::npos)
                throw std::runtime_error("This illustration has no supported local image path.");
        }

        std::string local(const char *name)
        {
            if (!name)
                return {};
            const auto *colon = std::strrchr(name, ':');
            return colon ? colon + 1 : name;
        }

        std::string attr(const XMLElement *node, const char *name)
        {
            if (!node)
                return {};
            for (auto *a = node->FirstAttribute(); a; a = a->Next())
                if (local(a->Name()) == name)
                    return a->Value();
            return {};
        }

        const XMLElement *child(const XMLNode *node, const char *name)
        {
            if (node)
                for (auto *e = node->FirstChildElement(); e; e = e->NextSiblingElement())
                    if (local(e->Name()) == name)
                        return e;
            return nullptr;
        }

        void visit(const XMLElement *node, const std::function<void(const XMLElement *)> &action)
        {
            if (!node)
                return;
            action(node);
            for (auto *e = node->FirstChildElement(); e; e = e->NextSiblingElement())
                visit(e, action);
        }

        bool token(const std::string &list, const std::string &word)
        {
            std::size_t start = 0;
            while (start < list.size())
            {
                while (start < list.size() && std::isspace(static_cast<unsigned char>(list[start])))
                    ++start;
                auto end = start;
                while (end < list.size() && !std::isspace(static_cast<unsigned char>(list[end])))
                    ++end;
                if (list.substr(start, end - start) == word)
                    return true;
                start = end;
            }
            return false;
        }

        int hex(char ch)
        {
            if (ch >= '0' && ch <= '9')
                return ch - '0';
            if (ch >= 'a' && ch <= 'f')
                return ch - 'a' + 10;
            if (ch >= 'A' && ch <= 'F')
                return ch - 'A' + 10;
            return -1;
        }

        std::string percent_decode(const std::string &value)
        {
            std::string decoded;
            for (std::size_t i = 0; i < value.size(); ++i)
            {
                if (value[i] != '%')
                    decoded += value[i];
                else
                {
                    if (i + 2 >= value.size() || hex(value[i + 1]) < 0 || hex(value[i + 2]) < 0)
                        throw std::runtime_error("Invalid EPUB link encoding.");
                    decoded += static_cast<char>(hex(value[i + 1]) * 16 + hex(value[i + 2]));
                    i += 2;
                }
            }
            return decoded;
        }

        std::string resolve(const std::string &base, const std::string &href)
        {
            const auto decoded = percent_decode(href.substr(0, href.find_first_of("#?")));
            if (decoded.empty())
                return base;
            if (decoded[0] == '/' || decoded.find_first_of(":\\") != std::string::npos ||
                decoded.find('\0') != std::string::npos || decoded.size() > 2048)
                throw std::runtime_error("External or absolute EPUB paths are unsupported.");
            const auto slash = base.find_last_of('/');
            const auto joined = (slash == std::string::npos ? "" : base.substr(0, slash + 1)) + decoded;
            std::vector<std::string> parts;
            for (std::size_t at = 0; at <= joined.size();)
            {
                const auto end = joined.find('/', at);
                const auto part = joined.substr(at, end == std::string::npos ? end : end - at);
                if (part == "..")
                {
                    if (parts.empty())
                        throw std::runtime_error("EPUB path escapes its archive root.");
                    parts.pop_back();
                }
                else if (!part.empty() && part != ".")
                    parts.push_back(part);
                if (end == std::string::npos)
                    break;
                at = end + 1;
            }
            std::string result;
            for (const auto &part : parts)
            {
                if (!result.empty())
                    result += '/';
                result += part;
            }
            if (result.empty())
                throw std::runtime_error("Invalid empty EPUB document path.");
            return result;
        }

        DocumentTarget link_target(const std::string &base, const std::string &href)
        {
            const auto hash = href.find('#');
            auto fragment = hash == std::string::npos ? std::string{} : percent_decode(href.substr(hash + 1));
            if (fragment.size() > 2048 || fragment.find('\0') != std::string::npos || !valid_utf8(fragment))
                throw std::runtime_error("Invalid EPUB link fragment.");
            return {resolve(base, href), std::move(fragment)};
        }

        // XML has only five named entities. Common legacy XHTML entities are converted
        // to numeric XML references before parsing; there is no DTD/entity expansion.
        std::string xhtml_entities(const std::string &input)
        {
            const std::map<std::string, std::string> entities = {
                {"nbsp", "&#160;"}, {"ensp", "&#8194;"}, {"emsp", "&#8195;"}, {"thinsp", "&#8201;"}, {"mdash", "&#8212;"}, {"ndash", "&#8211;"}, {"hellip", "&#8230;"}, {"lsquo", "&#8216;"}, {"rsquo", "&#8217;"}, {"ldquo", "&#8220;"}, {"rdquo", "&#8221;"}, {"copy", "&#169;"}, {"reg", "&#174;"}, {"trade", "&#8482;"}, {"bull", "&#8226;"}, {"eacute", "&#233;"}, {"Eacute", "&#201;"}, {"aacute", "&#225;"}, {"ouml", "&#246;"}, {"uuml", "&#252;"}, {"times", "&#215;"}};
            std::string result;
            result.reserve(input.size());
            for (std::size_t at = 0; at < input.size();)
            {
                if (input[at] == '&')
                {
                    const auto end = input.find(';', at + 1);
                    if (end != std::string::npos && end - at <= 12)
                    {
                        const auto found = entities.find(input.substr(at + 1, end - at - 1));
                        if (found != entities.end())
                        {
                            result += found->second;
                            at = end + 1;
                            continue;
                        }
                    }
                }
                result += input[at++];
            }
            return result;
        }

        void parse(XMLDocument &document, const std::string &data, const char *error)
        {
            if (!valid_utf8(data) || data.find('\0') != std::string::npos ||
                document.Parse(data.data(), data.size()) != tinyxml2::XML_SUCCESS || !document.RootElement())
                throw std::runtime_error(error);
            // Numeric XML references are decoded by TinyXML-2. Check the decoded values
            // too, so malformed scalar values cannot reach font rendering.
            visit(document.RootElement(), [&](const XMLElement *element)
                  {
                      for (auto *attribute = element->FirstAttribute(); attribute; attribute = attribute->Next())
                          if (!valid_utf8(attribute->Value()))
                              throw std::runtime_error(error);
                      for (auto *node = element->FirstChild(); node; node = node->NextSibling())
                          if (node->ToText() && !valid_utf8(node->Value()))
                              throw std::runtime_error(error); });
        }

        bool ignored(const std::string &tag)
        {
            return tag == "head" || tag == "script" || tag == "style" || tag == "rt" || tag == "rp";
        }

        std::string node_text(const XMLNode *node)
        {
            if (!node)
                return {};
            if (node->ToText())
                return node->Value();
            if (node->ToElement() && ignored(local(node->Value())))
                return {};
            std::string result;
            for (auto *n = node->FirstChild(); n; n = n->NextSibling())
                result += node_text(n);
            return result;
        }

        std::string compact(const std::string &input)
        {
            std::string output;
            bool space = false;
            for (std::size_t i = 0; i < input.size(); ++i)
            {
                const auto ch = static_cast<unsigned char>(input[i]);
                if (ch <= 0x20 || (ch == 0xc2 && i + 1 < input.size() && static_cast<unsigned char>(input[i + 1]) == 0xa0))
                {
                    if (ch == 0xc2)
                        ++i;
                    space = !output.empty();
                }
                else
                {
                    if (space)
                        output += ' ';
                    output += input[i];
                    space = false;
                }
            }
            return output;
        }

        class TextBuilder
        {
        public:
            explicit TextBuilder(std::string base) : base_(std::move(base)) {}
            Book book;
            void text(const std::string &input, unsigned style)
            {
                for (std::size_t i = 0; i < input.size();)
                {
                    const auto ch = static_cast<unsigned char>(input[i]);
                    if (ch <= 0x20 || (ch == 0xc2 && i + 1 < input.size() && static_cast<unsigned char>(input[i + 1]) == 0xa0))
                    {
                        if (ch == 0xc2)
                            ++i;
                        ++i;
                        pending_ = true;
                        continue;
                    }
                    if (pending_ && !book.text.empty() && book.text.back() != '\n')
                        append(" ", style);
                    pending_ = false;
                    auto end = i + 1;
                    while (end < input.size() && (static_cast<unsigned char>(input[end]) & 0xc0) == 0x80)
                        ++end;
                    append(input.substr(i, end - i), style);
                    i = end;
                }
            }
            void block(unsigned count = 2)
            {
                pending_ = false;
                if (book.text.empty())
                    return;
                unsigned existing = 0;
                for (auto at = book.text.size(); at > 0 && book.text[at - 1] == '\n'; --at)
                    ++existing;
                while (existing++ < count)
                    append("\n", Plain);
            }
            void walk(const XMLNode *node, unsigned style = Plain)
            {
                if (node->ToText())
                {
                    text(node->Value(), style);
                    return;
                }
                const auto *element = node->ToElement();
                if (!element)
                    return;
                const auto tag = local(element->Name());
                if (ignored(tag) || element->FindAttribute("hidden"))
                    return;
                if (tag == "em" || tag == "i")
                    style |= Emphasis;
                if (tag == "strong" || tag == "b")
                    style |= Strong;
                const bool heading = tag.size() == 2 && tag[0] == 'h' && tag[1] >= '1' && tag[1] <= '6';
                if (heading)
                {
                    style |= Heading | Strong;
                    if (book.title.empty())
                        book.title = compact(node_text(element));
                }
                const bool boundary = heading || tag == "p" || tag == "div" || tag == "section" ||
                                      tag == "blockquote" || tag == "li" || tag == "tr" || tag == "pre" || tag == "hr";
                if (boundary)
                    block();
                auto id = attr(element, "id");
                if (id.empty() && tag == "a")
                    id = attr(element, "name");
                if (!id.empty() && id.size() <= 2048 && book.anchors.size() < 8192)
                    book.anchors.emplace(std::move(id), book.text.size());
                if (tag == "br")
                {
                    block(1);
                    return;
                }
                if (tag == "img" || tag == "image")
                {
                    block();
                    const auto alt = compact(attr(element, "alt"));
                    if (book.illustrations.size() >= 1024)
                        throw std::runtime_error("Too many illustrations in this chapter.");
                    std::string path;
                    const auto reference = attr(element, tag == "img" ? "src" : "href");
                    if (!reference.empty())
                    {
                        try
                        {
                            path = resolve(base_, reference);
                        }
                        catch (const std::runtime_error &)
                        {
                        }
                    }
                    book.illustrations.push_back({book.text.size(), alt.empty() ? "Illustration " + std::to_string(book.illustrations.size() + 1) : alt, path});
                    text(alt.empty() ? "[Illustration]" : "[Illustration: " + alt + "]", Emphasis);
                    book.illustrations.back().end = book.text.size();
                    block();
                    return;
                }
                if (tag == "li")
                    text("- ", Plain);
                if (tag == "td" || tag == "th")
                    pending_ = true;
                const auto begin = book.text.size();
                for (auto *n = element->FirstChild(); n; n = n->NextSibling())
                    walk(n, style);
                if (tag == "a" && book.text.size() > begin && book.links.size() < 4096)
                {
                    const auto href = attr(element, "href");
                    if (!href.empty())
                        try
                        {
                            book.links.push_back({begin, book.text.size(), link_target(base_, href)});
                        }
                        catch (const std::runtime_error &)
                        {
                            // External or malformed links are displayed as plain text.
                        }
                }
                if (boundary)
                    block();
            }

        private:
            void append(const std::string &value, unsigned style)
            {
                if (book.text.size() + value.size() > max_chapter_bytes || book.styles.size() >= 100000)
                    throw std::runtime_error("EPUB chapter text or formatting exceeds its memory limit.");
                const auto begin = book.text.size();
                book.text += value;
                if (!style)
                    return;
                if (!book.styles.empty() && book.styles.back().end == begin && book.styles.back().style == style)
                    book.styles.back().end = book.text.size();
                else
                    book.styles.push_back({begin, book.text.size(), style});
            }
            bool pending_ = false;
            std::string base_;
        };

        std::vector<ContentsEntry> read_navigation(Archive &archive, const std::string &path, bool ncx,
                                                   const std::set<std::string> &spine_paths)
        {
            std::vector<ContentsEntry> entries;
            if (path.empty())
                return entries;
            try
            {
                XMLDocument document;
                parse(document, xhtml_entities(archive.read(path, metadata_limit)), "Invalid EPUB navigation.");
                visit(document.RootElement(), [&](const XMLElement *element)
                      {
                          std::string href, label;
                          unsigned depth = 0;
                          if (ncx && local(element->Name()) == "navPoint")
                          {
                              href = attr(child(element, "content"), "src");
                              label = compact(node_text(child(element, "navLabel")));
                              for (auto *parent = element->Parent(); parent; parent = parent->Parent())
                                  if (parent->ToElement() && local(parent->Value()) == "navPoint")
                                      ++depth;
                          }
                          else if (!ncx && local(element->Name()) == "a")
                          {
                              // Only use the table of contents, not page lists or landmarks.
                              const XMLNode *parent = element->Parent();
                              while (parent && (!parent->ToElement() || local(parent->Value()) != "nav"))
                              {
                                  if (parent->ToElement() && local(parent->Value()) == "ol")
                                      ++depth;
                                  parent = parent->Parent();
                              }
                              if (depth)
                                  --depth;
                              if (!parent || !token(attr(parent->ToElement(), "type"), "toc"))
                                  return;
                              href = attr(element, "href");
                              label = compact(node_text(element));
                          }
                          if (!href.empty() && !label.empty() && entries.size() < 8192)
                          {
                              try
                              {
                                  auto target = link_target(path, href);
                                  if (spine_paths.count(target.path))
                                      entries.push_back({std::move(label), std::move(target), depth});
                              }
                              catch (const std::runtime_error &)
                              {
                              }
                          } });
            }
            catch (const std::runtime_error &)
            {
                // Navigation is optional; the spine is authoritative.
            }
            return entries;
        }

        void check_encryption(Archive &archive, const Publication &publication)
        {
            if (!archive.contains("META-INF/encryption.xml"))
                return;
            XMLDocument document;
            parse(document, archive.read("META-INF/encryption.xml", metadata_limit), "Invalid EPUB encryption metadata.");
            visit(document.RootElement(), [&](const XMLElement *element)
                  {
                      if (local(element->Name()) != "CipherReference")
                          return;
                      const auto uri = resolve("", attr(element, "URI"));
                      for (const auto &chapter : publication.chapters)
                          if (uri == chapter.path)
                              throw std::runtime_error("DRM-encrypted EPUB chapters are not supported."); });
        }

    } // namespace

    bool open_publication(const std::string &path, Publication &publication, std::string &error)
    {
        const perf::Scope timing(perf::Event::Metadata);
        error.clear();
        if (is_online_book(path))
            return online::open_cached(path, publication, error);
        try
        {
            Publication result;
            result.path = path;
            result.title = title_from_filename(path);
            result.epub = is_epub_book(path);
            if (!result.epub)
            {
                result.chapters.push_back({result.title, path});
                publication = std::move(result);
                return true;
            }
            Archive archive(path);
            archive.build_index();
            if (archive.read("mimetype", 128) != "application/epub+zip")
                throw std::runtime_error("Archive is not an EPUB publication.");
            XMLDocument container;
            parse(container, archive.read("META-INF/container.xml", metadata_limit), "Invalid EPUB container metadata.");
            if (local(container.RootElement()->Name()) != "container")
                throw std::runtime_error("Invalid EPUB container root.");
            const auto *files = child(container.RootElement(), "rootfiles");
            const XMLElement *rootfile = nullptr;
            if (files)
                for (auto *e = files->FirstChildElement(); e; e = e->NextSiblingElement())
                    if (local(e->Name()) == "rootfile" && attr(e, "media-type") == "application/oebps-package+xml")
                    {
                        rootfile = e;
                        break;
                    }
            if (!rootfile || attr(rootfile, "full-path").empty())
                throw std::runtime_error("EPUB has no package document.");
            const auto package_path = resolve("", attr(rootfile, "full-path"));
            XMLDocument package;
            parse(package, archive.read(package_path, metadata_limit), "Invalid EPUB package document.");
            if (local(package.RootElement()->Name()) != "package")
                throw std::runtime_error("Invalid EPUB package root.");
            const auto *metadata = child(package.RootElement(), "metadata");
            const auto title = compact(node_text(child(metadata, "title")));
            if (!title.empty())
                result.title = title;
            if (metadata)
                for (auto *e = metadata->FirstChildElement(); e; e = e->NextSiblingElement())
                    if (local(e->Name()) == "creator")
                    {
                        const auto author = compact(node_text(e));
                        if (!author.empty())
                        {
                            if (!result.author.empty())
                                result.author += ", ";
                            result.author += author;
                        }
                    }
            struct Item
            {
                std::string path, media;
            };
            std::map<std::string, Item> manifest;
            const auto *manifest_xml = child(package.RootElement(), "manifest");
            std::string nav_path, cover_id;
            if (metadata)
                for (auto *e = metadata->FirstChildElement(); e; e = e->NextSiblingElement())
                    if (local(e->Name()) == "meta" && attr(e, "name") == "cover")
                        cover_id = attr(e, "content");
            if (manifest_xml)
                for (auto *e = manifest_xml->FirstChildElement(); e; e = e->NextSiblingElement())
                {
                    if (local(e->Name()) != "item")
                        continue;
                    const auto id = attr(e, "id"), href = attr(e, "href");
                    if (id.empty() || href.empty() || manifest.size() >= 10000)
                        throw std::runtime_error("Invalid or oversized EPUB manifest.");
                    // Remote resources cannot be part of the local reader's spine.
                    std::string resolved;
                    try
                    {
                        resolved = resolve(package_path, href);
                    }
                    catch (const std::runtime_error &)
                    {
                    }
                    if (!manifest.emplace(id, Item{resolved, attr(e, "media-type")}).second)
                        throw std::runtime_error("EPUB manifest contains duplicate IDs.");
                    if (token(attr(e, "properties"), "nav"))
                        nav_path = resolved;
                    if (token(attr(e, "properties"), "cover-image"))
                        result.cover_path = resolved;
                }
            if (result.cover_path.empty())
            {
                const auto cover = manifest.find(cover_id);
                if (cover != manifest.end())
                    result.cover_path = cover->second.path;
            }
            const auto *spine = child(package.RootElement(), "spine");
            if (!spine)
                throw std::runtime_error("EPUB has no reading order.");
            for (auto *e = spine->FirstChildElement(); e; e = e->NextSiblingElement())
            {
                if (local(e->Name()) != "itemref" || attr(e, "linear") == "no")
                    continue;
                const auto found = manifest.find(attr(e, "idref"));
                if (found == manifest.end() || found->second.path.empty())
                    throw std::runtime_error("Invalid EPUB spine reference.");
                if (found->second.media != "application/xhtml+xml" && found->second.media != "text/html")
                    throw std::runtime_error("EPUB spine contains an unsupported document type.");
                if (!archive.contains(found->second.path))
                    throw std::runtime_error("An EPUB chapter is missing.");
                if (result.chapters.size() >= max_chapters)
                    throw std::runtime_error("EPUB has too many chapters.");
                result.chapters.push_back({"Chapter " + std::to_string(result.chapters.size() + 1), found->second.path});
            }
            if (result.chapters.empty())
                throw std::runtime_error("EPUB has no readable chapters.");
            std::set<std::string> spine_paths;
            for (const auto &chapter : result.chapters)
                spine_paths.insert(chapter.path);
            result.contents = read_navigation(archive, nav_path, false, spine_paths);
            const auto ncx = manifest.find(attr(spine, "toc"));
            if (result.contents.empty() && ncx != manifest.end())
                result.contents = read_navigation(archive, ncx->second.path, true, spine_paths);
            std::map<std::string, std::string> labels;
            for (const auto &entry : result.contents)
                labels.emplace(entry.target.path, entry.title);
            for (auto &chapter : result.chapters)
            {
                const auto found = labels.find(chapter.path);
                if (found != labels.end())
                    chapter.title = found->second;
            }
            check_encryption(archive, result);
            result.epub_directory = archive.directory();
            publication = std::move(result);
            return true;
        }
        catch (const std::exception &exception)
        {
            error = exception.what();
            return false;
        }
    }

    bool load_chapter(const Publication &publication, std::size_t index, Book &chapter, std::string &error, bool probe_illustrations)
    {
        const perf::Scope timing(perf::Event::Chapter);
        error.clear();
        try
        {
            if (index >= publication.chapters.size())
                throw std::runtime_error("Chapter no longer exists in this book.");
            if (publication.online)
            {
                if (!load_book(publication.chapters[index].path, chapter, error))
                    return false;
                chapter.title = publication.chapters[index].title;
                return true;
            }
            if (!publication.epub)
                return load_book(publication.path, chapter, error);
            Archive archive(publication.path, publication.epub_directory);
            const auto &source = publication.chapters[index];
            TextBuilder builder(source.path);
            {
                XMLDocument document;
                parse(document, xhtml_entities(archive.read(source.path, max_chapter_bytes)),
                      "Chapter is not valid UTF-8 XHTML (or may be encrypted).");
                const auto *body = child(document.RootElement(), "body");
                if (!body)
                    throw std::runtime_error("EPUB chapter has no XHTML body.");
                builder.walk(body);
            }
            if (builder.book.text.empty())
                builder.text("[This section contains no text.]", Emphasis);
            // Whitespace at the end adds no useful pages. Styled offsets remain valid.
            while (!builder.book.text.empty() && builder.book.text.back() == '\n')
                builder.book.text.pop_back();
            if (builder.book.title.empty())
                builder.book.title = source.title;
            if (probe_illustrations)
                for (auto &image : builder.book.illustrations)
                {
                    try
                    {
                        validate_image_path(image.path);
                        ImageInfo info;
                        if (archive.probe_image(image.path, info, image.error))
                        {
                            image.width = info.width;
                            image.height = info.height;
                        }
                    }
                    catch (const std::exception &exception)
                    {
                        image.error = exception.what();
                    }
                }
            chapter = std::move(builder.book);
            return true;
        }
        catch (const std::exception &exception)
        {
            error = exception.what();
            return false;
        }
    }

    namespace
    {
        bool read_epub_image(const std::string &archive_path, const std::shared_ptr<const EpubDirectory> &directory,
                             const std::string &image_path, std::string &bytes, std::string &error)
        {
            error.clear();
            try
            {
                validate_image_path(image_path);
                Archive archive(archive_path, directory);
                auto data = archive.read(image_path, max_image_bytes);
                ImageInfo info;
                if (!inspect_image(data, info, error))
                    return false;
                bytes = std::move(data);
                return true;
            }
            catch (const std::exception &exception)
            {
                error = exception.what();
                return false;
            }
        }
    }

    bool load_epub_image(const std::string &archive_path, const std::string &image_path, std::string &bytes, std::string &error)
    {
        return read_epub_image(archive_path, {}, image_path, bytes, error);
    }

    bool load_epub_image(const Publication &publication, const std::string &image_path, std::string &bytes, std::string &error)
    {
        return read_epub_image(publication.path, publication.epub_directory, image_path, bytes, error);
    }

} // namespace reader
