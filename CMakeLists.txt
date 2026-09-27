# Copyright (c) 2026 akira-toriyama
# SPDX-License-Identifier: MIT
#
# Zephyr does not read a shield's own CMakeLists.txt, so the shield's sources
# are added from the module root, guarded by the shield symbol.

if(CONFIG_SHIELD_PROSPECTOR)
  target_sources(app PRIVATE src/backlight_init.c src/status_observer.c)
  target_sources_ifdef(CONFIG_ZMK_DISPLAY_STATUS_SCREEN_CUSTOM app PRIVATE src/prospector_screen.c)
endif()

target_sources_ifdef(CONFIG_BEACON_BOOTLOADER_ON_1200_BAUD app PRIVATE src/bootloader_on_1200_baud.c)
target_sources_ifdef(CONFIG_BEACON_STATUS_BROADCAST app PRIVATE src/status_broadcaster.c)

# The sprite GIF is embedded from CONFIG_BEACON_SPRITE_GIF into the build tree
# only (never next to the sources: the file is personal and stays out of git).
# The checks below reject up front what gifdec would reject at run time
# (gifdec.c gif_open(): "GIF89a" and a global colour table) and what would not
# fit the LVGL pool: its one allocation is sizeof(gd_GIF) + 5 * w * h + a 16 KiB
# LZW cache (LV_GIF_CACHE_DECODE_DATA), kept for the whole run. The reserve is
# for the battery screen's widgets and the draw tasks: on hardware 2026-09-27
# the pool peaked at 63,376 B with a 58,515 B decoder allocation, so the screen
# took under 5 KB. The size limits are the panel width and the box above the
# digits that prospector_screen.c computes at run time (240 - 12 margin - 9
# base line - 34 digit height = 185 rows with Montserrat 48); change them
# together.
if(CONFIG_BEACON_SPRITE)
  set(sprite_gif "${CONFIG_BEACON_SPRITE_GIF}")
  set(sprite_gd_gif_bytes 2048)
  set(sprite_lzw_cache_bytes 16384)
  set(sprite_reserve_bytes 8192)
  set(sprite_max_w 280)
  set(sprite_max_h 185)

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
  if(sprite_w EQUAL 0 OR sprite_h EQUAL 0 OR sprite_w GREATER sprite_max_w OR sprite_h GREATER sprite_max_h)
    message(FATAL_ERROR
            "CONFIG_BEACON_SPRITE_GIF: ${sprite_gif} is ${sprite_w}x${sprite_h}; the sprite box above the "
            "digits is ${sprite_max_w}x${sprite_max_h}")
  endif()

  math(EXPR sprite_need
       "${sprite_gd_gif_bytes} + 5 * ${sprite_w} * ${sprite_h} + ${sprite_lzw_cache_bytes} + ${sprite_reserve_bytes}")
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
