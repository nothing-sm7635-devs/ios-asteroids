#pragma once
#include <stdint.h>

/* Boot the SWEETXNU payload found in ABL's initrd; returns why it could not. */
const char *loader_boot(uintptr_t dtb);
