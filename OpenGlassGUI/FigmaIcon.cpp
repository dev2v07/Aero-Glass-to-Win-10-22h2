#include "pch.h"
#include "FigmaIcon.hpp"

#define NANOSVG_IMPLEMENTATION
#include <nanosvg/nanosvg.h>
#define NANOSVGRAST_IMPLEMENTATION
#include <nanosvg/nanosvgrast.h>

namespace OpenGlass
{
	wxBitmap LoadFigmaIcon(const std::filesystem::path& path, int size, const wxColour& tint)
	{
		const auto utf8Path = path.u8string();
		NSVGimage* image = nsvgParseFromFile(reinterpret_cast<const char*>(utf8Path.c_str()), "px", 96.0f);
		if (!image || image->width <= 0.0f || image->height <= 0.0f)
		{
			if (image) nsvgDelete(image);
			return {};
		}

		const float scale = static_cast<float>(size) / std::max(image->width, image->height);
		std::vector<unsigned char> rgba(static_cast<size_t>(size) * size * 4);
		NSVGrasterizer* rasterizer = nsvgCreateRasterizer();
		if (!rasterizer)
		{
			nsvgDelete(image);
			return {};
		}
		nsvgRasterize(rasterizer, image, 0.0f, 0.0f, scale, rgba.data(), size, size, size * 4);
		nsvgDeleteRasterizer(rasterizer);
		if (tint.IsOk())
		{
			for (size_t pixel = 0; pixel < static_cast<size_t>(size) * size; ++pixel)
			{
				if (rgba[pixel * 4 + 3] == 0) continue;
				rgba[pixel * 4 + 0] = tint.Red();
				rgba[pixel * 4 + 1] = tint.Green();
				rgba[pixel * 4 + 2] = tint.Blue();
			}
		}

		wxImage wxImageBuffer(size, size);
		unsigned char* rgb = wxImageBuffer.GetData();
		unsigned char* alpha = new unsigned char[static_cast<size_t>(size) * size];
		for (size_t pixel = 0; pixel < static_cast<size_t>(size) * size; ++pixel)
		{
			rgb[pixel * 3 + 0] = rgba[pixel * 4 + 0];
			rgb[pixel * 3 + 1] = rgba[pixel * 4 + 1];
			rgb[pixel * 3 + 2] = rgba[pixel * 4 + 2];
			alpha[pixel] = rgba[pixel * 4 + 3];
		}
		wxImageBuffer.SetAlpha(alpha);
		nsvgDelete(image);
		return wxBitmap(wxImageBuffer);
	}
}
