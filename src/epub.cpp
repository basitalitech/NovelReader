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
#include <stdexcept>
#include <utility>

namespace reader
{
    namespace
    {
        using tinyxml2::XMLDocument;
        using tinyxml2::XMLElement;
        using tinyxml2::XMLNode;

        constexpr std::size_t metadata_limit = 512 * 1024;

        class Archive
        {
        public:
            explicit Archive(const std::string &path)
            {
                std::ifstream input(path, std::ios::binary | std::ios::ate);
                if (!input || input.tellg() < 0)
                    throw std::runtime_error("Cannot open this EPUB.");
                if (input.tellg() > 512 * 1024 * 1024)
                    throw std::runtime_error("EPUB exceeds the 512 MiB archive limit.");
                file_ = unzOpen64(path.c_str());
                if (!file_)
                    throw std::runtime_error("This EPUB is not a readable ZIP archive.");
                unz_global_info64 info{};
                if (unzGetGlobalInfo64(file_, &info) != UNZ_OK || info.number_entry > 20000)
                {
                    unzClose(file_);
                    file_ = nullptr;
                    throw std::runtime_error("EPUB archive has too many entries or is damaged.");
                }
            }
            ~Archive()
            {
                if (file_)
                    unzClose(file_);
            }
            Archive(const Archive &) = delete;
            Archive &operator=(const Archive &) = delete;

            bool contains(const std::string &path) { return unzLocateFile(file_, path.c_str(), 1) == UNZ_OK; }

            std::string read(const std::string &path, std::size_t limit)
            {
                if (!contains(path))
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
                std::string result;
                // Close the entry even if allocation fails; never extract files onto disk.
                try
                {
                    result.reserve(static_cast<std::size_t>(info.uncompressed_size));
                    char buffer[16384];
                    int count = 0;
                    while ((count = unzReadCurrentFile(file_, buffer, sizeof(buffer))) > 0)
                    {
                        if (result.size() + static_cast<std::size_t>(count) > limit)
                            throw std::runtime_error("Expanded EPUB document exceeds its memory limit.");
                        result.append(buffer, static_cast<std::size_t>(count));
                    }
                    if (count < 0 || result.size() != info.uncompressed_size)
                        throw std::runtime_error("EPUB document is truncated or damaged.");
                }
                catch (...)
                {
                    unzCloseCurrentFile(file_);
                    throw;
                }
                if (unzCloseCurrentFile(file_) != UNZ_OK)
                    throw std::runtime_error("EPUB document failed its checksum.");
                return result;
            }

        private:
            unzFile file_ = nullptr;
        };

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

        std::string resolve(const std::string &base, std::string href)
        {
            href = href.substr(0, href.find_first_of("#?"));
            std::string decoded;
            for (std::size_t i = 0; i < href.size(); ++i)
            {
                if (href[i] != '%')
                    decoded += href[i];
                else
                {
                    if (i + 2 >= href.size() || hex(href[i + 1]) < 0 || hex(href[i + 2]) < 0)
                        throw std::runtime_error("Invalid EPUB document path.");
                    decoded += static_cast<char>(hex(href[i + 1]) * 16 + hex(href[i + 2]));
                    i += 2;
                }
            }
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
        for (auto* a = element->FirstAttribute(); a; a = a->Next())
            if (!valid_utf8(a->Value())) throw std::runtime_error(error);
        for (auto* n = element->FirstChild(); n; n = n->NextSibling())
            if (n->ToText() && !valid_utf8(n->Value())) throw std::runtime_error(error); });
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
                for (auto *n = element->FirstChild(); n; n = n->NextSibling())
                    walk(n, style);
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

