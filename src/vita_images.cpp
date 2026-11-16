#include "vita_images.hpp"
#include "reader.hpp"
#include <png.h>
#include <turbojpeg.h>
#include <algorithm>
#include <cstring>
#include <memory>

namespace
{
    struct Surface
    {
        unsigned char *pixels;
        int stride;
    };

    struct PngReader
    {
        png_image image{};
        PngReader() { image.version = PNG_IMAGE_VERSION; }
        ~PngReader() { png_image_free(&image); }
    };

    template <typename Allocate>
    bool decode_rgba(const std::string &bytes, std::string &error, Allocate allocate)
    {
        reader::ImageInfo info;
        if (!reader::inspect_image(bytes, info, error))
            return false;
        if (info.format == reader::ImageFormat::Png)
        {
            PngReader png;
            if (!png_image_begin_read_from_memory(&png.image, bytes.data(), bytes.size()))
            {
                error = "Could not read PNG image.";
                return false;
            }
            if (png.image.width != info.width || png.image.height != info.height)
            {
                error = "PNG dimensions are inconsistent.";
                return false;
            }
            const auto target = allocate(info.width, info.height);
            if (!target.pixels)
                return false;
            png.image.format = PNG_FORMAT_RGBA;
            if (png_image_finish_read(&png.image, nullptr, target.pixels, target.stride, nullptr))
                return true;
            error = "PNG image is damaged.";
            return false;
        }

        const std::unique_ptr<void, decltype(&tjDestroy)> decoder(tjInitDecompress(), tjDestroy);
        if (!decoder)
        {
            error = "Could not initialize JPEG decoder.";
            return false;
        }
        int width = 0, height = 0, subsampling = 0, colorspace = 0;
        const auto *buffer = reinterpret_cast<const unsigned char *>(bytes.data());
        if (tjDecompressHeader3(decoder.get(), buffer, bytes.size(), &width, &height, &subsampling, &colorspace) < 0 ||
            width != static_cast<int>(info.width) || height != static_cast<int>(info.height))
        {
            error = "JPEG header is damaged.";
            return false;
        }
        const auto target = allocate(info.width, info.height);
        if (!target.pixels)
            return false;
        if (tjDecompress2(decoder.get(), buffer, bytes.size(), target.pixels, width, target.stride, height, TJPF_RGBA, TJFLAG_STOPONWARNING) >= 0)
            return true;
        error = "JPEG is damaged or uses unsupported colors.";
        return false;
    }

    Surface surface(vita2d_texture *texture)
    {
        return texture ? Surface{static_cast<unsigned char *>(vita2d_texture_get_datap(texture)),
                                 static_cast<int>(vita2d_texture_get_stride(texture))}
                       : Surface{nullptr, 0};
    }

    void scale_rgba(Surface source, unsigned source_width, unsigned source_height,
                    Surface target, unsigned width, unsigned height)
    {
        for (unsigned y = 0; y < height; ++y)
        {
            const auto *in = source.pixels + (static_cast<std::size_t>(y) * source_height / height) * source.stride;
            auto *out = target.pixels + y * target.stride;
            for (unsigned x = 0; x < width; ++x)
                std::memcpy(out + x * 4, in + (static_cast<std::size_t>(x) * source_width / width) * 4, 4);
        }
    }

    void use_linear_filter(vita2d_texture *texture)
    {
        vita2d_texture_set_filters(texture, SCE_GXM_TEXTURE_FILTER_LINEAR, SCE_GXM_TEXTURE_FILTER_LINEAR);
    }

    struct PngSource
    {
        const std::string &bytes;
        std::size_t offset = 0;
    };

    void read_png_bytes(png_structp png, png_bytep output, png_size_t count)
    {
        auto &input = *static_cast<PngSource *>(png_get_io_ptr(png));
        if (count > input.bytes.size() - input.offset)
            png_error(png, "Truncated PNG cover");
        std::memcpy(output, input.bytes.data() + input.offset, count);
        input.offset += count;
    }

    struct PngRows
    {
        png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
        png_infop info = png ? png_create_info_struct(png) : nullptr;
        ~PngRows()
        {
            if (png)
                png_destroy_read_struct(&png, &info, nullptr);
        }
    };

    bool decode_png_thumbnail(const std::string &bytes, const reader::ImageInfo &source, ThumbnailPixels &target)
    {
        PngRows reader;
        if (!reader.info)
            return false;
        // libpng can jump back on damaged input. Construct all C++ resource owners first.
        PngSource input{bytes};
        const auto stride = static_cast<int>(source.width * 4);
        std::vector<unsigned char> scratch(stride);
        std::vector<unsigned char> selected(static_cast<std::size_t>(stride) * target.height);
        if (setjmp(png_jmpbuf(reader.png)))
            return false;
        png_set_read_fn(reader.png, &input, read_png_bytes);
        png_read_info(reader.png, reader.info);
        if (png_get_image_width(reader.png, reader.info) != source.width || png_get_image_height(reader.png, reader.info) != source.height)
            return false;
        const auto color = png_get_color_type(reader.png, reader.info);
        const bool alpha = (color & PNG_COLOR_MASK_ALPHA) || png_get_valid(reader.png, reader.info, PNG_INFO_tRNS);
        png_set_expand(reader.png);
        png_set_strip_16(reader.png);
        if (color == PNG_COLOR_TYPE_GRAY || color == PNG_COLOR_TYPE_GRAY_ALPHA)
            png_set_gray_to_rgb(reader.png);
        if (!alpha)
            png_set_filler(reader.png, 0xff, PNG_FILLER_AFTER);
        const auto passes = png_set_interlace_handling(reader.png);
        png_read_update_info(reader.png, reader.info);
        if (png_get_rowbytes(reader.png, reader.info) != static_cast<png_size_t>(stride))
            return false;
        for (int pass = 0; pass < passes; ++pass)
        {
            unsigned row = 0;
            for (unsigned y = 0; y < source.height; ++y)
            {
                auto *output = scratch.data();
                if (row < target.height && row * source.height / target.height == y)
                    output = selected.data() + row * stride;
                png_read_row(reader.png, output, nullptr);
                if (output != scratch.data())
                    while (++row < target.height && row * source.height / target.height == y)
                        std::memcpy(selected.data() + row * stride, output, stride);
            }
        }
        png_read_end(reader.png, nullptr);
        scale_rgba({selected.data(), stride}, source.width, target.height,
                   {target.rgba.data(), static_cast<int>(target.width * 4)}, target.width, target.height);
        return true;
    }

