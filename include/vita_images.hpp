#pragma once
#include <string>
#include <vita2d.h>

// Returns one caller-owned RGBA GPU texture. Decoders receive bounded buffers.
vita2d_texture* decode_texture(const std::string& bytes, std::string& error);
