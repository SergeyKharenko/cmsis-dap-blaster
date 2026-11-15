/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Sergey Kharenko
 */

#include "cmsis_dap_internal.h"

#include <string.h>

int cmsis_dap_command(JTAGClientData *client, const unsigned char *request, size_t request_size,
                      unsigned char *response, size_t response_capacity, size_t *response_size)
{
    unsigned char output[CMSIS_DAP_MAX_PACKET_SIZE + 1];
    unsigned char input[CMSIS_DAP_MAX_PACKET_SIZE + 1];
    int transferred;
    size_t input_offset;

    if (response_size)
    {
        *response_size = 0;
    }
    if (!client ||
        (client->transport_kind == CMSIS_DAP_TRANSPORT_HID ? !client->device_handle
                                                           : !client->usb_handle) ||
        client->packet_size < 2 || client->packet_size > CMSIS_DAP_MAX_PACKET_SIZE || !request ||
        request_size == 0 || request_size > client->packet_size ||
        response_capacity > CMSIS_DAP_MAX_PACKET_SIZE || (response_capacity && !response))
    {
        return -1;
    }

    if (client->transport_kind == CMSIS_DAP_TRANSPORT_HID)
    {
        memset(output, 0, sizeof(output));
        output[0] = 0; /* CMSIS-DAP HID report ID */
        memcpy(output + 1, request, request_size);
        transferred = hid_write(client->device_handle, output, client->packet_size + 1);
        if (transferred < 0 || (size_t)transferred != client->packet_size + 1)
        {
            return -1;
        }

        memset(input, 0, sizeof(input));
        transferred = hid_read_timeout(client->device_handle, input, client->packet_size + 1, 1000);
        if (transferred < 0 || transferred == 0)
        {
            return -1;
        }

        /* hidapi omits the report ID for unnumbered reports. Some wrappers
           prepend zero to a full report; distinguish that by its extra byte,
           since DAP_Info itself has command ID zero. */
        input_offset = (size_t)transferred == client->packet_size + 1 && input[0] == 0 ? 1 : 0;
    }
    else
    {
        if (!client->usb_handle || !client->usb_endpoint_out || !client->usb_endpoint_in)
        {
            return -1;
        }
        transferred = 0;
        if (libusb_bulk_transfer(client->usb_handle, client->usb_endpoint_out,
                                 (unsigned char *)request, (int)request_size, &transferred,
                                 1000) != 0 ||
            (size_t)transferred != request_size)
        {
            return -1;
        }
        /* USB bulk transfers need a zero-length packet when a short CMSIS-DAP
           packet ends exactly on an endpoint packet boundary. */
        if (request_size < client->packet_size && client->usb_endpoint_size &&
            request_size % client->usb_endpoint_size == 0)
        {
            if (libusb_bulk_transfer(client->usb_handle, client->usb_endpoint_out, NULL, 0,
                                     &transferred, 1000) != 0)
            {
                return -1;
            }
        }
        transferred = 0;
        int bulk_status = 0;
        for (unsigned int attempt = 0; attempt < 3 && transferred == 0; attempt++)
        {
            bulk_status = libusb_bulk_transfer(client->usb_handle, client->usb_endpoint_in, input,
                                               (int)client->packet_size, &transferred, 1000);
            if (bulk_status != 0)
            {
                break;
            }
        }
        if (bulk_status != 0 || transferred <= 0)
        {
            return -1;
        }
        input_offset = 0;
    }
    if ((size_t)transferred <= input_offset || input[input_offset] != request[0])
    {
        return -1;
    }

    transferred -= (int)input_offset;
    if ((size_t)transferred > response_capacity)
    {
        return -1;
    }
    if (transferred && response)
    {
        memcpy(response, input + input_offset, (size_t)transferred);
    }
    if (response_size)
    {
        *response_size = (size_t)transferred;
    }
    return 0;
}
