idf_component_register(
    # Time-window history and uniform time sampling for the serial chart.
    SRCS "services/serial_plot.c"
         "main.c" "services/serial_frame.c" "services/serial_history.c" "services/serial_uart.c" "services/serial_ble.c" "apps/serial/app_serial.c" "apps/serial/serial_status_font.c" "services/app_services.c" "services/keyboard_flow.c" "services/mouse_report.c" "services/keymouse_service.c" "apps/keymouse/app_keymouse.c" "services/ble_mouse.c" "services/mouse_flow.c" "app_registry.c" "services/usb_app_mode.c" "services/resource_store.c"
         "services/xiaozhi_protocol.c" "services/xiaozhi_service.c" "apps/xiaozhi/app_xiaozhi.c"
         "platform/app.c" "shell/shell.c" "shell/desktop.c" "shell/ui.c" "shell/font_ui.c"
         "services/mic_meter.c" "apps/microphone/app_microphone.c" "services/music_service.c" "apps/music/app_music.c" "services/settings.c" "services/usb_service.c"
         "services/wifi_policy.c" "services/wifi_store.c" "services/wifi_service.c"
         "apps/keysim/app_keysim.c" "apps/counter/app_counter.c"
         "apps/settings/app_settings.c" "apps/system_info/app_system_info.c"
         "apps/wifi/app_wifi.c"
         "apps/weather/app_weather.c" "apps/weather/weather_service.c"
         "apps/weather/weather_data.c" "apps/weather/weather_calendar.c"
         "apps/weather/weather_style.c" "apps/weather/weather_preferences.c" "apps/weather/weather_number_fonts.c"
         "apps/weather/weather_fonts.c" "apps/weather/weather_icons.c" "apps/weather/weather_art.c"
    INCLUDE_DIRS "." "platform" "shell" "services" "apps/weather" "apps/serial"
    REQUIRES esp_driver_uart bt usb_host_hid board lvgl esp_timer esp_tinyusb nvs_flash app_update esp_psram esp_wifi esp_netif esp_event
             esp_http_client esp_websocket_client mqtt esp_audio_codec esp-tls mbedtls json usb usb_host_uac fatfs music_codec esp_partition)
idf_component_get_property(lvgl_lib lvgl COMPONENT_LIB)
target_include_directories(${lvgl_lib} PRIVATE "${CMAKE_CURRENT_LIST_DIR}/shell")
set_source_files_properties("services/music_service.c" PROPERTIES COMPILE_OPTIONS "-O2")

if(NOT CMAKE_BUILD_EARLY_EXPANSION)
    set(resource_pack "${CMAKE_CURRENT_LIST_DIR}/../tools/pack_resources.py")
    file(GLOB resource_inputs CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/../assets/resource-data/*")
    add_custom_command(
        OUTPUT "${CMAKE_BINARY_DIR}/resources.bin" "${CMAKE_BINARY_DIR}/generated/resource_manifest.h"
        COMMAND "${PYTHON}" "${resource_pack}" --output "${CMAKE_BINARY_DIR}"
        DEPENDS "${resource_pack}" ${resource_inputs}
        VERBATIM)
    add_custom_target(touchdesk_resources DEPENDS "${CMAKE_BINARY_DIR}/resources.bin" "${CMAKE_BINARY_DIR}/generated/resource_manifest.h")
    add_dependencies(${COMPONENT_LIB} touchdesk_resources)
    target_include_directories(${COMPONENT_LIB} PRIVATE "${CMAKE_BINARY_DIR}/generated")
    esptool_py_flash_to_partition(flash "assets" "${CMAKE_BINARY_DIR}/resources.bin")
    add_dependencies(flash touchdesk_resources)
endif()
