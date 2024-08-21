/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2021, Ha Thach (tinyusb.org)
 *               2022 rppicomidi
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 */

/**
 * This program initializes pins GP1 and GP2 for use as a MIDI host port using
 * the Pico-PIO-USB library. It then waits for a USB device to be attached.
 * The software clones the USB descriptor of the attached USB device, and if
 * it is a MIDI device, it will initialized the RP2040 USB port as a MIDI
 * Device with the same descriptor as the attached device. It will transparently
 * pass data between the attached upstream USB host and the downstream USB MIDI
 * device. It will translate certain note messages from one note to another
 * note to allow DAW control buttons messages from an Arturia Keylab Essential 88
 * to work correctly with the Cubase DAW.
 */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "pico/bootrom.h"
#include "hardware/watchdog.h"
#include "hardware/clocks.h"
#include "pio_usb.h"

#include "tusb.h"
#include "bsp/board_api.h"
#include "usb_midi_host.h"
#include "class/midi/midi_device.h"
#include "usb_descriptors.h"
#include "midi_filter.h"
//--------------------------------------------------------------------+
// MACRO TYPEDEF CONSTANT ENUM DECLARATION
//--------------------------------------------------------------------+

//--------------------------------------------------------------------+
// STATIC GLOBALS DECLARATION
//--------------------------------------------------------------------+
static uint8_t midi_dev_addr = 0;

static void poll_midi_dev_rx(bool connected)
{
  // device must be attached and have at least one endpoint ready to receive a message
  if (!connected)
  {
    return;
  }
  uint8_t packet[4];
  while (tud_midi_packet_read(packet))
  {
    if (filter_midi_out(packet))
      tuh_midi_packet_write(midi_dev_addr, packet);
  }
}

static void midi_host_app_task(void)
{
  if (cloning_is_required()) {
    if (midi_dev_addr != 0 && tuh_midi_configured(midi_dev_addr)) {
      TU_LOG2("start descriptor cloning\r\n");
      start_cloning(midi_dev_addr);
    }
  }
  else if (clone_next_string_is_required()) {
    clone_next_string();
  }
  else if (descriptors_are_cloned()) {
    tuh_midi_stream_flush(midi_dev_addr);
  }
}

//--------------------------------------------------------------------+
// TinyUSB Callbacks
//--------------------------------------------------------------------+

// Invoked when device with hid interface is mounted
// Report descriptor is also available for use. tuh_hid_parse_report_descriptor()
// can be used to parse common/simple enough descriptor.
// Note: if report descriptor length > CFG_TUH_ENUMERATION_BUFSIZE, it will be skipped
// therefore report_desc = NULL, desc_len = 0
void tuh_midi_mount_cb(uint8_t dev_addr, uint8_t in_ep, uint8_t out_ep, uint8_t num_cables_rx, uint16_t num_cables_tx)
{
  (void)in_ep;
  (void)out_ep;
  (void)num_cables_rx;
  (void)num_cables_tx;
  TU_LOG1("Attached MIDI device addr=%u, IN EPT=%u has %u cables, OUT EPT=%u has %u cables\r\n",
      dev_addr, in_ep & 0xf, num_cables_rx, out_ep & 0xf, num_cables_tx);

  midi_dev_addr = dev_addr;
  set_cloning_required();
}

// Invoked when device with hid interface is un-mounted
void tuh_midi_umount_cb(uint8_t dev_addr, uint8_t instance)
{
  (void)dev_addr;
  (void)instance;
  midi_dev_addr = 0;
  set_descriptors_uncloned();
  TU_LOG1("MIDI device address = %d, instance = %d is unmounted\r\n", dev_addr, instance);
  watchdog_reboot(0,0,10); // wait 10 ms and then reboot
}

void tuh_midi_rx_cb(uint8_t dev_addr, uint32_t num_packets)
{
  if (midi_dev_addr == dev_addr)
  {
    while (num_packets>0)
    {
      --num_packets;
      uint8_t packet[4];
      while (tuh_midi_packet_read(dev_addr, packet))
      {
        if (filter_midi_in(packet))
          tud_midi_packet_write(packet);
      }
    }
  }
}

void tuh_midi_tx_cb(uint8_t dev_addr)
{
    (void)dev_addr;
}

//--------------------------------------------------------------------+
// MACRO CONSTANT TYPEDEF PROTYPES
//--------------------------------------------------------------------+
static void led_blinking_task(void);
// core1: handle host events
void core1_main() {
  sleep_ms(10);

  // To run USB SOF interrupt in core1, init host stack for pio_usb (roothub
  // port1) on core1
  tuh_init(BOARD_TUH_RHPORT);

  while (true) {
    tuh_task(); // tinyusb host task

    midi_host_app_task();
  }
}
static enum {MIDI_DEVICE_NOT_INITIALIZED, MIDI_DEVICE_NEEDS_INIT, MIDI_DEVICE_IS_INITIALIZED} midi_device_status = MIDI_DEVICE_NOT_INITIALIZED;
void device_clone_complete_cb()
{
  midi_device_status = MIDI_DEVICE_NEEDS_INIT;
}

// core0: handle device events
int main(void) {
  // default 125MHz is not appropreate. Sysclock should be multiple of 12MHz.
  set_sys_clock_khz(120000, true);

  sleep_ms(10);

  // direct printf to UART
  board_init();

  multicore_reset_core1();
  // all USB task run in core1
  multicore_launch_core1(core1_main);

  TU_LOG1("pico-usb-midi-filter\r\n");
  filter_midi_init();
  while (1)
  {
    if (midi_device_status == MIDI_DEVICE_NEEDS_INIT) {
      tud_init(0);
      TU_LOG1("MIDI device initialized\r\n");
      midi_device_status = MIDI_DEVICE_IS_INITIALIZED;
    }
    else if (midi_device_status == MIDI_DEVICE_IS_INITIALIZED) {
      tud_task();
      bool connected = tud_midi_mounted();
      poll_midi_dev_rx(connected);
    }
    
    led_blinking_task();
  }

  return 0;
}

//--------------------------------------------------------------------+
// TinyUSB Callbacks
//--------------------------------------------------------------------+

//--------------------------------------------------------------------+
// Blinking Task
//--------------------------------------------------------------------+

static void led_blinking_task(void)
{
  const uint32_t interval_ms = 1000;
  static uint32_t start_ms = 0;

  static bool led_state = false;

  // Blink every interval ms
  if ( board_millis() - start_ms < interval_ms) return; // not enough time
  start_ms += interval_ms;

  board_led_write(led_state != 0);
  led_state = 1 - led_state; // toggle
}
