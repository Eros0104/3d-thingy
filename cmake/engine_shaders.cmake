# Embedded triangle shaders via bgfx::shaderc (uses _bgfx_* helpers from bgfxToolUtils.cmake).
# Apple: skip spirv for --platform osx (shaderc currently fails on SPIRV path for these sources).

function(engine_add_triangle_shaders target)
	set(varying_def "${CMAKE_SOURCE_DIR}/shaders/varying.def.sc")
	set(out_dir "${CMAKE_BINARY_DIR}/generated/shaders")

	if(APPLE)
		set(platform_i OSX)
		set(profiles metal 120 100_es)
	elseif(UNIX)
		set(platform_i LINUX)
		set(profiles spirv 120 100_es)
	elseif(WIN32)
		set(platform_i WINDOWS)
		set(profiles spirv 120 100_es s_5_0 s_6_0)
	else()
		message(FATAL_ERROR "Unsupported platform for engine_add_triangle_shaders")
	endif()

	set(all_outputs "")

	foreach(shader_path IN ITEMS
		"${CMAKE_SOURCE_DIR}/shaders/vs_triangle.sc"
		"${CMAKE_SOURCE_DIR}/shaders/fs_triangle.sc"
	)
		get_filename_component(shader_basename "${shader_path}" NAME)
		get_filename_component(shader_we "${shader_path}" NAME_WE)

		if(shader_we MATCHES "^vs_")
			set(stype VERTEX)
		else()
			set(stype FRAGMENT)
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
				FILE "${shader_path}"
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
				MAIN_DEPENDENCY "${shader_path}"
				DEPENDS "${varying_def}"
				VERBATIM
			)
			list(APPEND all_outputs "${output}")
		endforeach()
	endforeach()

	target_sources(${target} PRIVATE ${all_outputs})
endfunction()
