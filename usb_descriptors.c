/* 
 * The MIT License (MIT)
 *
 * Copyright (c) 2019 Ha Thach (tinyusb.org)
 *                    rppicomidi
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

#include "tusb.h"
#include "stdlib.h"
#include "usb_descriptors.h"
static tusb_desc_device_t desc_device_connected;

static uint8_t* desc_fs_configuration = NULL;
static uint8_t daddr;
struct devstring_set_s {
    uint16_t langid;
    uint16_t** string_list;
};

static struct devstring_set_s* devstrings = NULL;
static uint8_t num_langids = 0;
static uint8_t langid_idx = 0;
static uint8_t string_idx = 0;
static uint8_t* string_idx_list = NULL;
static uint8_t nstrings = 0;
#define SZ_SCRATCHPAD 128
static uint8_t scratchpad[SZ_SCRATCHPAD];
static enum {UNCLONED, START_CLONING, CLONING, CLONE_NEXT_DESCRIPTOR, CLONED} clone_state = UNCLONED;


void set_cloning_required()
{
  clone_state = START_CLONING;
}

bool cloning_is_required()
{
  return clone_state == START_CLONING;
}

bool clone_next_string_is_required()
{
  return clone_state == CLONE_NEXT_DESCRIPTOR;
}

bool descriptors_are_cloned(void)
{
  return clone_state == CLONED;
}

void set_descriptors_uncloned(void)
{
  clone_state = UNCLONED;
}

static void clone_string_cb(tuh_xfer_t* xfer)
{
  if (XFER_RESULT_SUCCESS == xfer->result) {
    devstrings[langid_idx].string_list[string_idx] = malloc(xfer->buffer[0]);
    memcpy(devstrings[langid_idx].string_list[string_idx], xfer->buffer, xfer->buffer[0]);
    if (++string_idx < nstrings) {
      clone_state = CLONE_NEXT_DESCRIPTOR;
    }
    else {
      printf("All strings for langid 0x%04x:\r\n", devstrings[langid_idx].langid);
      for (uint8_t idx=0; idx < nstrings; idx++) {
        uint8_t length_string = (*((uint8_t*)(devstrings[langid_idx].string_list[idx])) - 2);
        char* cptr = (char*)(devstrings[langid_idx].string_list[idx])+2;
        for (uint8_t jdx=0; jdx < length_string; jdx++) {
          if (cptr[jdx]) {
            printf("%c", cptr[jdx]);
          }
        }
        printf("\r\n");
      }
      if (++langid_idx < num_langids) {
        string_idx = 0;
        clone_state = CLONE_NEXT_DESCRIPTOR;
      }
      else {
        clone_state = CLONED;
        printf("all strings cloned\r\n");
        if (device_clone_complete_cb) device_clone_complete_cb();
      }
    }
  }
}

void clone_next_string()
{
  if (langid_idx < num_langids && string_idx < nstrings)
  {
    if (tuh_descriptor_get_string(daddr, string_idx_list[string_idx], devstrings[langid_idx].langid, scratchpad, SZ_SCRATCHPAD, clone_string_cb, 0)) {
      clone_state = CLONING;
    }
  }
}

static void clone_langids_cb(tuh_xfer_t* xfer)
{
  if (XFER_RESULT_SUCCESS == xfer->result) {
    num_langids = (xfer->actual_len - 2)/2;
    uint16_t* langids = (uint16_t*)(xfer->buffer+2);
    devstrings = calloc(num_langids, sizeof(uint16_t));
    printf("num_langids=%u\r\n", num_langids);
    int idx;
    for (idx = 0; idx < num_langids; idx++) {
      devstrings[idx].langid = langids[idx];
      printf("langid %u=0x%04x\r\n", idx, langids[idx]);
      devstrings[idx].string_list = calloc(nstrings, sizeof(*(devstrings[idx].string_list)));
    }
    if (nstrings > 0) {
      clone_state = CLONE_NEXT_DESCRIPTOR;
    }
    else {
      clone_state = CLONED;
    }
  }
  
  // Continue until all strings for all langids are fetched.
  // Then call clone_complete_cb()--that should trigger tud_init(0) call.
}

// The callback functions capture the device descriptor and configuration descriptor
// This function starts the process that extracts the string descriptors.
// It assumes that captured device descriptor and configuration descriptor
// are valid and that all string descriptor indices are available
void clone_descriptors(uint8_t dev_addr)
{
  int idx, jdx;
  const uint8_t* midi_string_idxs;
  uint8_t nmidi_strings;
  if (dev_addr == daddr) {
    // first free any old devstrings
    if (devstrings != NULL) {
      for (idx = 0; idx < num_langids; idx++) {
        for (jdx = 0; jdx < nstrings; jdx++) {
          free(devstrings[idx].string_list[jdx]);
        }
        free(devstrings+idx);
      }
    }
    // Get the list of string indexs
    nmidi_strings = tuh_midi_get_all_istrings(dev_addr, &midi_string_idxs);
    nstrings = nmidi_strings;
    if (desc_device_connected.iManufacturer != 0) {
        ++nstrings;
    }
    if (desc_device_connected.iProduct != 0) {
        ++nstrings;
    }
    if (desc_device_connected.iSerialNumber != 0) {
        ++nstrings;
    }
    string_idx_list = malloc(nstrings);
    string_idx = 0;
    if (desc_device_connected.iManufacturer != 0) {
        string_idx_list[string_idx++] = desc_device_connected.iManufacturer;
    }
    if (desc_device_connected.iProduct != 0) {
        string_idx_list[string_idx++] = desc_device_connected.iProduct;
    }
    if (desc_device_connected.iSerialNumber != 0) {
        string_idx_list[string_idx++] = desc_device_connected.iSerialNumber;
    }
    if (nmidi_strings > 0)
        memcpy(string_idx_list+string_idx, midi_string_idxs, nmidi_strings);
    
    // Kick off the process of fetching all strings for all languages from the host-connected device
    string_idx = 0;
    langid_idx = 0;
    // get the string langid list
    if (tuh_descriptor_get_string(daddr, 0, 0, scratchpad, SZ_SCRATCHPAD, clone_langids_cb, 0)) {
      clone_state = CLONING;
    }
  }
}

// grab the device descriptor during enumeration
void tuh_desc_device_cb(uint8_t dev_addr, const tusb_desc_device_t *desc)
{
    daddr = dev_addr;        
    memcpy(&desc_device_connected, desc, sizeof(desc_device_connected));
    printf("device descriptor cloned");
    uint8_t* ptr = (uint8_t*)&desc_device_connected;
    for (uint8_t idx=0; idx < sizeof(desc_device_connected); idx++) {
      if ((idx % 16) == 0)
        printf("\r\n");
      printf("%02x ", *ptr++);
    }
    printf("\r\n");
}

void tuh_desc_config_cb(uint8_t dev_addr, const tusb_desc_configuration_t *desc_config)
{
    if (daddr == dev_addr && desc_config->wTotalLength > 0) {
        desc_fs_configuration = malloc(desc_config->wTotalLength);
        if (desc_fs_configuration) {
            memcpy(desc_fs_configuration, desc_config, desc_config->wTotalLength);
            printf("configuration descriptor cloned");
            uint8_t* ptr = (uint8_t*)desc_fs_configuration;
            for (uint8_t idx=0; idx < desc_config->wTotalLength; idx++) {
              if ((idx % 16) == 0)
                printf("\r\n");
              printf("%02x ", *ptr++);
            }
            printf("\r\n");
        }        
    }
}

// Invoked when received GET DEVICE DESCRIPTOR
// Application return pointer to descriptor
uint8_t const * tud_descriptor_device_cb(void)
{
  printf("midi device descriptor returned\r\n");
  return (uint8_t const *) &desc_device_connected;
}

uint8_t midid_get_endpoint0_size()
{
  return desc_device_connected.bMaxPacketSize0;
}

//--------------------------------------------------------------------+
// Configuration Descriptor
//--------------------------------------------------------------------+

// Invoked when received GET CONFIGURATION DESCRIPTOR
// Application return pointer to descriptor
// Descriptor contents must exist long enough for transfer to complete
uint8_t const * tud_descriptor_configuration_cb(uint8_t index)
{
  (void) index; // for multiple configurations
  printf("midi device configuration returned\r\n");
  return desc_fs_configuration;
}

//--------------------------------------------------------------------+
// String Descriptors
//--------------------------------------------------------------------+
// Invoked when received GET STRING DESCRIPTOR request
// Application return pointer to descriptor, whose contents must exist long enough for transfer to complete
uint16_t const* tud_descriptor_string_cb(uint8_t index, uint16_t langid)
{
  uint16_t* ptr = NULL;
  // return the langid list descriptor if index == 0
  if (index == 0) {
    scratchpad[0] = (num_langids * 2) + 2;  // descriptor length
    scratchpad[1] = TUSB_DESC_STRING;
    ptr = (uint16_t*)(scratchpad+2);
    for (int idx=0; idx < num_langids; idx++) {
      *ptr++ = devstrings[idx].langid;
    }
    ptr = (uint16_t*)scratchpad;
  }
  else {
    // find the string descriptor index for this langid
    int lang;
    for (lang = 0; lang < num_langids && devstrings[lang].langid != langid; lang++) {
    }
    if (lang < num_langids) {
      // then we found the devstrings structure for this langid
      int idx;
      for (idx = 0; idx < nstrings && string_idx_list[idx] != index; idx++) {
      }
      if (idx < nstrings) {
        ptr = (uint16_t*)(devstrings[lang].string_list[idx]);
      }
    }
  }
  return ptr;
}
