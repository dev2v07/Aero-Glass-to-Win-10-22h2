#pragma once

#include "pch.h"

namespace OpenGlass
{
	[[nodiscard]] wxBitmap LoadFigmaIcon(const std::filesystem::path& path, int size = 22, const wxColour& tint = wxNullColour);
}
