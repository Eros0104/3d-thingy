#pragma once

#include <bgfx/bgfx.h>

namespace engine {

bgfx::ProgramHandle load_triangle_program();
bgfx::ProgramHandle load_skinned_program();
bgfx::ProgramHandle load_debug_program();

}
