#pragma once
#include <stdint.h>
#include "tusb.h"
void clone_descriptors(uint8_t dev_addr);
void set_cloning_required();
bool cloning_is_required();
bool clone_next_string_is_required();
void clone_next_string();
bool descriptors_are_cloned(void);
void set_descriptors_uncloned(void);
TU_ATTR_WEAK void device_clone_complete_cb();
