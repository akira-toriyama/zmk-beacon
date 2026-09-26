# Copyright (c) 2026 akira-toriyama
# SPDX-License-Identifier: MIT
#
# Zephyr does not read a shield's own CMakeLists.txt, so the shield's sources
# are added from the module root, guarded by the shield symbol.

if(CONFIG_SHIELD_PROSPECTOR)
  target_sources(app PRIVATE src/backlight_init.c)
  target_sources_ifdef(CONFIG_ZMK_DISPLAY_STATUS_SCREEN_CUSTOM app PRIVATE src/prospector_screen.c)
endif()

target_sources_ifdef(CONFIG_BEACON_BOOTLOADER_ON_1200_BAUD app PRIVATE src/bootloader_on_1200_baud.c)
