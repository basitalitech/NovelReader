#include "reader.hpp"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <unistd.h>

namespace
{
      void check(bool condition, const char *message)
      {
            if (!condition)
                  throw std::runtime_error(message);
      }
      std::string png_header(unsigned width, unsigned height)
      {
            std::string bytes("\x89PNG\r\n\x1a\n", 8);
            bytes.append("\0\0\0\x0dIHDR", 8);
            bytes.append(17, '\0');
            for (int i = 0; i < 4; ++i)
            {
                  bytes[16 + i] = static_cast<char>(width >> ((3 - i) * 8));
                  bytes[20 + i] = static_cast<char>(height >> ((3 - i) * 8));
            }
            return bytes;
      }
}

int main()
{
      char pattern[] = "./library-tests-XXXXXX";
      const auto *created = mkdtemp(pattern);
      if (!created)
            return 1;
      const std::string directory(created), path = directory + "/state";
      int status = 0;
      try
      {
            const auto index = directory + "/library.txt";
            std::vector<std::string> collection = {"Series/Volume 1.epub", "Online/novgo/abc.nvr", "Story.txt", "Story.txt"};
            check(reader::save_library(index, collection), "Save mixed local and online library");
            std::vector<std::string> restored_library;
            check(reader::load_library(index, restored_library) && restored_library == std::vector<std::string>({"Series/Volume 1.epub", "Online/novgo/abc.nvr", "Story.txt"}), "Library persists and deduplicates identities");
            check(!reader::save_library(index, {"../outside.txt"}), "Library allowed traversal");
            check(!reader::save_library(index, {"Series"}), "Library allowed folder membership");
            check(reader::load_library(index, restored_library) && restored_library.size() == 3, "Rejected write damaged existing library");
            {
                  std::ofstream corrupt(index);
                  corrupt << "NRLIBRARY1\nStory.txt\n../outside.epub\n";
            }
            check(!reader::load_library(index, restored_library) && restored_library.size() == 3, "Invalid index partially replaced library");
            check(reader::save_library(index, {}) && reader::load_library(index, restored_library) && restored_library.empty(), "Empty library persistence");
            const std::vector<std::string> external = {"uma0:/Books/Novel.TXT", "imc0:/Books/Novel.TXT", "ux0:/Books/Story.epub"};
            check(reader::save_library(index, external) && reader::load_library(index, restored_library) && restored_library == external,
                  "External device identities did not round trip");
            for (const auto &invalid : {"uma0:/../escape.txt", "uma0:/.secret/story.txt", "uma0:/Books//story.txt", "uma0:relative.txt", "uma0:/Books/", "uma0:/Books/../story.txt"})
                  check(!reader::save_library(index, {invalid}), "Invalid external identity accepted");
            check(reader::library_identity("ux0:/data/NovelReader/books/Series/Story.TXT", "ux0:data/NovelReader/books") == "Series/Story.TXT",
                  "Absolute local path changed legacy progress identity");
            check(reader::library_book_path(external[0], "ux0:data/NovelReader/books") == external[0], "External path was prefixed with Library directory");
            check(reader::progress_path(directory, external[0]) != reader::progress_path(directory, external[1]), "Different storage devices share progress");
            std::remove(index.c_str());
            std::vector<reader::LibraryItem> items(4);
            items[0].filename = "b.epub";
            items[0].title = "beta";
            items[1].filename = "a.txt";
            items[1].title = "Alpha";
            items[2].filename = "c.epub";
            items[2].title = "Gamma";
            items[3].filename = "@welcome";
            items[3].title = "A guide";
            items[3].guide = true;
            items[0].progress = {20, false, 0, 1, 0, 50, 100};
            items[2].progress = {40, false, 0, 2, 0, 100, 200};
            check(reader::library_order(items, reader::LibrarySort::Title, reader::LibraryFilter::All) ==
                      std::vector<std::size_t>({1, 0, 2, 3}),
                  "Case-insensitive title sort or guide placement");
            check(reader::library_order(items, reader::LibrarySort::Recent, reader::LibraryFilter::All) ==
                      std::vector<std::size_t>({2, 0, 1, 3}),
                  "Recent sort");
            check(reader::library_order(items, reader::LibrarySort::Progress, reader::LibraryFilter::All) ==
                      std::vector<std::size_t>({2, 0, 1, 3}),
                  "Progress sort");
            check(reader::library_order(items, reader::LibrarySort::Title, reader::LibraryFilter::Reading) ==
                      std::vector<std::size_t>({0}),
                  "Reading filter");
            check(reader::library_order(items, reader::LibrarySort::Title, reader::LibraryFilter::Finished) ==
                      std::vector<std::size_t>({2}),
                  "Finished filter");
            items[2].progress = {};
            check(reader::library_order(items, reader::LibrarySort::Title, reader::LibraryFilter::Finished).empty(), "Empty filter");
            reader::LibraryItem folder;
            folder.filename = "Series";
            folder.title = "Series";
            folder.directory = true;
            items.push_back(folder);
            folder.filename = "Arc/alpha.epub";
            folder.title = "alpha.epub";
            items.push_back(folder);
            for (const auto sort : {reader::LibrarySort::Title, reader::LibrarySort::Recent, reader::LibrarySort::Progress})
            {
                  for (const auto filter : {reader::LibraryFilter::All, reader::LibraryFilter::Reading, reader::LibraryFilter::Finished})
                  {
                        const auto order = reader::library_order(items, sort, filter);
                        check(order.size() >= 2 && order[0] == 5 && order[1] == 4,
                              "Folders must remain alphabetically first and reachable under every filter");
                  }
            }
            check(reader::child_library_folder("", "Series") == "Series" &&
                      reader::child_library_folder("Series", "Arc 1") == "Series/Arc 1",
                  "Nested folder path construction");
            check(reader::parent_library_folder("Series/Arc 1") == "Series" &&
                      reader::parent_library_folder("Series") == "" && reader::parent_library_folder("") == "",
                  "Parent navigation escaped or lost library root");
            check(reader::child_library_folder("", "\xe6\x97\xa5\xe6\x9c\xac") == "\xe6\x97\xa5\xe6\x9c\xac", "UTF-8 folder names rejected");
            for (const auto &invalid : {"", ".", "..", ".hidden", "../escape", "a/b", "a\\b", "ux0:escape"})
                  check(!reader::valid_library_component(invalid), "Invalid library path component accepted");
            bool rejected = false;
            try
            {
                  reader::child_library_folder("../outside", "Volume.txt");
            }
            catch (const std::invalid_argument &)
            {
                  rejected = true;
            }
            check(rejected, "Relative path traversal accepted");
            rejected = false;
            try
            {
                  reader::parent_library_folder("/absolute");
            }
            catch (const std::invalid_argument &)
            {
                  rejected = true;
            }
            check(rejected, "Absolute folder navigation accepted");
            rejected = false;
            try
            {
                  reader::child_library_folder(std::string(760, 'a'), "long-name.txt");
            }
            catch (const std::invalid_argument &)
            {
                  rejected = true;
            }
            check(rejected, "Unbounded folder path accepted");
            const auto root_id = reader::progress_path(directory, reader::child_library_folder("", "Volume.txt"));
            const auto first_id = reader::progress_path(directory, reader::child_library_folder("Series A", "Volume.txt"));
            const auto second_id = reader::progress_path(directory, reader::child_library_folder("Series B", "Volume.txt"));
            check(root_id == reader::progress_path(directory, "Volume.txt"), "Root save identity changed");
            check(root_id != first_id && first_id != second_id && root_id != second_id, "Duplicate filenames share folder progress");
            check(reader::save_progress(root_id, {10, true, 1}) && reader::save_progress(first_id, {20, true, 2}) &&
                      reader::save_progress(second_id, {30, true, 3}),
                  "Could not save folder-specific progress");
            check(reader::load_progress(root_id).offset == 10 && reader::load_progress(first_id).offset == 20 &&
                      reader::load_progress(second_id).offset == 30 && reader::load_progress(first_id).bookmark == 2,
                  "Folder reading progress or bookmarks were overwritten");
            std::remove(root_id.c_str());
            std::remove(first_id.c_str());
            std::remove(second_id.c_str());
            items[0].progress = {0, false, 0, 1, 0};
            check(reader::reading_started(items[0].progress), "Legacy chapter resume not recognized");
            check(reader::reading_started({0, false, 0, 0, 0, 0, 100}), "First-page book not recognized as started");
            check(!reader::reading_started({}), "New book marked as started");
            check(reader::reading_percent(0, 4, 0, 1000, false) == 0, "Start percentage");
            check(reader::reading_percent(1, 4, 500, 1000, false) == 37, "Chapter-weighted percentage");
            check(reader::reading_percent(0, 4, 500, 1000, true) == 25, "Chapter end percentage");
            check(reader::reading_percent(3, 4, 999, 1000, true) == 100, "Finished percentage");
            check(reader::reading_percent(3, 4, 1000, 1000, false) == 99, "Premature finished percentage");
            check(reader::reading_percent(0, 0, 0, 0, true) == 0, "Empty publication percentage");

            const reader::Progress saved{71, true, 99, 3, 1, 67, 1700000000};
            check(reader::save_progress(path, saved), "Save new progress");
            const auto restored = reader::load_progress(path);
            check(restored.percent == 67 && restored.last_read == 1700000000 && restored.chapter == 3 &&
                      restored.offset == 71 && restored.bookmark_chapter == 1 && restored.bookmark == 99,
                  "Progress migration lost fields");
            {
                  std::ofstream file(path);
                  file << "NRP2\n21 1 9 2 0\n";
            }
            const auto legacy = reader::load_progress(path);
            check(legacy.chapter == 2 && legacy.percent == -1 && legacy.last_read == 0 && legacy.bookmark == 9, "Legacy v0.2 save migration");
            {
                  std::ofstream file(path);
                  file << "NRP3\n21 1 9 2 0 101 100\n";
            }
            check(reader::load_progress(path).offset == 0, "Invalid percentage accepted");
            check(reader::save_settings(path, {2, 1, 0, 2, 1}), "Save library preferences");
            const auto settings = reader::load_settings(path);
            check(settings.font == 2 && settings.library_sort == 2 && settings.library_filter == 1, "Library preference round trip");
            {
                  std::ofstream file(path);
                  file << "NRS1\n2 0 1\n";
            }
            check(reader::load_settings(path).font == 2 && reader::load_settings(path).library_sort == 0, "Legacy settings migration");

            reader::ImageInfo info;
            std::string error;
            check(reader::inspect_image(png_header(640, 960), info, error) && info.width == 640 && info.height == 960 &&
                      info.format == reader::ImageFormat::Png,
                  "PNG dimensions");
            check(!reader::inspect_image(png_header(0, 10), info, error), "Zero-sized image accepted");
            check(!reader::inspect_image(png_header(5000, 100), info, error), "Oversized side accepted");
            check(!reader::inspect_image(png_header(4096, 4096), info, error), "Oversized pixel count accepted");
            check(reader::inspect_image(png_header(2048, 2048), info, error), "Supported image size rejected");
            check(!reader::inspect_image(png_header(10, 10).substr(0, 20), info, error), "Truncated PNG accepted");
            check(!reader::inspect_image("GIF89a", info, error), "Unsupported image accepted");
            std::string jpeg("\xff\xd8\xff\xc0\x00\x08\x08\x01\x00\x02\x00\x03", 12);
            check(reader::inspect_image(jpeg, info, error) && info.width == 512 && info.height == 256 &&
                      info.format == reader::ImageFormat::Jpeg,
                  "JPEG dimensions");
            jpeg[3] = static_cast<char>(0xc2);
            check(reader::inspect_image(jpeg, info, error), "Progressive JPEG header rejected");
            jpeg[4] = static_cast<char>(0xff);
            jpeg[5] = static_cast<char>(0xff);
            check(!reader::inspect_image(jpeg, info, error), "Truncated JPEG marker accepted");
            check(!reader::inspect_image(std::string(reader::max_image_bytes + 1, 'x'), info, error), "Image byte limit");
            std::cout << "Library and image header tests passed\n";
      }
      catch (const std::exception &error)
      {
            std::cerr << error.what() << '\n';
            status = 1;
      }
      std::remove(path.c_str());
      rmdir(directory.c_str());
      return status;
}