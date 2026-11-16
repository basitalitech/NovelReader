#pragma once

#include "file_io.hpp"
#include "online.hpp"
#include "vita_images.hpp"
#include <sstream>
#include <sys/stat.h>
#include <zlib.h>

namespace reader
{
    namespace thumbnails
    {
        constexpr unsigned width = 144, height = 200;

        inline std::string cache_path(const std::string &directory, const std::string &book_path)
        {
            return directory + '/' + online::stable_id(book_path) + ".thumb";
        }

        inline std::string source_revision(const std::string &source, const std::string &member)
        {
            struct stat info{};
            if (stat(source.c_str(), &info) != 0 || !S_ISREG(info.st_mode))
                return {};
            std::ostringstream key;
            key << source << '\n'
                << member << '\n'
                << info.st_size << ' ' << info.st_mtime << ' '
                << info.st_ctime << ' ' << info.st_ino;
#if defined(__linux__)
            key << ' ' << info.st_mtim.tv_nsec << ' ' << info.st_ctim.tv_nsec;
#endif
            return online::stable_id(key.str());
        }

        inline ThumbnailPixels load(const std::string &path, const std::string &revision)
        {
            if (revision.empty())
                return {};
            try
            {
                const auto file = online::read_file(path, compressBound(width * height * 4) + 96);
                const auto newline = file.find('\n');
                if (newline == std::string::npos || newline > 96)
                    return {};
                std::istringstream header(file.substr(0, newline));
                std::string magic, stored_revision;
                ThumbnailPixels result;
                if (!(header >> magic >> stored_revision >> result.width >> result.height) || magic != "NRTH1" ||
                    stored_revision != revision || !result.width || !result.height || result.width > width || result.height > height)
                    return {};
                result.rgba.resize(result.width * result.height * 4);
                uLongf size = result.rgba.size();
                const auto *bytes = reinterpret_cast<const unsigned char *>(file.data() + newline + 1);
                if (uncompress(result.rgba.data(), &size, bytes, file.size() - newline - 1) != Z_OK || size != result.rgba.size())
                    return {};
                return result;
            }
            catch (const std::exception &)
            {
                return {};
            }
        }

        inline bool save(const std::string &path, const std::string &revision, const ThumbnailPixels &pixels)
        {
            if (revision.empty() || !pixels.width || !pixels.height || pixels.width > width || pixels.height > height ||
                pixels.rgba.size() != pixels.width * pixels.height * 4)
                return false;
            uLongf size = compressBound(pixels.rgba.size());
            std::vector<unsigned char> compressed(size);
            if (compress2(compressed.data(), &size, pixels.rgba.data(), pixels.rgba.size(), Z_BEST_SPEED) != Z_OK)
                return false;
            return detail::atomic_write_stream(path, [&](std::ostream &output)
                                               {
                output << "NRTH1 " << revision << ' ' << pixels.width << ' ' << pixels.height << '\n';
                output.write(reinterpret_cast<const char *>(compressed.data()), static_cast<std::streamsize>(size)); });
        }
    }
}