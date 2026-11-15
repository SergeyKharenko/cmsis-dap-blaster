/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Sergey Kharenko
 */

#ifndef CMSIS_DAP_BLASTER_INTERNAL_H
#define CMSIS_DAP_BLASTER_INTERNAL_H

#include "quartus_abi.h"
#include <hidapi.h>
#include <libusb.h>

#include <stddef.h>
#include <stdint.h>

#define JTAG_CLOCK_PARAM             "JtagClock"
#define JTAG_CLOCK_AUTO_ADJUST_PARAM "JtagClockAutoAdjust"
#define JTAG_UNSUPPORTED_PARAMETER   0x2D
#define JTAG_OPEN_FAILED             0x56

typedef struct CmsisDapClockSetting
{
    struct CmsisDapClockSetting *next;
    char identity[512];
    uint32_t jtag_clock;
    unsigned char auto_adjust;
} CmsisDapClockSetting;

#define CMSIS_DAP_COMMAND_JTAG_SEQUENCE 0x14
#define CMSIS_DAP_COMMAND_CONNECT       0x02
#define CMSIS_DAP_COMMAND_DISCONNECT    0x03
#define CMSIS_DAP_COMMAND_SWJ_CLOCK     0x11
#define CMSIS_DAP_COMMAND_JTAG_CONFIG   0x15
#define CMSIS_DAP_STATUS_OK             0x00
#define CMSIS_DAP_CLOCK_UNSUPPORTED     2
#define CMSIS_DAP_CONNECT_JTAG          0x02
#define CMSIS_DAP_DEFAULT_PACKET_SIZE   64U
#define CMSIS_DAP_MAX_PACKET_SIZE       1024U
#define CMSIS_DAP_MAX_SEQUENCES         64U
#define CMSIS_DAP_SEQUENCE_MAX_BITS     64U

typedef struct
{
    unsigned short vid;
    unsigned short pid;
} CmsisDapDeviceId;

typedef enum
{
    CMSIS_DAP_TRANSPORT_HID,
    CMSIS_DAP_TRANSPORT_BULK,
} CmsisDapTransportKind;

typedef struct
{
    unsigned char tms;
    unsigned char bit_count;
    unsigned char tdi[8];
} CmsisDapSequence;

struct JTAGClientData
{
    JTAGServerOperations *server_operations;
    void *server_instance;
    uint32_t jtag_clock;
    unsigned char auto_adjust;
    unsigned char tms_high_count;
    unsigned char auto_adjust_busy;
    uint32_t transport_status;
    CmsisDapClockSetting *clock_setting;
    CmsisDapTransportKind transport_kind;
    hid_device *device_handle;
    libusb_device_handle *usb_handle;
    unsigned char usb_interface;
    unsigned char usb_endpoint_in;
    unsigned char usb_endpoint_out;
    unsigned short usb_endpoint_size;
    char serial_number[128];
    char device_path[512];
    unsigned int device_index;
    uint32_t packet_size;
    CmsisDapSequence sequences[CMSIS_DAP_MAX_SEQUENCES];
    unsigned int sequence_count;
};

int cmsis_dap_command(JTAGClientData *client, const unsigned char *request, size_t request_size,
                      unsigned char *response, size_t response_capacity, size_t *response_size);
int cmsis_dap_execute_sequences(JTAGClientData *client, const CmsisDapSequence *sequences,
                                unsigned int sequence_count, unsigned char *tdo,
                                size_t tdo_capacity, unsigned int *tdo_bits);
uint32_t cmsis_dap_set_clock(JTAGClientData *client, uint32_t frequency);
uint32_t cmsis_dap_auto_adjust_clock(JTAGClientData *client);
JTAGClientData *cmsis_dap_device_open(unsigned int index, JTAGServerOperations *server_operations,
                                      void *server_instance);
JtagBool cmsis_dap_scan_ports(unsigned int index, char *port_name, int size);
JtagBool cmsis_dap_has_supported_device(void);
uint32_t cmsis_dap_open_hardware(JTAGClientData **client, char *device_name,
                                 JTAGServerOperations *server_operations, void *server_instance);
uint32_t cmsis_dap_init_driver(JTAGClientData **client, char *device_name,
                               JTAGServerOperations *server_operations, void *server_instance);
uint32_t cmsis_dap_close_hardware(JTAGClientData *client);
JtagBool cmsis_dap_flush(JTAGClientData *client, JtagBool store, int position);
uint32_t cmsis_dap_delay(JTAGClientData *client, uint32_t microseconds);
uint32_t cmsis_dap_clock_bits(JTAGClientData *client, int tms, int tdi, int bit_count,
                              int position);
uint32_t cmsis_dap_clock_bytes(JTAGClientData *client, int tms, unsigned char *tdi, int bit_count,
                               int position);
uint32_t cmsis_dap_get_param(JTAGClientData *client, char *name, uint32_t *value);
uint32_t cmsis_dap_set_param(JTAGClientData *client, char *name, uint32_t value);
uint32_t cmsis_dap_get_param_block(JTAGClientData *client, char *name, unsigned char *buffer,
                                   uint32_t *size);

#endif /* CMSIS_DAP_BLASTER_INTERNAL_H */
