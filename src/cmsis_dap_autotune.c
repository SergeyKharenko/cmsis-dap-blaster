/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Sergey Kharenko
 */

#include "cmsis_dap_internal.h"

#include <string.h>

/* Bound chain length and latency, with 32 terminating ones after 128 IDCODEs. */
#define AUTOTUNE_MAX_DEVICES 128U
#define AUTOTUNE_SCAN_BYTES  (AUTOTUNE_MAX_DEVICES * 4U + 4U)
#define AUTOTUNE_REPETITIONS 5U

static uint32_t reset_tap(JTAGClientData *client)
{
    const CmsisDapSequence sequence = {.tms = 1, .bit_count = 5, .tdi = {0x1F}};
    unsigned char tdo[1];
    unsigned int tdo_bits;
    return cmsis_dap_execute_sequences(client, &sequence, 1, tdo, sizeof(tdo), &tdo_bits) == 0 &&
                   tdo_bits == 5
               ? 0
               : 1;
}

static uint32_t read_reset_chain(JTAGClientData *client, unsigned char *signature)
{
    static const CmsisDapSequence enter_shift[] = {
        {.tms = 1, .bit_count = 5, .tdi = {0x1F}},
        {.tms = 0, .bit_count = 1, .tdi = {0x00}},
        {.tms = 1, .bit_count = 1, .tdi = {0x01}},
        {.tms = 0, .bit_count = 2, .tdi = {0x00}}, /* Capture-DR, then Shift-DR */
    };
    unsigned char tdo[CMSIS_DAP_MAX_PACKET_SIZE];
    unsigned int tdo_bits;
    size_t offset = 0;

    if (cmsis_dap_execute_sequences(client, enter_shift,
                                    sizeof(enter_shift) / sizeof(enter_shift[0]), tdo, sizeof(tdo),
                                    &tdo_bits) != 0 ||
        tdo_bits != 9)
    {
        return 1;
    }
    /* Split private scans to fit both HID and bulk command packets. Probe TDO
       stays private and is never delivered through the Quartus callbacks. */
    while (offset < AUTOTUNE_SCAN_BYTES)
    {
        CmsisDapSequence sequences[CMSIS_DAP_MAX_SEQUENCES];
        unsigned int sequence_count = 0;
        size_t request_size = 2;
        size_t scan_bytes = 0;
        while (offset + scan_bytes < AUTOTUNE_SCAN_BYTES &&
               sequence_count < CMSIS_DAP_MAX_SEQUENCES)
        {
            size_t bytes = AUTOTUNE_SCAN_BYTES - offset - scan_bytes;
            if (bytes > 8)
            {
                bytes = 8;
            }
            if (request_size + 1 + bytes > client->packet_size)
            {
                break;
            }
            CmsisDapSequence *sequence = &sequences[sequence_count++];
            sequence->tms = 0;
            sequence->bit_count = (unsigned char)(bytes * 8U);
            memset(sequence->tdi, 0xFF, sizeof(sequence->tdi));
            scan_bytes += bytes;
            request_size += 1 + bytes;
        }
        if (!sequence_count ||
            cmsis_dap_execute_sequences(client, sequences, sequence_count, tdo, sizeof(tdo),
                                        &tdo_bits) != 0 ||
            tdo_bits != scan_bytes * 8U)
        {
            return 1;
        }
        memcpy(signature + offset, tdo, scan_bytes);
        offset += scan_bytes;
    }
    return reset_tap(client);
}

static unsigned int signature_bit(const unsigned char *signature, unsigned int bit)
{
    return (signature[bit / 8U] >> (bit % 8U)) & 1U;
}

static JtagBool valid_signature(const unsigned char *signature)
{
    unsigned int position = 0;
    for (unsigned int devices = 0; devices <= AUTOTUNE_MAX_DEVICES; devices++)
    {
        uint32_t idcode = 0;
        for (unsigned int bit = 0; bit < 32U; bit++)
        {
            idcode |= (uint32_t)signature_bit(signature, position + bit) << bit;
        }
        if (idcode == UINT32_MAX)
        {
            // A disconnected/stuck-high TDO must not qualify as a fast chain.
            if (!devices)
            {
                return 0;
            }
            for (; position < AUTOTUNE_SCAN_BYTES * 8U; position++)
            {
                if (!signature_bit(signature, position))
                {
                    return 0;
                }
            }
            return 1;
        }
        if (devices == AUTOTUNE_MAX_DEVICES)
        {
            return 0;
        }
        if (idcode & 1U)
        {
            unsigned int manufacturer = (idcode >> 1) & 0x7FFU;
            if (!manufacturer || manufacturer == 0x7FFU)
            {
                return 0;
            }
            position += 32U;
        }
        else
        {
            position++; // A device without IDCODE resets to its one-bit BYPASS register.
        }
    }
    return 0;
}

