# Copyright (c) 2026 akira-toriyama
# SPDX-License-Identifier: MIT
#
# Zephyr does not read a shield's own CMakeLists.txt, so the shield's sources
# are added from the module root, guarded by the shield symbol.

if(CONFIG_SHIELD_PROSPECTOR)
  target_sources(app PRIVATE src/backlight.c src/status_observer.c)
  target_sources_ifdef(CONFIG_ZMK_DISPLAY_STATUS_SCREEN_CUSTOM app PRIVATE src/prospector_screen.c src/hp_bar.c)
  target_sources_ifdef(CONFIG_BEACON_SCREEN_OFF app PRIVATE src/screen_off.c)
endif()

target_sources_ifdef(CONFIG_BEACON_BOOTLOADER_ON_1200_BAUD app PRIVATE src/bootloader_on_1200_baud.c)
target_sources_ifdef(CONFIG_BEACON_SCREEN_DUMP app PRIVATE src/screen_dump.c)
target_sources_ifdef(CONFIG_BEACON_STATUS_BROADCAST app PRIVATE src/status_broadcaster.c)

# The sprite GIF named by CONFIG_BEACON_SPRITE_GIF is converted at configure
# time into a sprite pack (tools/sprite_pack.py, docs/sprite-pack.md) and
# embedded from the build tree only (never next to the sources: the file is
# personal and stays out of git). The converter prints the animation's size,
# from which the LVGL pool check computes what the player keeps for the whole
# run: a packed canvas and the decoded picture 0 (stride x height bytes each);
# the reserve is for the screen's widgets and the draw tasks (the pool peaked
# 4.9 KB above the sprite's block, hardware 2026-09-27 and 2026-10-06). The
# canvas size goes to the sources as BEACON_SPRITE_GIF_W/_H, and
# prospector_screen.c asserts that it fits the sprite box it lays out. The
# name goes into the pack too, cut to what the HP bar shows; no message here
# echoes the path or the name.
if(CONFIG_BEACON_SPRITE)
  set(sprite_gif "${CONFIG_BEACON_SPRITE_GIF}")
  set(sprite_pack_py "${CMAKE_CURRENT_SOURCE_DIR}/tools/sprite_pack.py")
  set(sprite_bin "${CMAKE_CURRENT_BINARY_DIR}/beacon_sprite.spk")
  set(sprite_reserve_bytes 8192)
  set(sprite_name_glyphs 15)

  if(NOT IS_ABSOLUTE "${sprite_gif}")
    message(FATAL_ERROR "CONFIG_BEACON_SPRITE_GIF must be an absolute path")
  endif()
  if(NOT EXISTS "${sprite_gif}")
    message(FATAL_ERROR "CONFIG_BEACON_SPRITE_GIF: no such file")
  endif()
  string(SUBSTRING "${CONFIG_BEACON_SPRITE_NAME}" 0 ${sprite_name_glyphs} sprite_name)
  # execute_process registers no dependency: a changed GIF or converter must re-run it.
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
               "${sprite_gif}" "${sprite_pack_py}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/gifpics.py")
  execute_process(COMMAND ${PYTHON_EXECUTABLE} "${sprite_pack_py}" pack --name "${sprite_name}"
                          -o "${sprite_bin}" "${sprite_gif}"
                  RESULT_VARIABLE sprite_rc OUTPUT_VARIABLE sprite_out ERROR_VARIABLE sprite_err)
  if(NOT sprite_rc EQUAL 0)
    message(FATAL_ERROR "CONFIG_BEACON_SPRITE_GIF: the GIF does not convert to a sprite pack: ${sprite_err}")
  endif()
  string(REGEX MATCH
         "animation 0: ([0-9]+)x([0-9]+) bpp ([0-9]+) colours ([0-9]+) pictures ([0-9]+) frames ([0-9]+) bytes ([0-9]+)"
         sprite_line "${sprite_out}")
  if(sprite_line STREQUAL "")
    message(FATAL_ERROR "CONFIG_BEACON_SPRITE_GIF: the converter printed no size: ${sprite_out}")
  endif()
  set(sprite_w ${CMAKE_MATCH_1})
  set(sprite_h ${CMAKE_MATCH_2})
  set(sprite_bpp ${CMAKE_MATCH_3})
  target_compile_definitions(app PRIVATE BEACON_SPRITE_GIF_W=${sprite_w} BEACON_SPRITE_GIF_H=${sprite_h})

  if(sprite_bpp EQUAL 4)
    math(EXPR sprite_stride "(${sprite_w} + 1) / 2")
  else()
    set(sprite_stride ${sprite_w})
  endif()
  math(EXPR sprite_need "2 * ${sprite_stride} * ${sprite_h} + ${sprite_reserve_bytes}")
  if(sprite_need GREATER CONFIG_LV_Z_MEM_POOL_SIZE)
    math(EXPR sprite_pool_fit "(${sprite_need} + 4095) / 4096 * 4096")
    message(FATAL_ERROR
            "CONFIG_BEACON_SPRITE_GIF: a ${sprite_w}x${sprite_h} sprite at ${sprite_bpp} bits a pixel needs about "
            "${sprite_need} bytes of the LVGL pool (canvas, dictionary, screen reserve), CONFIG_LV_Z_MEM_POOL_SIZE is "
            "${CONFIG_LV_Z_MEM_POOL_SIZE}; CONFIG_LV_Z_MEM_POOL_SIZE=${sprite_pool_fit} would fit, if RAM allows")
  endif()
  message(STATUS "Sprite: ${sprite_line}; about ${sprite_need} of ${CONFIG_LV_Z_MEM_POOL_SIZE} pool bytes")

  set(sprite_inc "${CMAKE_CURRENT_BINARY_DIR}/beacon_sprite_pack.inc")
  generate_inc_file_for_target(app "${sprite_bin}" "${sprite_inc}")
  target_include_directories(app PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
  target_sources(app PRIVATE src/sprite.c src/sprite_pack.c)
endif()
