#pragma once
#include <string>
#include <vector>
#include <vita2d.h>

struct ThumbnailPixels
{
    unsigned width = 0, height = 0;
    std::vector<unsigned char> rgba;
};

// Returns one caller-owned RGBA GPU texture. Decoders receive bounded buffers.
vita2d_texture *decode_texture(const std::string &bytes, std::string &error);
vita2d_texture *decode_thumbnail(const std::string &bytes, unsigned width, unsigned height, std::string &error);
// CPU-only decode for worker threads. Texture allocation stays on the render thread.
ThumbnailPixels decode_thumbnail_pixels(const std::string &bytes, unsigned width, unsigned height, std::string &error);
vita2d_texture *upload_thumbnail(const ThumbnailPixels &pixels);