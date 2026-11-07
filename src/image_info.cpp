#include "reader.hpp"
#include <cstring>

namespace reader {
bool inspect_image(const std::string& bytes, ImageInfo& image, std::string& error) {
    error.clear();
    const auto* data = reinterpret_cast<const unsigned char*>(bytes.data());
    const auto size = bytes.size();
    if (size > max_image_bytes) { error = "Image exceeds the 8 MiB limit."; return false; }
    ImageInfo result;
    if (size >= 33 && std::memcmp(data, "\x89PNG\r\n\x1a\n", 8) == 0 &&
        data[8] == 0 && data[9] == 0 && data[10] == 0 && data[11] == 13 && std::memcmp(data + 12, "IHDR", 4) == 0) {
        const auto big = [&](std::size_t at) {
            return (static_cast<unsigned>(data[at]) << 24) | (static_cast<unsigned>(data[at + 1]) << 16) |
                   (static_cast<unsigned>(data[at + 2]) << 8) | data[at + 3];
        };
        result = {ImageFormat::Png, big(16), big(20)};
    } else if (size >= 4 && data[0] == 0xff && data[1] == 0xd8) {
        result.format = ImageFormat::Jpeg;
        std::size_t at = 2;
        while (at < size) {
            if (data[at++] != 0xff) break;
            while (at < size && data[at] == 0xff) ++at;
            if (at >= size) break;
            const auto marker = data[at++];
            if (marker == 0xda || marker == 0xd9) break;
            if (marker == 0x01 || (marker >= 0xd0 && marker <= 0xd7)) continue;
            if (size - at < 2) break;
            const auto length = static_cast<std::size_t>(data[at] * 256 + data[at + 1]);
            if (length < 2 || length > size - at) break;
            if (marker == 0xc0 || marker == 0xc1 || marker == 0xc2) {
                if (length < 8) break;
                result.width = data[at + 5] * 256 + data[at + 6];
                result.height = data[at + 3] * 256 + data[at + 4];
                break;
            }
            at += length;
        }
    } else { error = "Only PNG and JPEG illustrations are supported."; return false; }
    if (!result.width || !result.height) { error = "Image header is damaged or unsupported."; return false; }
    if (result.width > 4096 || result.height > 4096 ||
        static_cast<std::uint64_t>(result.width) * result.height > 4 * 1024 * 1024) {
        error = "Image exceeds 4096 pixels per side or 4 megapixels."; return false;
    }
    image = result;
    return true;
}
} // namespace reader
