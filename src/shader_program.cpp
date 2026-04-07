#include "shader_program.hpp"

#include <bgfx/embedded_shader.h>
#include <bx/platform.h>

#if BX_PLATFORM_OSX
#include <essl/fs_triangle.sc.bin.h>
#include <essl/vs_triangle.sc.bin.h>
#include <glsl/fs_triangle.sc.bin.h>
#include <glsl/vs_triangle.sc.bin.h>
#include <metal/fs_triangle.sc.bin.h>
#include <metal/vs_triangle.sc.bin.h>
#elif BX_PLATFORM_LINUX || BX_PLATFORM_BSD
#include <essl/fs_triangle.sc.bin.h>
#include <essl/vs_triangle.sc.bin.h>
#include <glsl/fs_triangle.sc.bin.h>
#include <glsl/vs_triangle.sc.bin.h>
#include <spirv/fs_triangle.sc.bin.h>
#include <spirv/vs_triangle.sc.bin.h>
#elif BX_PLATFORM_WINDOWS
#include <dxbc/fs_triangle.sc.bin.h>
#include <dxbc/vs_triangle.sc.bin.h>
#include <dxil/fs_triangle.sc.bin.h>
#include <dxil/vs_triangle.sc.bin.h>
#include <essl/fs_triangle.sc.bin.h>
#include <essl/vs_triangle.sc.bin.h>
#include <glsl/fs_triangle.sc.bin.h>
#include <glsl/vs_triangle.sc.bin.h>
#include <spirv/fs_triangle.sc.bin.h>
#include <spirv/vs_triangle.sc.bin.h>
#else
#error "Add shader embedded includes for this platform."
#endif

namespace engine {

namespace {

const bgfx::EmbeddedShader k_triangle_shaders[] = {
	BGFX_EMBEDDED_SHADER(vs_triangle),
	BGFX_EMBEDDED_SHADER(fs_triangle),
	BGFX_EMBEDDED_SHADER_END(),
};

} // namespace

bgfx::ProgramHandle load_triangle_program()
{
	const bgfx::RendererType::Enum renderer = bgfx::getRendererType();
	bgfx::ShaderHandle vsh = bgfx::createEmbeddedShader(k_triangle_shaders, renderer, "vs_triangle");
	bgfx::ShaderHandle fsh = bgfx::createEmbeddedShader(k_triangle_shaders, renderer, "fs_triangle");
	return bgfx::createProgram(vsh, fsh, true);
}

} // namespace engine
