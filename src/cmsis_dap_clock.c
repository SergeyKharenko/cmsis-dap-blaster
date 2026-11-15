/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Sergey Kharenko
 */

#include "cmsis_dap_internal.h"

#include <string.h>

uint32_t cmsis_dap_set_clock(JTAGClientData *client, uint32_t frequency)
{
    unsigned char request[5] = {
        CMSIS_DAP_COMMAND_SWJ_CLOCK,      (unsigned char)(frequency >> 0),
        (unsigned char)(frequency >> 8),  (unsigned char)(frequency >> 16),
        (unsigned char)(frequency >> 24),
    };
    unsigned char response[CMSIS_DAP_MAX_PACKET_SIZE];
    size_t response_size;
    if (cmsis_dap_command(client, request, sizeof(request), response, sizeof(response),
                          &response_size) != 0)
    {
        return 1;
    }
    if (response_size < 2)
    {
        return 1;
    }
    if (response[1] != CMSIS_DAP_STATUS_OK)
    {
        return CMSIS_DAP_CLOCK_UNSUPPORTED;
    }
    client->jtag_clock = frequency;
    return 0;
}

uint32_t cmsis_dap_get_param(JTAGClientData *client, char *param_name, uint32_t *param_value)
{
    if (!client || !param_name)
    {
        return JTAG_UNSUPPORTED_PARAMETER;
    }

    if (!strcmp(param_name, JTAG_CLOCK_PARAM))
    {
        if (param_value)
        {
            *param_value = client->jtag_clock;
        }
        return 0;
    }

    if (!strcmp(param_name, JTAG_CLOCK_AUTO_ADJUST_PARAM))
    {
        if (param_value)
        {
            *param_value = !!client->auto_adjust;
        }
        return 0;
    }

    return JTAG_UNSUPPORTED_PARAMETER;
}

uint32_t cmsis_dap_set_param(JTAGClientData *client, char *param_name, uint32_t param_value)
{
    if (!client || !param_name)
    {
        return JTAG_UNSUPPORTED_PARAMETER;
    }

    if (!strcmp(param_name, JTAG_CLOCK_AUTO_ADJUST_PARAM))
    {
        if (param_value > 1)
        {
            return JTAG_UNSUPPORTED_PARAMETER;
        }
        // Calibration starts at TAP reset, without interrupting queued traffic.
        client->auto_adjust = (unsigned char)param_value;
        if (client->clock_setting)
        {
            client->clock_setting->auto_adjust = (unsigned char)param_value;
        }
        return 0;
    }

    if (strcmp(param_name, JTAG_CLOCK_PARAM) != 0 || param_value == 0)
    {
        return JTAG_UNSUPPORTED_PARAMETER;
    }

    // Finish queued clocks using the old request before changing frequency.
    if (!cmsis_dap_flush(client, 1, 0))
    {
        return 1;
    }

    uint32_t status = cmsis_dap_set_clock(client, param_value);
    if (status == CMSIS_DAP_CLOCK_UNSUPPORTED)
    {
        // Quartus parameter errors use AJI status 0x2D.
        return JTAG_UNSUPPORTED_PARAMETER;
    }
    client->transport_status = status;
    if (status == 0 && client->clock_setting)
    {
        client->clock_setting->jtag_clock = client->jtag_clock;
    }
    return status;
}

uint32_t cmsis_dap_get_param_block(JTAGClientData *client, char *param_name, unsigned char *buffer,
                                   uint32_t *size)
{
    uint32_t length;
    if (!client ||
        (client->transport_kind == CMSIS_DAP_TRANSPORT_HID ? !client->device_handle
                                                           : !client->usb_handle) ||
        !param_name || !size || (*size && !buffer))
    {
        return JTAG_UNSUPPORTED_PARAMETER;
    }
    if (strcmp(param_name, "SerialNumber") != 0)
    {
        return JTAG_UNSUPPORTED_PARAMETER;
    }
    length = (uint32_t)strlen(client->serial_number);
    // Match the Quartus USB-Blaster block parameter convention: the input
    // size is capacity, the output size is bytes copied, without a terminator.
    if (length > *size)
    {
        length = *size;
    }
    if (length)
    {
        memcpy(buffer, client->serial_number, length);
    }
    *size = length;
    return 0;
}
