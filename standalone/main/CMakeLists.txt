set(PORT_DIR "${CMAKE_CURRENT_LIST_DIR}/../../src/main")
set(ENGINE_DIR "${CMAKE_CURRENT_LIST_DIR}/../../upstream/ClassiCube/src")
file(GLOB ENGINE_SOURCES "${ENGINE_DIR}/*.c")

idf_component_register(
    SRCS "app_main.c" "silt_assets.c" "silt_host.c" "silt_controls.c" "silt_font.c" "silt_rules.c" "silt_world.c" "../survival.c"
         "bsp/bsp_i2c.c" "bsp/bsp_pca9557.c" "bsp/bsp_lcd.c"
         "bsp/bsp_touch.c" "bsp/bsp_sd.c" "bsp/bsp_audio.c"
         "${PORT_DIR}/holo_port.c" "${PORT_DIR}/holo_perf.c"
         "${PORT_DIR}/Platform_Holo.c" "${PORT_DIR}/Window_Holo.c"
         "${PORT_DIR}/Audio_Holo.c" ${ENGINE_SOURCES}
    INCLUDE_DIRS "." "include" "bsp" ".." "${PORT_DIR}" "${ENGINE_DIR}"
    PRIV_REQUIRES esp_driver_gpio esp_driver_spi esp_driver_i2c
                  esp_driver_i2s esp_driver_ledc esp_lcd esp_timer
                  heap vfs fatfs sdmmc esp_driver_sdmmc
)
target_compile_definitions(${COMPONENT_LIB} PRIVATE PLAT_ESP32S3=1 SILTWALK_STANDALONE=1)
target_compile_options(${COMPONENT_LIB} PRIVATE -O2 -fno-strict-aliasing -Wno-error)
target_add_binary_data(${COMPONENT_LIB} "${CMAKE_CURRENT_LIST_DIR}/../assets/default.zip" BINARY)
target_add_binary_data(${COMPONENT_LIB} "${CMAKE_CURRENT_LIST_DIR}/../audio/classicube.zip" BINARY)
target_add_binary_data(${COMPONENT_LIB} "${CMAKE_CURRENT_LIST_DIR}/../audio/bgm.wav" BINARY)

option(SILT_CAPTURE_SCREEN "Export one gameplay framebuffer over serial" OFF)
if(SILT_CAPTURE_SCREEN)
    target_sources(${COMPONENT_LIB} PRIVATE "silt_capture.c")
    set_source_files_properties("${PORT_DIR}/Window_Holo.c" "silt_capture.c"
        PROPERTIES COMPILE_DEFINITIONS SILT_CAPTURE_SCREEN=1)
endif()
