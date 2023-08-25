cmake_minimum_required(VERSION 3.13)
include (pico_sdk_import.cmake)
project(pico_usb_midi_filter)

pico_sdk_init()

# make sure required dependencies exist in the source tree
if ((NOT EXISTS ${PICO_TINYUSB_PATH}/lib/Pico-PIO-USB))
    execute_process(COMMAND python3 tools/get_deps.py rp2040
    WORKING_DIRECTORY ${PICO_TINYUSB_PATH})
endif()

set(target_name midi_app)
add_executable(${target_name})

add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/lib/usb_midi_host)
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/lib/usb_midi_dev_ac_optional)
pico_enable_stdio_uart(${target_name} 1) 

target_sources(${target_name} PRIVATE
 midi_app.c
 usb_descriptors.c
 keylab_essential_mc_filter.c
 midi_mc_fader_pickup.c
 )
target_link_options(${target_name} PRIVATE -Xlinker --print-memory-usage)
target_compile_options(${target_name} PRIVATE -Wall -Wextra)
target_compile_definitions(${target_name} PRIVATE
PICO_DEFAULT_UART_TX_PIN=16
PICO_DEFAULT_UART_RX_PIN=17
PICO_DEFAULT_PIO_USB_DP_PIN=0
)
target_include_directories(${target_name} PRIVATE ${PICO_PIO_USB_SRC} ${CMAKE_CURRENT_LIST_DIR})

target_link_libraries(${target_name} PRIVATE pico_stdlib pico_multicore hardware_pio hardware_dma tinyusb_board tinyusb_device
    tinyusb_host tinyusb_pico_pio_usb usb_midi_host_app_driver usb_midi_device_app_driver)
pico_add_extra_outputs(${target_name})

