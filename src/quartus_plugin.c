/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2024 tow3rs
 * Copyright (C) 2026 Sergey Kharenko
 */

#include "cmsis_dap_internal.h"

#include <stddef.h>

/* Changing these layouts breaks the Quartus x86_64 plugin ABI. */
_Static_assert(sizeof(JTAGServerOperations) == 48, "Quartus server table size");
_Static_assert(sizeof(JtagClientOperations) == 208, "Quartus client table size");
_Static_assert(offsetof(JtagClientOperations, Delay) == 0x80, "Quartus Delay offset");
_Static_assert(offsetof(JtagClientOperations, InitDriver) == 0xB8, "Quartus InitDriver offset");

static JtagClientOperations hw_device = {
    .Size = sizeof(JtagClientOperations),
    .Name = "JTAG-CMSIS-DAP-Blaster",
    .Features = 0x800,
    .Flags = 0,
    .Reserved1 = NULL,
    .ScanPorts = cmsis_dap_scan_ports,
    .Reserved2 = NULL,
    .OpenHardware = cmsis_dap_open_hardware,
    .CloseHardware = cmsis_dap_close_hardware,
    .SetParam = cmsis_dap_set_param,
    .GetParam = cmsis_dap_get_param,
    .DirectControl = NULL, // USB-Blaster control includes unwired nCE/nCS/ASDO pins.
    .ClockRaw = cmsis_dap_clock_bits,
    .ClockMultiple = cmsis_dap_clock_bytes,
    .Delay = cmsis_dap_delay,
    .Flush = cmsis_dap_flush,
    // jtagd performs recirculation in software when these callbacks are NULL.
    .EnableRecirculate = NULL,
    .DisableRecirculate = NULL,
    .Recirculate = NULL,
    .SetParamBlock = NULL, // No writable block parameters (including EEPROM).
    .GetParamBlock = cmsis_dap_get_param_block,
    .InitDriver = cmsis_dap_init_driver,
    // The four-wire adapter has no Active Serial/nCE/nCS/ASDO wiring.
    .EnableActiveSerial = NULL,
    .RemoveDriver = NULL, // No persistent driver configuration to remove.
};

void *get_supported_hardware(int index)
{
    return index == 0 && cmsis_dap_has_supported_device() ? &hw_device : NULL;
}
