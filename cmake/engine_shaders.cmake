# Embedded shaders via bgfx::shaderc (uses _bgfx_* helpers from bgfxToolUtils.cmake).
# Apple: skip spirv for --platform osx (shaderc currently fails on SPIRV path for these sources).
# The skinned shader needs more uniform space than ES2 allows, so it skips 100_es.

function(engine_add_triangle_shaders target)
	set(varying_def "${CMAKE_SOURCE_DIR}/shaders/varying.def.sc")
	set(out_dir "${CMAKE_BINARY_DIR}/generated/shaders")

	if(APPLE)
		set(platform_i OSX)
		set(profiles_lit metal 120)
		set(profiles_skinned metal 120)
	elseif(UNIX)
		set(platform_i LINUX)
		set(profiles_lit spirv 120 100_es)
		set(profiles_skinned spirv 120)
	elseif(WIN32)
		set(platform_i WINDOWS)
		set(profiles_lit spirv 120 100_es s_5_0 s_6_0)
		set(profiles_skinned spirv 120 s_5_0 s_6_0)
	else()
		message(FATAL_ERROR "Unsupported platform for engine_add_triangle_shaders")
	endif()

	set(all_outputs "")

	foreach(shader_path IN ITEMS
		"${CMAKE_SOURCE_DIR}/shaders/vs_triangle.sc:lit"
		"${CMAKE_SOURCE_DIR}/shaders/fs_triangle.sc:lit"
		"${CMAKE_SOURCE_DIR}/shaders/vs_skinned.sc:skinned"
		"${CMAKE_SOURCE_DIR}/shaders/fs_skinned.sc:skinned"
		"${CMAKE_SOURCE_DIR}/shaders/vs_debug.sc:lit"
		"${CMAKE_SOURCE_DIR}/shaders/fs_debug.sc:lit"
	)
		string(REPLACE ":" ";" shader_pair "${shader_path}")
		list(GET shader_pair 0 shader_file)
		list(GET shader_pair 1 shader_kind)

		get_filename_component(shader_basename "${shader_file}" NAME)
		get_filename_component(shader_we "${shader_file}" NAME_WE)

		if(shader_we MATCHES "^vs_")
			set(stype VERTEX)
		else()
			set(stype FRAGMENT)
		endif()

		if(shader_kind STREQUAL "skinned")
			set(profiles ${profiles_skinned})
		else()
			set(profiles ${profiles_lit})
		endif()

		foreach(profile IN LISTS profiles)
			_bgfx_get_profile_path_ext(${profile} profile_path_ext)
			_bgfx_get_profile_ext(${profile} profile_ext)

			set(output "${out_dir}/${profile_path_ext}/${shader_basename}.bin.h")
			set(bin2c_name "${shader_we}_${profile_ext}")

			_bgfx_shaderc_parse(
				cli
				BIN2C ${bin2c_name}
				${stype} ${platform_i}
				WERROR
				FILE "${shader_file}"
				OUTPUT "${output}"
				PROFILE ${profile}
				O 3
				VARYINGDEF "${varying_def}"
				INCLUDES "${BGFX_DIR}/src"
			)

			add_custom_command(
				OUTPUT "${output}"
				COMMAND ${CMAKE_COMMAND} -E make_directory "${out_dir}/${profile_path_ext}"
				COMMAND bgfx::shaderc ${cli}
				MAIN_DEPENDENCY "${shader_file}"
				DEPENDS "${varying_def}"
				VERBATIM
			)
			list(APPEND all_outputs "${output}")
		endforeach()
	endforeach()

	target_sources(${target} PRIVATE ${all_outputs})
endfunction()
