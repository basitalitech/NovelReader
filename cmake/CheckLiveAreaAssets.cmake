# PNG requirements from https://github.com/hammerill/livearea-specs
# Can also be run directly with cmake -P cmake/CheckLiveAreaAssets.cmake.
function(check_livearea_png relative_path dimensions allow_alpha palette_size)
    set(asset "${CMAKE_CURRENT_LIST_DIR}/../${relative_path}")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${asset}")
    file(READ "${asset}" png HEX)
    string(SUBSTRING "${png}" 0 16 signature)
    string(SUBSTRING "${png}" 24 34 header)
    if(NOT signature STREQUAL "89504e470d0a1a0a" OR
       NOT header STREQUAL "49484452${dimensions}0803000000")
        message(FATAL_ERROR "${relative_path}: expected the specified dimensions, 8-bit indexed color, and no interlacing")
    endif()
    # Read chunk boundaries so palette data cannot be mistaken for a chunk tag.
    string(LENGTH "${png}" png_length)
    set(offset 16)
    set(found_palette FALSE)
    while(offset LESS png_length)
        string(SUBSTRING "${png}" ${offset} 8 chunk_length_hex)
        math(EXPR chunk_length "0x${chunk_length_hex}")
        math(EXPR type_offset "${offset} + 8")
        string(SUBSTRING "${png}" ${type_offset} 8 chunk_type)
        if(chunk_type STREQUAL "504c5445") # PLTE
            set(found_palette TRUE)
            if(palette_size AND NOT chunk_length EQUAL palette_size)
                message(FATAL_ERROR "${relative_path}: requires exactly 256 palette entries")
            endif()
        elseif(chunk_type STREQUAL "74524e53" AND NOT allow_alpha) # tRNS
            message(FATAL_ERROR "${relative_path}: transparency is only supported for startup.png")
        elseif(chunk_type STREQUAL "49454e44") # IEND
            break()
        endif()
        math(EXPR offset "${offset} + 24 + ${chunk_length} * 2")
    endwhile()
    if(NOT found_palette)
        message(FATAL_ERROR "${relative_path}: missing PNG palette")
    endif()
endfunction()

check_livearea_png(sce_sys/icon0.png "0000008000000080" FALSE 0)
check_livearea_png(sce_sys/pic0.png "000003c000000220" FALSE 768)
check_livearea_png(sce_sys/livearea/contents/bg0.png "00000348000001f4" FALSE 0)
check_livearea_png(sce_sys/livearea/contents/startup.png "000001180000009e" TRUE 0)