static uint32_t matches_reference(JTAGClientData *client, const unsigned char *reference,
                                  JtagBool *matches)
{
    unsigned char signature[AUTOTUNE_SCAN_BYTES];
    *matches = 0;
    for (unsigned int repeat = 0; repeat < AUTOTUNE_REPETITIONS; repeat++)
    {
        uint32_t status = read_reset_chain(client, signature);
        if (status != 0)
        {
            return status;
        }
        if (memcmp(reference, signature, sizeof(signature)) != 0)
        {
            return 0;
        }
    }
    *matches = 1;
    return 0;
}

static uint32_t select_frequency(JTAGClientData *client, uint32_t frequency)
{
    return cmsis_dap_set_clock(client, frequency);
}

uint32_t cmsis_dap_auto_adjust_clock(JTAGClientData *client)
{
    static const uint32_t reference_rates[] = {1000000U, 100000U, 10000U};
    static const uint32_t candidate_rates[] = {
        40000000U, 30000000U, 20000000U, 15000000U, 10000000U, 8000000U,
        5000000U,  4000000U,  2000000U,  1000000U,  500000U,   100000U,
    };
    unsigned char reference[AUTOTUNE_SCAN_BYTES];
    if (!client || client->auto_adjust_busy)
    {
        return 0;
    }
    client->auto_adjust_busy = 1;
    uint32_t status;
    uint32_t original_rate = client->jtag_clock;
    uint32_t reference_rate = 0;

    // Called only at a caller-requested TAP reset, after its queue is flushed.
    for (unsigned int rate = 0; rate < sizeof(reference_rates) / sizeof(reference_rates[0]); rate++)
    {
        status = select_frequency(client, reference_rates[rate]);
        if (status == CMSIS_DAP_CLOCK_UNSUPPORTED)
        {
            continue;
        }
        if (status != 0)
        {
            goto done;
        }
        status = read_reset_chain(client, reference);
        if (status != 0)
        {
            goto done;
        }
        if (!valid_signature(reference))
        {
            continue;
        }
        JtagBool matches;
        status = matches_reference(client, reference, &matches);
        if (status != 0)
        {
            goto done;
        }
        if (matches)
        {
            reference_rate = client->jtag_clock;
            break;
        }
    }

    if (!reference_rate)
    {
        // Leave an absent/unsupported chain for the caller's normal scan to diagnose.
        status = cmsis_dap_set_clock(client, original_rate);
        goto done;
    }

    uint32_t selected_rate = client->jtag_clock;
    uint32_t maximum_rate = 40000000U;
    for (unsigned int rate = 0; rate < sizeof(candidate_rates) / sizeof(candidate_rates[0]); rate++)
    {
        if (candidate_rates[rate] > maximum_rate || candidate_rates[rate] <= reference_rate)
        {
            continue;
        }
        status = select_frequency(client, candidate_rates[rate]);
        if (status == CMSIS_DAP_CLOCK_UNSUPPORTED)
        {
            continue;
        }
        if (status != 0)
        {
            goto done;
        }
        JtagBool matches;
        status = matches_reference(client, reference, &matches);
        if (status != 0)
        {
            goto done;
        }
        if (matches)
        {
            selected_rate = client->jtag_clock;
            break;
        }
    }

    // A rejected high-speed probe may have lost its final TMS transitions.
    // Re-establish reset at the verified reference rate before returning.
    status = select_frequency(client, reference_rate);
    if (status != 0)
    {
        goto done;
    }
    status = reset_tap(client);
    if (status != 0)
    {
        goto done;
    }
    status = cmsis_dap_set_clock(client, selected_rate);
    if (status == 0 && client->clock_setting)
    {
        client->clock_setting->jtag_clock = selected_rate;
    }
done:
    client->auto_adjust_busy = 0;
    return status;
}
