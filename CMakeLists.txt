cmake_minimum_required(VERSION 3.13)
include (pico_sdk_import.cmake)
project(pico_usb_midi_filter)

pico_sdk_init()

add_executable(pico_usb_midi_filter)

add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/lib/usb_midi_host)
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/lib/usb_midi_dev_ac_optional)
pico_enable_stdio_uart(pico_usb_midi_filter 1) 

target_sources(pico_usb_midi_filter PRIVATE
 midi_app.c
 usb_descriptors.c
 keylab_essential_mc_filter.c
 midi_mc_fader_pickup.c
 )
target_link_options(pico_usb_midi_filter PRIVATE -Xlinker --print-memory-usage)
target_compile_options(pico_usb_midi_filter PRIVATE -Wall -Wextra)
target_compile_definitions(pico_usb_midi_filter PRIVATE
PICO_DEFAULT_UART_TX_PIN=16
PICO_DEFAULT_UART_RX_PIN=17
PICO_DEFAULT_PIO_USB_DP_PIN=0
)
target_include_directories(pico_usb_midi_filter PRIVATE ${PICO_PIO_USB_SRC} ${CMAKE_CURRENT_LIST_DIR})

target_link_libraries(pico_usb_midi_filter PRIVATE pico_stdlib pico_multicore hardware_pio hardware_dma tinyusb_board tinyusb_device
    tinyusb_host tinyusb_pico_pio_usb usb_midi_host_app_driver usb_midi_device_app_driver)
pico_add_extra_outputs(pico_usb_midi_filter)

