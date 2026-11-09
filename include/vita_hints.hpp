#pragma once
#include "vita_ui.hpp"
#include "vita_fonts.hpp"

namespace reader { namespace visual {
// x is the icon's left edge; center_y is shared by icon and label.
// max_width includes both. Labels are clipped rather than wrapped in the bar.
void controller_hint(const VitaFonts& fonts, ControllerButton button,
    const std::string& label, int x, int center_y, int max_width, unsigned tint=text);
} }
