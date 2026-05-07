# Embeds raw texture files as C uint8_t arrays using bgfx::bin2c.
# Usage: engine_embed_texture(<target> <file_path> <array_name>)

function(engine_embed_texture target tex_path array_name)
    set(out_dir "${CMAKE_BINARY_DIR}/generated/textures")
    set(output  "${out_dir}/${array_name}.h")

    add_custom_command(
        OUTPUT  "${output}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${out_dir}"
        COMMAND bgfx::bin2c -f "${tex_path}" -o "${output}" -n "${array_name}"
        MAIN_DEPENDENCY "${tex_path}"
        VERBATIM
    )
    target_sources(${target} PRIVATE "${output}")
    target_include_directories(${target} PRIVATE "${out_dir}")
endfunction()
