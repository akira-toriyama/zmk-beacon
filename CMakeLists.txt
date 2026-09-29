# Copyright (c) 2026 akira-toriyama
# SPDX-License-Identifier: MIT
#
# Zephyr does not read a shield's own CMakeLists.txt, so the shield's sources
# are added from the module root, guarded by the shield symbol.

if(CONFIG_SHIELD_PROSPECTOR)
  target_sources(app PRIVATE src/backlight_init.c src/status_observer.c)
  target_sources_ifdef(CONFIG_ZMK_DISPLAY_STATUS_SCREEN_CUSTOM app PRIVATE src/prospector_screen.c src/hp_bar.c)
endif()

target_sources_ifdef(CONFIG_BEACON_BOOTLOADER_ON_1200_BAUD app PRIVATE src/bootloader_on_1200_baud.c)
target_sources_ifdef(CONFIG_BEACON_SCREEN_DUMP app PRIVATE src/screen_dump.c)
target_sources_ifdef(CONFIG_BEACON_STATUS_BROADCAST app PRIVATE src/status_broadcaster.c)

# The sprite GIF is embedded from CONFIG_BEACON_SPRITE_GIF into the build tree
# only (never next to the sources: the file is personal and stays out of git).
# The file checks reject up front what gifdec would reject at run time
# (gifdec.c gif_open(): "GIF89a", a global colour table, a zero size). The
# GIF's width and height go to the sources as BEACON_SPRITE_GIF_W/_H, and
# prospector_screen.c asserts that they fit the sprite box it lays out.
#
# The LVGL pool check is here, not a BUILD_ASSERT, because CMake can name the
# pool size that would fit. The decoder's one allocation, kept for the whole
# run, is sizeof(gd_GIF) (under 2 KiB), a 4-byte canvas pixel and a 1-byte
# frame index per GIF pixel, and a 16 KiB LZW cache (gifdec.c gif_open(),
# LV_GIF_CACHE_DECODE_DATA); the sprite's boot log line reports it as
# measured. The reserve is for the screen's widgets and the draw tasks: on
# hardware 2026-09-27 the pool peaked at 63,376 B with a 58,515 B decoder
# allocation, so the screen took under 5 KB.
if(CONFIG_BEACON_SPRITE)
  set(sprite_gif "${CONFIG_BEACON_SPRITE_GIF}")
  set(sprite_gd_gif_bytes 2048)
  set(sprite_bytes_per_pixel 5)
  set(sprite_lzw_cache_bytes 16384)
  set(sprite_reserve_bytes 8192)

  if(NOT IS_ABSOLUTE "${sprite_gif}")
    message(FATAL_ERROR "CONFIG_BEACON_SPRITE_GIF must be an absolute path, got '${sprite_gif}'")
  endif()
  if(NOT EXISTS "${sprite_gif}")
    message(FATAL_ERROR "CONFIG_BEACON_SPRITE_GIF: no such file: ${sprite_gif}")
  endif()
  # file(READ) registers no dependency: a changed GIF must re-run these checks.
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${sprite_gif}")

  # Header + logical screen descriptor: "GIF89a", width, height (uint16 LE), packed byte.
  file(READ "${sprite_gif}" sprite_head HEX LIMIT 13)
  string(LENGTH "${sprite_head}" sprite_head_len)
  if(sprite_head_len LESS 26)
    message(FATAL_ERROR "CONFIG_BEACON_SPRITE_GIF: ${sprite_gif} is shorter than a GIF header")
  endif()
  string(SUBSTRING "${sprite_head}" 0 12 sprite_sig)
  if(NOT sprite_sig STREQUAL "474946383961")
    message(FATAL_ERROR "CONFIG_BEACON_SPRITE_GIF: ${sprite_gif} is not a GIF89a file (gifdec opens no other version)")
  endif()
  string(SUBSTRING "${sprite_head}" 12 2 sprite_w_lo)
  string(SUBSTRING "${sprite_head}" 14 2 sprite_w_hi)
  string(SUBSTRING "${sprite_head}" 16 2 sprite_h_lo)
  string(SUBSTRING "${sprite_head}" 18 2 sprite_h_hi)
  string(SUBSTRING "${sprite_head}" 20 2 sprite_packed)
  math(EXPR sprite_w "0x${sprite_w_hi}${sprite_w_lo}")
  math(EXPR sprite_h "0x${sprite_h_hi}${sprite_h_lo}")
  math(EXPR sprite_has_gct "0x${sprite_packed} & 0x80")
  if(sprite_has_gct EQUAL 0)
    message(FATAL_ERROR "CONFIG_BEACON_SPRITE_GIF: ${sprite_gif} has no global colour table (gifdec needs one)")
  endif()
  if(sprite_w EQUAL 0 OR sprite_h EQUAL 0)
    message(FATAL_ERROR "CONFIG_BEACON_SPRITE_GIF: ${sprite_gif} is ${sprite_w}x${sprite_h} (gifdec opens no empty image)")
  endif()
  target_compile_definitions(app PRIVATE BEACON_SPRITE_GIF_W=${sprite_w} BEACON_SPRITE_GIF_H=${sprite_h})

  math(EXPR sprite_need
       "${sprite_gd_gif_bytes} + ${sprite_bytes_per_pixel} * ${sprite_w} * ${sprite_h} + ${sprite_lzw_cache_bytes} + ${sprite_reserve_bytes}")
  if(sprite_need GREATER CONFIG_LV_Z_MEM_POOL_SIZE)
    math(EXPR sprite_pool_fit "(${sprite_need} + 4095) / 4096 * 4096")
    message(FATAL_ERROR
            "CONFIG_BEACON_SPRITE_GIF: ${sprite_gif} (${sprite_w}x${sprite_h}) needs about ${sprite_need} bytes "
            "of the LVGL pool (decoder + screen reserve), CONFIG_LV_Z_MEM_POOL_SIZE is ${CONFIG_LV_Z_MEM_POOL_SIZE}; "
            "CONFIG_LV_Z_MEM_POOL_SIZE=${sprite_pool_fit} would fit, if RAM allows")
  endif()
  message(STATUS "Sprite: ${sprite_gif} (${sprite_w}x${sprite_h}), about ${sprite_need} of ${CONFIG_LV_Z_MEM_POOL_SIZE} pool bytes")

  set(sprite_inc "${CMAKE_CURRENT_BINARY_DIR}/beacon_sprite_gif.inc")
  generate_inc_file_for_target(app "${sprite_gif}" "${sprite_inc}")
  target_include_directories(app PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
  target_sources(app PRIVATE src/sprite.c)
endif()