    bool decode_jpeg_thumbnail(const std::string &bytes, const reader::ImageInfo &source, ThumbnailPixels &target)
    {
        const std::unique_ptr<void, decltype(&tjDestroy)> decoder(tjInitDecompress(), tjDestroy);
        if (!decoder)
            return false;
        int width = 0, height = 0, subsampling = 0, colorspace = 0;
        const auto *buffer = reinterpret_cast<const unsigned char *>(bytes.data());
        if (tjDecompressHeader3(decoder.get(), buffer, bytes.size(), &width, &height, &subsampling, &colorspace) < 0 ||
            width != static_cast<int>(source.width) || height != static_cast<int>(source.height))
            return false;
        const auto required_width = std::min(source.width, target.width), required_height = std::min(source.height, target.height);
        const bool width_limited = required_width * source.height <= required_height * source.width;
        int count = 0, scaled_width = width, scaled_height = height;
        const auto *factors = tjGetScalingFactors(&count);
        for (int i = 0; i < count; ++i)
        {
            const auto sw = TJSCALED(width, factors[i]), sh = TJSCALED(height, factors[i]);
            if ((width_limited ? sw >= static_cast<int>(required_width) : sh >= static_cast<int>(required_height)) &&
                sw * sh < scaled_width * scaled_height)
            {
                scaled_width = sw;
                scaled_height = sh;
            }
        }
        std::vector<unsigned char> scaled(static_cast<std::size_t>(scaled_width) * scaled_height * 4);
        if (tjDecompress2(decoder.get(), buffer, bytes.size(), scaled.data(), scaled_width, scaled_width * 4,
                          scaled_height, TJPF_RGBA, TJFLAG_STOPONWARNING) < 0)
            return false;
        scale_rgba({scaled.data(), scaled_width * 4}, scaled_width, scaled_height,
                   {target.rgba.data(), static_cast<int>(target.width * 4)}, target.width, target.height);
        return true;
    }
}

vita2d_texture *decode_texture(const std::string &bytes, std::string &error)
{
    std::unique_ptr<vita2d_texture, decltype(&vita2d_free_texture)> texture(nullptr, vita2d_free_texture);
    if (!decode_rgba(bytes, error, [&](unsigned width, unsigned height)
                     {
        texture.reset(vita2d_create_empty_texture(width, height));
        if (!texture)
            error = "Not enough graphics memory for this image.";
        return surface(texture.get()); }))
        return nullptr;
    use_linear_filter(texture.get());
    return texture.release();
}

vita2d_texture *decode_thumbnail(const std::string &bytes, unsigned width, unsigned height, std::string &error)
{
    std::unique_ptr<vita2d_texture, decltype(&vita2d_free_texture)> source(decode_texture(bytes, error), vita2d_free_texture);
    if (!source)
        return nullptr;
    if (!width || !height)
    {
        error = "Invalid thumbnail dimensions.";
        return nullptr;
    }
    const auto source_width = vita2d_texture_get_width(source.get()), source_height = vita2d_texture_get_height(source.get());
    if (source_width <= width && source_height <= height)
        return source.release();
    auto *result = vita2d_create_empty_texture(width, height);
    if (!result)
    {
        error = "Not enough graphics memory for inline image.";
        return nullptr;
    }
    scale_rgba(surface(source.get()), source_width, source_height, surface(result), width, height);
    use_linear_filter(result);
    return result;
}

ThumbnailPixels decode_thumbnail_pixels(const std::string &bytes, unsigned width, unsigned height, std::string &error)
{
    if (!width || !height || width > 144 || height > 200)
    {
        error = "Invalid cover thumbnail dimensions.";
        return {};
    }
    reader::ImageInfo info;
    if (!reader::inspect_image(bytes, info, error))
        return {};
    if (info.width <= width && info.height <= height)
    {
        width = info.width;
        height = info.height;
    }
    ThumbnailPixels result{width, height, std::vector<unsigned char>(static_cast<std::size_t>(width) * height * 4)};
    const bool decoded = info.format == reader::ImageFormat::Png ? decode_png_thumbnail(bytes, info, result)
                                                                 : decode_jpeg_thumbnail(bytes, info, result);
    if (!decoded)
    {
        error = "Could not decode cover thumbnail.";
        return {};
    }
    return result;
}

vita2d_texture *upload_thumbnail(const ThumbnailPixels &pixels)
{
    if (pixels.rgba.empty())
        return nullptr;
    auto *texture = vita2d_create_empty_texture(pixels.width, pixels.height);
    if (!texture)
        return nullptr;
    const auto target = surface(texture);
    const auto row = pixels.width * 4;
    for (unsigned y = 0; y < pixels.height; ++y)
        std::memcpy(target.pixels + y * target.stride, pixels.rgba.data() + y * row, row);
    use_linear_filter(texture);
    return texture;
}