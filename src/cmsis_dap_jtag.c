/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Sergey Kharenko
 */

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "cmsis_dap_internal.h"

#include <string.h>
#ifndef _WIN32
#include <errno.h>
#include <time.h>
#else
#include <windows.h>
#endif

static unsigned int sequence_bytes(const CmsisDapSequence *sequence)
{
    return (sequence->bit_count + 7U) / 8U;
}

static size_t pending_request_size(const JTAGClientData *client)
{
    size_t size = 2; /* command and sequence count */
    for (unsigned int index = 0; index < client->sequence_count; index++)
    {
        size += 1 + sequence_bytes(&client->sequences[index]);
    }
    return size;
}

static void clear_pending(JTAGClientData *client)
{
    client->sequence_count = 0;
}

int cmsis_dap_execute_sequences(JTAGClientData *client, const CmsisDapSequence *sequences,
                                unsigned int sequence_count, unsigned char *tdo,
                                size_t tdo_capacity, unsigned int *tdo_bits)
{
    unsigned char request[CMSIS_DAP_MAX_PACKET_SIZE];
    unsigned char response[CMSIS_DAP_MAX_PACKET_SIZE];
    size_t response_size = 0;
    size_t request_size = 2;
    size_t response_offset = 2;
    unsigned int total_bits = 0;
    unsigned int tdo_position = 0;

    if (!client || !sequences || sequence_count == 0 || sequence_count > CMSIS_DAP_MAX_SEQUENCES ||
        !tdo || !tdo_bits)
    {
        return -1;
    }
    memset(request, 0, sizeof(request));
    request[0] = CMSIS_DAP_COMMAND_JTAG_SEQUENCE;
    request[1] = (unsigned char)sequence_count;
    for (unsigned int index = 0; index < sequence_count; index++)
    {
        unsigned int bytes;
        if (sequences[index].bit_count == 0 || sequences[index].bit_count > 64)
        {
            return -1;
        }
        bytes = sequence_bytes(&sequences[index]);
        if (request_size + 1 + bytes > client->packet_size ||
            request_size + 1 + bytes > sizeof(request))
        {
            return -1;
        }
        request[request_size++] =
            (unsigned char)((sequences[index].bit_count == 64 ? 0 : sequences[index].bit_count) |
                            (sequences[index].tms ? 0x40 : 0) | 0x80);
        memcpy(request + request_size, sequences[index].tdi, bytes);
        request_size += bytes;
        total_bits += sequences[index].bit_count;
    }
    if ((total_bits + 7U) / 8U > tdo_capacity ||
        cmsis_dap_command(client, request, request_size, response, sizeof(response),
                          &response_size) != 0 ||
        response_size < 2 || response[1] != CMSIS_DAP_STATUS_OK)
    {
        return -1;
    }
    memset(tdo, 0, tdo_capacity);
    for (unsigned int index = 0; index < sequence_count; index++)
    {
        unsigned int bytes = sequence_bytes(&sequences[index]);
        if (response_offset + bytes > response_size)
        {
            return -1;
        }
        for (unsigned int bit = 0; bit < sequences[index].bit_count; bit++)
        {
            unsigned int value = (response[response_offset + bit / 8U] >> (bit % 8U)) & 1U;
            tdo[tdo_position / 8U] |= (unsigned char)(value << (tdo_position % 8U));
            tdo_position++;
        }
        response_offset += bytes;
    }
    *tdo_bits = total_bits;
    return 0;
}

JtagBool cmsis_dap_flush(JTAGClientData *client, JtagBool store, int position)
{
    unsigned char tdo[CMSIS_DAP_MAX_PACKET_SIZE];
    unsigned int tdo_bits = 0;

    (void)store;
    (void)position;
    if (!client || (client->transport_kind == CMSIS_DAP_TRANSPORT_HID ? !client->device_handle
                                                                      : !client->usb_handle))
    {
        return 0;
    }
    if (client->transport_status != 0)
    {
        clear_pending(client);
        return 0;
    }
    if (client->sequence_count == 0)
    {
        return 1;
    }
    if (cmsis_dap_execute_sequences(client, client->sequences, client->sequence_count, tdo,
                                    sizeof(tdo), &tdo_bits) != 0)
    {
        /* A partially executed packet cannot be retried safely. */
        client->transport_status = 1;
        clear_pending(client);
        return 0;
    }
    clear_pending(client);

    if (tdo_bits && client->server_operations && client->server_operations->StoreTDO)
    {
        client->server_operations->StoreTDO(client->server_instance, tdo, tdo_bits);
    }
    if (tdo_bits && client->server_operations && client->server_operations->IndicateFlush)
    {
        client->server_operations->IndicateFlush(client->server_instance);
    }
    return 1;
}

