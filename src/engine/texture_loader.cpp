#include "engine/texture_loader.hpp"

#include <bimg/decode.h>
#include <bx/allocator.h>

#include <cstdio>
#include <fstream>
#include <vector>

namespace engine {

bgfx::TextureHandle load_texture_from_file(const char* path, uint64_t flags)
{
	std::ifstream file(path, std::ios::binary | std::ios::ate);
	if (!file) {
		std::fprintf(stderr, "texture: cannot open \"%s\"\n", path);
		return BGFX_INVALID_HANDLE;
	}
	const std::streamsize size = file.tellg();
	if (size <= 0) {
		std::fprintf(stderr, "texture: empty or bad size \"%s\"\n", path);
		return BGFX_INVALID_HANDLE;
	}
	file.seekg(0, std::ios::beg);
	std::vector<uint8_t> data(static_cast<size_t>(size));
	if (!file.read(reinterpret_cast<char*>(data.data()), size)) {
		std::fprintf(stderr, "texture: read failed \"%s\"\n", path);
		return BGFX_INVALID_HANDLE;
	}

	bx::DefaultAllocator allocator;
	bimg::ImageContainer* image = bimg::imageParse(
		&allocator,
		data.data(),
		static_cast<uint32_t>(data.size())
	);
	if (!image) {
		std::fprintf(stderr, "texture: decode failed \"%s\"\n", path);
		return BGFX_INVALID_HANDLE;
	}

	if (image->m_cubeMap || image->m_depth > 1) {
		bimg::imageFree(image);
		std::fprintf(stderr, "texture: expected 2D image \"%s\"\n", path);
		return BGFX_INVALID_HANDLE;
	}

	const bgfx::Memory* mem = bgfx::copy(image->m_data, image->m_size);
	bimg::imageFree(image);

	bgfx::TextureHandle handle = bgfx::createTexture2D(
		static_cast<uint16_t>(image->m_width),
		static_cast<uint16_t>(image->m_height),
		image->m_numMips > 1,
		image->m_numLayers,
		static_cast<bgfx::TextureFormat::Enum>(image->m_format),
		flags,
		mem
	);

	if (bgfx::isValid(handle)) {
		bgfx::setName(handle, path);
	}
	return handle;
}

} // namespace engine
