# Layer relay display for the peripheral (right) half
if(CONFIG_ZMK_LAYER_RELAY_DISPLAY)
  zephyr_library()
  zephyr_library_include_directories(${APPLICATION_SOURCE_DIR}/include)
  zephyr_library_include_directories(${APPLICATION_SOURCE_DIR}/boards/shields/nice_view/widgets)
  zephyr_library_sources(src/layer_relay_display.c)
  zephyr_library_sources(${APPLICATION_SOURCE_DIR}/boards/shields/nice_view/widgets/util.c)
  zephyr_library_sources(${APPLICATION_SOURCE_DIR}/boards/shields/nice_view/widgets/bolt.c)
endif()