static uint32_t append_sequence(JTAGClientData *client, int tms, const unsigned char *tdi,
                                unsigned int bit_count)
{
    CmsisDapSequence *sequence;
    unsigned int bytes;

    if (bit_count == 0)
    {
        return 0;
    }
    /* Pack adjacent clocks with the same TMS into at most 64-bit sequences,
       just as the reference accumulates bits before emitting MPSSE commands. */
    if (client->sequence_count)
    {
        sequence = &client->sequences[client->sequence_count - 1U];
        unsigned int combined_bits = sequence->bit_count + bit_count;
        unsigned int combined_bytes = (combined_bits + 7U) / 8U;
        if (sequence->tms == !!tms && combined_bits <= CMSIS_DAP_SEQUENCE_MAX_BITS &&
            pending_request_size(client) + combined_bytes - sequence_bytes(sequence) <=
                client->packet_size)
        {
            for (unsigned int bit = 0; bit < bit_count; bit++)
            {
                unsigned int value = (tdi[bit / 8U] >> (bit % 8U)) & 1U;
                unsigned int position = sequence->bit_count + bit;
                sequence->tdi[position / 8U] |= (unsigned char)(value << (position % 8U));
            }
            sequence->bit_count = (unsigned char)combined_bits;
            return 0;
        }
    }
    if (client->sequence_count == CMSIS_DAP_MAX_SEQUENCES ||
        pending_request_size(client) + 1U + ((bit_count + 7U) / 8U) > client->packet_size)
    {
        if (!cmsis_dap_flush(client, 1, 0))
        {
            return client->transport_status;
        }
    }
    if (client->sequence_count == CMSIS_DAP_MAX_SEQUENCES ||
        pending_request_size(client) + 1U + ((bit_count + 7U) / 8U) > client->packet_size)
    {
        return 1;
    }
    sequence = &client->sequences[client->sequence_count++];
    sequence->tms = (unsigned char)!!tms;
    sequence->bit_count = (unsigned char)bit_count;
    bytes = (bit_count + 7U) / 8U;
    memset(sequence->tdi, 0, sizeof(sequence->tdi));
    memcpy(sequence->tdi, tdi, bytes);
    return 0;
}

uint32_t cmsis_dap_delay(JTAGClientData *client, uint32_t microseconds)
{
    if (!client || (client->transport_kind == CMSIS_DAP_TRANSPORT_HID ? !client->device_handle
                                                                      : !client->usb_handle))
    {
        return 1;
    }

    // cmsis_dap_delay is a barrier in the client ABI: queued CMSIS-DAP commands must reach
    // the adapter before sleeping.  Quartus expresses this value in
    // microseconds (the server-side operation is named delay_microseconds).
    if (!cmsis_dap_flush(client, 1, 0))
    {
        return 1;
    }

#ifdef _WIN32
    Sleep(microseconds / 1000U + (microseconds % 1000U != 0));
#else
    struct timespec request = {
        .tv_sec = (time_t)(microseconds / 1000000UL),
        .tv_nsec = (long)((microseconds % 1000000UL) * 1000UL),
    };
    while (nanosleep(&request, &request) != 0)
    {
        if (errno != EINTR)
        {
            return 1;
        }
    }
#endif
    return 0;
}

uint32_t cmsis_dap_clock_bits(JTAGClientData *client, int tms, int tdi, int bit_count, int position)
{
    if (!client ||
        (client->transport_kind == CMSIS_DAP_TRANSPORT_HID ? !client->device_handle
                                                           : !client->usb_handle) ||
        bit_count < 0)
    {
        return 1;
    }
    if (client->transport_status != 0)
    {
        return client->transport_status;
    }
    tms = !!tms;
    tdi = !!tdi;
    for (int current_bit = 0; current_bit < bit_count; current_bit++)
    {
        unsigned char data = (unsigned char)tdi;
        uint32_t status = append_sequence(client, tms, &data, 1);
        if (status != 0)
        {
            return status;
        }

        // Five consecutive TMS-high clocks put every TAP in Test-Logic-Reset.
        // Calibrate here, with all earlier commands completed, and leave the
        // physical TAP in reset so the caller's remaining clocks stay valid.
        if (!tms)
        {
            client->tms_high_count = 0;
        }
        else if (client->tms_high_count < 5)
        {
            client->tms_high_count++;
            if (client->tms_high_count == 5 && client->auto_adjust && !client->auto_adjust_busy)
            {
                if (!cmsis_dap_flush(client, 1, position))
                {
                    return client->transport_status;
                }
                status = cmsis_dap_auto_adjust_clock(client);
                client->transport_status = status;
                if (status != 0)
                {
                    return status;
                }
            }
        }
    }

    return 0;
}

uint32_t cmsis_dap_clock_bytes(JTAGClientData *client, int tms, unsigned char *tdi, int bit_count,
                               int position)
{
    if (!client ||
        (client->transport_kind == CMSIS_DAP_TRANSPORT_HID ? !client->device_handle
                                                           : !client->usb_handle) ||
        bit_count < 0 || (bit_count && !tdi))
    {
        return 1;
    }
    for (int bit_position = 0; bit_position < bit_count; bit_position++)
    {
        uint32_t status =
            cmsis_dap_clock_bits(client, tms, (tdi[bit_position / 8] >> (bit_position % 8)) & 0x1,
                                 1, position + bit_position);
        if (status != 0)
        {
            return status;
        }
    }

    return 0;
}