        void navigation_labels(Archive &archive, const std::string &path, bool ncx,
                               std::map<std::string, std::string> &labels)
        {
            if (path.empty())
                return;
            try
            {
                XMLDocument document;
                parse(document, xhtml_entities(archive.read(path, metadata_limit)), "Invalid EPUB navigation.");
                visit(document.RootElement(), [&](const XMLElement *element)
                      {
            std::string href, label;
            if (ncx && local(element->Name()) == "navPoint") {
                href = attr(child(element, "content"), "src");
                label = compact(node_text(child(element, "navLabel")));
            } else if (!ncx && local(element->Name()) == "a") {
                // Only use the table of contents, not page lists or landmarks.
                const XMLNode* parent = element->Parent();
                while (parent && (!parent->ToElement() || local(parent->Value()) != "nav")) parent = parent->Parent();
                if (!parent || !token(attr(parent->ToElement(), "type"), "toc")) return;
                href = attr(element, "href"); label = compact(node_text(element));
            }
            if (!href.empty() && !label.empty()) {
                try { labels.emplace(resolve(path, href), label); } catch (const std::runtime_error&) { }
            } });
            }
            catch (const std::runtime_error &)
            { /* Navigation is optional; the spine is authoritative. */
            }
        }

        void check_encryption(Archive &archive, const Publication &publication)
        {
            if (!archive.contains("META-INF/encryption.xml"))
                return;
            XMLDocument document;
            parse(document, archive.read("META-INF/encryption.xml", metadata_limit), "Invalid EPUB encryption metadata.");
            visit(document.RootElement(), [&](const XMLElement *element)
                  {
        if (local(element->Name()) != "CipherReference") return;
        const auto uri = resolve("", attr(element, "URI"));
        for (const auto& chapter : publication.chapters)
            if (uri == chapter.path) throw std::runtime_error("DRM-encrypted EPUB chapters are not supported."); });
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
            std::map<std::string, std::string> labels;
            navigation_labels(archive, nav_path, false, labels);
            const auto ncx = manifest.find(attr(spine, "toc"));
            if (ncx != manifest.end())
                navigation_labels(archive, ncx->second.path, true, labels);
            for (auto &chapter : result.chapters)
            {
                const auto found = labels.find(chapter.path);
                if (found != labels.end())
                    chapter.title = found->second;
            }
            check_encryption(archive, result);
            publication = std::move(result);
            return true;
        }
        catch (const std::exception &exception)
        {
            error = exception.what();
            return false;
        }
    }

    bool load_chapter(const Publication &publication, std::size_t index, Book &chapter, std::string &error)
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
            Archive archive(publication.path);
            XMLDocument document;
            parse(document, xhtml_entities(archive.read(publication.chapters[index].path, max_chapter_bytes)),
                  "Chapter is not valid UTF-8 XHTML (or may be encrypted).");
            const auto *body = child(document.RootElement(), "body");
            if (!body)
                throw std::runtime_error("EPUB chapter has no XHTML body.");
            TextBuilder builder(publication.chapters[index].path);
            builder.walk(body);
            if (builder.book.text.empty())
                builder.text("[This section contains no text.]", Emphasis);
            // Whitespace at the end adds no useful pages. Styled offsets remain valid.
            while (!builder.book.text.empty() && builder.book.text.back() == '\n')
                builder.book.text.pop_back();
            if (builder.book.title.empty())
                builder.book.title = publication.chapters[index].title;
            chapter = std::move(builder.book);
            return true;
        }
        catch (const std::exception &exception)
        {
            error = exception.what();
            return false;
        }
    }

    bool load_epub_image(const std::string &archive_path, const std::string &image_path,
                         std::string &bytes, std::string &error)
    {
        error.clear();
        try
        {
            if (image_path.empty() || image_path.front() == '/' || image_path.find_first_of(":\\") != std::string::npos ||
                image_path == ".." || image_path.compare(0, 3, "../") == 0 || image_path.find("/../") != std::string::npos ||
                image_path.find('\0') != std::string::npos)
                throw std::runtime_error("This illustration has no supported local image path.");
            Archive archive(archive_path);
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

} // namespace reader