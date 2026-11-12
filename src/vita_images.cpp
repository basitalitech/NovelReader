#include "vita_images.hpp"
#include "reader.hpp"
#include <png.h>
#include <turbojpeg.h>
#include <algorithm>
#include <cstdint>

vita2d_texture *decode_texture(const std::string &bytes, std::string &error)
{
    reader::ImageInfo info;
    if (!reader::inspect_image(bytes, info, error))
        return nullptr;
    if (info.format == reader::ImageFormat::Png)
    {
        png_image png{};
        png.version = PNG_IMAGE_VERSION;
        if (!png_image_begin_read_from_memory(&png, bytes.data(), bytes.size()))
        {
            error = "Could not read PNG image.";
            png_image_free(&png);
            return nullptr;
        }
        if (png.width != info.width || png.height != info.height)
        {
            error = "PNG dimensions are inconsistent.";
            png_image_free(&png);
            return nullptr;
        }
        auto *texture = vita2d_create_empty_texture(png.width, png.height);
        if (!texture)
        {
            error = "Not enough graphics memory for this image.";
            png_image_free(&png);
            return nullptr;
        }
        png.format = PNG_FORMAT_RGBA;
        // vita2d's linear texture stride is in bytes, including row padding.
        const auto decoded = png_image_finish_read(&png, nullptr, vita2d_texture_get_datap(texture),
                                                   vita2d_texture_get_stride(texture), nullptr);
        png_image_free(&png);
        if (!decoded)
        {
            vita2d_free_texture(texture);
            error = "PNG image is damaged.";
            return nullptr;
        }
        vita2d_texture_set_filters(texture, SCE_GXM_TEXTURE_FILTER_LINEAR, SCE_GXM_TEXTURE_FILTER_LINEAR);
        return texture;
    }
    auto decoder = tjInitDecompress();
    if (!decoder)
    {
        error = "Could not initialize JPEG decoder.";
        return nullptr;
    }
    int width = 0, height = 0, subsampling = 0, colorspace = 0;
    const auto *buffer = reinterpret_cast<const unsigned char *>(bytes.data());
    if (tjDecompressHeader3(decoder, buffer, bytes.size(), &width, &height, &subsampling, &colorspace) < 0 ||
        width != static_cast<int>(info.width) || height != static_cast<int>(info.height))
    {
        tjDestroy(decoder);
        error = "JPEG header is damaged.";
        return nullptr;
    }
    auto *texture = vita2d_create_empty_texture(info.width, info.height);
    if (!texture)
    {
        tjDestroy(decoder);
        error = "Not enough graphics memory for this image.";
        return nullptr;
    }
    const auto decoded = tjDecompress2(decoder, buffer, bytes.size(),
                                       static_cast<unsigned char *>(vita2d_texture_get_datap(texture)), width,
                                       vita2d_texture_get_stride(texture), height, TJPF_RGBA, TJFLAG_STOPONWARNING);
    tjDestroy(decoder);
    if (decoded < 0)
    {
        vita2d_free_texture(texture);
        error = "JPEG is damaged or uses unsupported colors.";
        return nullptr;
    }
    vita2d_texture_set_filters(texture, SCE_GXM_TEXTURE_FILTER_LINEAR, SCE_GXM_TEXTURE_FILTER_LINEAR);
    return texture;
}

vita2d_texture *decode_thumbnail(const std::string &bytes, unsigned width, unsigned height, std::string &error)
{
    auto *source = decode_texture(bytes, error);
    if (!source)
        return nullptr;
    const auto source_width = vita2d_texture_get_width(source), source_height = vita2d_texture_get_height(source);
    if (!width || !height)
    {
        vita2d_free_texture(source);
        error = "Invalid thumbnail dimensions.";
        return nullptr;
    }
    if (source_width <= width && source_height <= height)
        return source;
    auto *result = vita2d_create_empty_texture(width, height);
    if (!result)
    {
        vita2d_free_texture(source);
        error = "Not enough graphics memory for inline image.";
        return nullptr;
    }
    const auto *input = static_cast<const std::uint32_t *>(vita2d_texture_get_datap(source));
    auto *output = static_cast<std::uint32_t *>(vita2d_texture_get_datap(result));
    const auto input_stride = vita2d_texture_get_stride(source) / 4, output_stride = vita2d_texture_get_stride(result) / 4;
    for (unsigned y = 0; y < height; ++y)
        for (unsigned x = 0; x < width; ++x)
            output[y * output_stride + x] = input[(static_cast<std::uint64_t>(y) * source_height / height) * input_stride +
                                                  static_cast<std::uint64_t>(x) * source_width / width];
    vita2d_free_texture(source);
    vita2d_texture_set_filters(result, SCE_GXM_TEXTURE_FILTER_LINEAR, SCE_GXM_TEXTURE_FILTER_LINEAR);
    return result;
}