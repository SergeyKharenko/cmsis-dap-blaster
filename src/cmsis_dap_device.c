/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Sergey Kharenko
 */

#include "cmsis_dap_internal.h"

#include <stdlib.h>
#include <string.h>

#include <stdio.h>
#include <errno.h>
#include <limits.h>
#include <ctype.h>
#include <wctype.h>

static const CmsisDapDeviceId valid_devices[] = {
    {0x0D28, 0x0204}, /* Arm DAPLink */
    {0x03EB, 0x2111}, /* Microchip EDBG */
    {0x03EB, 0x2140}, {0x03EB, 0x2141}, {0x03EB, 0x2144}, {0x03EB, 0x2145}, {0x03EB, 0x216C},
    {0x03EB, 0x2175}, {0x04B4, 0xF138}, {0x04B4, 0xF148}, {0x04B4, 0xF151}, {0x04B4, 0xF152},
    {0x04B4, 0xF154}, {0x04B4, 0xF155}, {0x04B4, 0xF166}, {0xC251, 0x2750}, {0x30CC, 0x9527},
    {0x1FC9, 0x0090}, {0x1FC9, 0x0143}, {0x1A86, 0x8011}, {0x2A86, 0x8011},
};

static int hid_initialized;
static libusb_context *usb_context;
static int usb_initialized;
// jtagd closes a cable between clients. Preserve its selected clock request
// for subsequent opens in this server process, keyed by serial or device path.
static CmsisDapClockSetting *clock_settings;

static void clear_clock_settings(void)
{
    while (clock_settings)
    {
        CmsisDapClockSetting *next = clock_settings->next;
        free(clock_settings);
        clock_settings = next;
    }
}

static void finalize_backends(void)
{
    clear_clock_settings();
    if (hid_initialized)
    {
        hid_exit();
        hid_initialized = 0;
    }
    if (usb_initialized)
    {
        libusb_exit(usb_context);
        usb_context = NULL;
        usb_initialized = 0;
    }
}

#ifndef _WIN32
__attribute__((destructor)) static void finalize_library(void)
{
    finalize_backends();
}
#else
static int register_cleanup(void)
{
    static int registered;
    if (!registered)
    {
        if (atexit(finalize_backends) != 0)
        {
            return -1;
        }
        registered = 1;
    }
    return 0;
}
#endif

static CmsisDapClockSetting *find_clock_setting(const char *identity)
{
    for (CmsisDapClockSetting *setting = clock_settings; setting; setting = setting->next)
    {
        if (!strcmp(setting->identity, identity))
        {
            return setting;
        }
    }
    CmsisDapClockSetting *setting = calloc(1, sizeof(*setting));
    if (!setting)
    {
        return NULL;
    }
    snprintf(setting->identity, sizeof(setting->identity), "%s", identity);
    setting->jtag_clock = 10000000U;
    setting->next = clock_settings;
    clock_settings = setting;
    return setting;
}

static int initialize_hid(void)
{
#ifdef _WIN32
    if (register_cleanup() != 0)
    {
        return -1;
    }
#endif
    if (!hid_initialized)
    {
        if (hid_init() != 0)
        {
            return -1;
        }
        hid_initialized = 1;
    }
    return 0;
}

static int initialize_libusb(void)
{
#ifdef _WIN32
    if (register_cleanup() != 0)
    {
        return -1;
    }
#endif
    if (!usb_initialized)
    {
        if (libusb_init(&usb_context) != 0)
        {
            return -1;
        }
        usb_initialized = 1;
    }
    return 0;
}

static int wide_contains(const wchar_t *text, const char *needle)
{
    size_t needle_length;

    if (!text || !needle)
    {
        return 0;
    }
    needle_length = strlen(needle);
    for (const wchar_t *cursor = text; *cursor; cursor++)
    {
        size_t offset = 0;
        while (offset < needle_length && cursor[offset] &&
               towlower(cursor[offset]) == towlower((unsigned char)needle[offset]))
        {
            offset++;
        }
        if (offset == needle_length)
        {
            return 1;
        }
    }
    return 0;
}

static int known_cmsis_dap_id(unsigned short vendor_id, unsigned short product_id)
{
    for (size_t index = 0; index < sizeof(valid_devices) / sizeof(valid_devices[0]); index++)
    {
        if (valid_devices[index].vid == vendor_id && valid_devices[index].pid == product_id)
        {
            return 1;
        }
    }
    return 0;
}

static int is_cmsis_dap(const struct hid_device_info *info)
{
    if (!info)
    {
        return 0;
    }
    /* The vendor-defined HID usage page is shared by unrelated devices (for
       example touchpads), so require the CMSIS-DAP/DAPLink identity string as
       well. This avoids opening an arbitrary vendor HID interface. */
    return (info->usage_page == 0xFF00 || info->usage_page == 0xFF01) &&
           (known_cmsis_dap_id(info->vendor_id, info->product_id) ||
            wide_contains(info->product_string, "CMSIS-DAP") ||
            wide_contains(info->product_string, "DAPLink") ||
            wide_contains(info->manufacturer_string, "CMSIS-DAP") ||
            wide_contains(info->manufacturer_string, "DAPLink"));
}

static void wide_to_ascii(char *destination, size_t destination_size, const wchar_t *source)
{
    size_t position = 0;
    if (!destination || !destination_size)
    {
        return;
    }
    if (source)
    {
        while (source[position] && position + 1 < destination_size)
        {
            destination[position] = (source[position] >= 0x20 && source[position] < 0x7F)
                                        ? (char)source[position]
                                        : '?';
            position++;
        }
    }
    destination[position] = '\0';
}

static int ascii_contains(const char *text, const char *needle)
{
    size_t needle_length;
    if (!text || !needle)
    {
        return 0;
    }
    needle_length = strlen(needle);
    for (const char *cursor = text; *cursor; cursor++)
    {
        size_t offset = 0;
        while (offset < needle_length && cursor[offset] &&
               tolower((unsigned char)cursor[offset]) == tolower((unsigned char)needle[offset]))
        {
            offset++;
        }
        if (offset == needle_length)
        {
            return 1;
        }
    }
    return 0;
}

typedef struct
{
    libusb_device *device;
    uint16_t vendor_id;
    uint16_t product_id;
    unsigned char configuration;
    unsigned char interface_number;
    unsigned char alternate_setting;
    unsigned char endpoint_in;
    unsigned char endpoint_out;
    unsigned short endpoint_size;
    char product[128];
    char manufacturer[128];
    char interface_name[128];
    char serial_number[128];
} CmsisDapBulkInfo;

static int get_ascii_string(libusb_device_handle *handle, unsigned char index, char *buffer,
                            size_t size)
{
    int length;
    if (!index || !buffer || size < 2)
    {
        if (buffer && size)
        {
            buffer[0] = '\0';
        }
        return 0;
    }
    length =
        libusb_get_string_descriptor_ascii(handle, index, (unsigned char *)buffer, (int)(size - 1));
    if (length < 0)
    {
        buffer[0] = '\0';
        return 0;
    }
    buffer[length] = '\0';
    return 1;
}

static int bulk_interface_matches(const struct libusb_interface_descriptor *interface,
                                  libusb_device_handle *handle, CmsisDapBulkInfo *info)
{
    /* CMSIS-DAP v2 defines a vendor-specific interface with class/subclass/
       protocol 0xFF/0/0. The endpoint order is part of the interface contract:
       command OUT, response IN, and optional SWO IN. Matching this strictly
       avoids claiming an unrelated vendor bulk interface on composite devices. */
    if (interface->bInterfaceClass != LIBUSB_CLASS_VENDOR_SPEC ||
        interface->bInterfaceSubClass != 0 || interface->bInterfaceProtocol != 0 ||
        (interface->bNumEndpoints != 2 && interface->bNumEndpoints != 3))
    {
        return 0;
    }

    for (unsigned int endpoint = 0; endpoint < interface->bNumEndpoints; endpoint++)
    {
        const struct libusb_endpoint_descriptor *descriptor = &interface->endpoint[endpoint];
        unsigned char direction = descriptor->bEndpointAddress & LIBUSB_ENDPOINT_DIR_MASK;
        if ((descriptor->bmAttributes & LIBUSB_TRANSFER_TYPE_MASK) != LIBUSB_TRANSFER_TYPE_BULK)
        {
            return 0;
        }
        if ((endpoint == 0 && direction != LIBUSB_ENDPOINT_OUT) ||
            ((endpoint == 1 || endpoint == 2) && direction != LIBUSB_ENDPOINT_IN))
        {
            return 0;
        }
    }

    info->endpoint_out = interface->endpoint[0].bEndpointAddress;
    info->endpoint_in = interface->endpoint[1].bEndpointAddress;
    info->endpoint_size = interface->endpoint[0].wMaxPacketSize;
    get_ascii_string(handle, interface->iInterface, info->interface_name,
                     sizeof(info->interface_name));
    return known_cmsis_dap_id(info->vendor_id, info->product_id) ||
           ascii_contains(info->product, "CMSIS-DAP") || ascii_contains(info->product, "DAPLink") ||
           ascii_contains(info->manufacturer, "CMSIS-DAP") ||
           ascii_contains(info->manufacturer, "DAPLink") ||
           ascii_contains(info->interface_name, "CMSIS-DAP") ||
           ascii_contains(info->interface_name, "DAPLink");
}

static int inspect_bulk_device(libusb_device *device, CmsisDapBulkInfo *info)
{
    struct libusb_device_descriptor descriptor;
    struct libusb_config_descriptor *config = NULL;
    libusb_device_handle *handle = NULL;
    int result = 0;

    memset(info, 0, sizeof(*info));
    info->device = device;
    if (libusb_get_device_descriptor(device, &descriptor) != 0 || libusb_open(device, &handle) != 0)
    {
        return 0;
    }
    info->vendor_id = descriptor.idVendor;
    info->product_id = descriptor.idProduct;
    get_ascii_string(handle, descriptor.iProduct, info->product, sizeof(info->product));
    get_ascii_string(handle, descriptor.iManufacturer, info->manufacturer,
                     sizeof(info->manufacturer));
    get_ascii_string(handle, descriptor.iSerialNumber, info->serial_number,
                     sizeof(info->serial_number));
    if (libusb_get_active_config_descriptor(device, &config) != 0 &&
        libusb_get_config_descriptor(device, 0, &config) != 0)
    {
        goto done;
    }
    for (int interface_index = 0; interface_index < config->bNumInterfaces; interface_index++)
    {
        const struct libusb_interface *interface = &config->interface[interface_index];
        for (int alternate = 0; alternate < interface->num_altsetting; alternate++)
        {
            const struct libusb_interface_descriptor *alt = &interface->altsetting[alternate];
            CmsisDapBulkInfo candidate = *info;
            if (!bulk_interface_matches(alt, handle, &candidate))
            {
                continue;
            }
            candidate.configuration = config->bConfigurationValue;
            candidate.interface_number = alt->bInterfaceNumber;
            candidate.alternate_setting = alt->bAlternateSetting;
            *info = candidate;
            result = 1;
            goto done;
        }
    }
done:
    if (config)
    {
        libusb_free_config_descriptor(config);
    }
    libusb_close(handle);
    return result;
}

/* Keep both backend enumerations alive until the selected port is opened. */
typedef struct CmsisDapPort
{
    struct CmsisDapPort *next;
    CmsisDapTransportKind transport_kind;
    CmsisDapBulkInfo bulk;
    struct hid_device_info *hid;
} CmsisDapPort;

typedef struct
{
    CmsisDapPort *ports;
    struct hid_device_info *hid_devices;
    libusb_device **usb_devices;
} CmsisDapPortList;

static void free_port_list(CmsisDapPortList *list)
{
    while (list->ports)
    {
        CmsisDapPort *next = list->ports->next;
        free(list->ports);
        list->ports = next;
    }
    if (list->hid_devices)
    {
        hid_free_enumeration(list->hid_devices);
    }
    if (list->usb_devices)
    {
        libusb_free_device_list(list->usb_devices, 1);
    }
    memset(list, 0, sizeof(*list));
}

static int same_probe(const CmsisDapBulkInfo *bulk, const struct hid_device_info *hid)
{
    if (bulk->vendor_id != hid->vendor_id || bulk->product_id != hid->product_id ||
        !bulk->serial_number[0] || !hid->serial_number || !hid->serial_number[0])
    {
        return 0;
    }
    /* Compare the original HID string, not the truncated/lossy display string.
       libusb's ASCII conversion can replace Unicode with '?'; such identities
       cannot safely be used to merge interfaces. Missing serials stay separate. */
    size_t index = 0;
    while (bulk->serial_number[index])
    {
        unsigned char character = (unsigned char)bulk->serial_number[index];
        if (character < 0x20 || character >= 0x7F || character == '?' ||
            hid->serial_number[index] != character)
        {
            return 0;
        }
        index++;
    }
    return hid->serial_number[index] == L'\0';
}

static int enumerate_ports(CmsisDapPortList *list)
{
    CmsisDapPort **tail;
    memset(list, 0, sizeof(*list));
    tail = &list->ports;

    /* Bulk comes first. Only a bulk interface that the backend can inspect
       suppresses its HID counterpart, so HID remains available without WinUSB
       or USB permissions. Enumeration does not claim either interface. */
    if (initialize_libusb() == 0)
    {
        ssize_t count = libusb_get_device_list(usb_context, &list->usb_devices);
        for (ssize_t index = 0; index < count; index++)
        {
            CmsisDapBulkInfo info;
            if (!inspect_bulk_device(list->usb_devices[index], &info))
            {
                continue;
            }
            CmsisDapPort *port = calloc(1, sizeof(*port));
            if (!port)
            {
                goto fail;
            }
            port->transport_kind = CMSIS_DAP_TRANSPORT_BULK;
            port->bulk = info;
            *tail = port;
            tail = &port->next;
        }
    }
    if (initialize_hid() == 0)
    {
        list->hid_devices = hid_enumerate(0, 0);
        for (struct hid_device_info *hid = list->hid_devices; hid; hid = hid->next)
        {
            if (!is_cmsis_dap(hid))
            {
                continue;
            }
            CmsisDapPort *port;
            for (port = list->ports; port; port = port->next)
            {
                if (port->transport_kind == CMSIS_DAP_TRANSPORT_BULK &&
                    same_probe(&port->bulk, hid))
                {
                    break;
                }
            }
            if (port)
            {
                continue;
            }
            port = calloc(1, sizeof(*port));
            if (!port)
            {
                goto fail;
            }
            port->transport_kind = CMSIS_DAP_TRANSPORT_HID;
            port->hid = hid;
            *tail = port;
            tail = &port->next;
        }
    }
    return 0;

fail:
    free_port_list(list);
    return -1;
}

static CmsisDapPort *port_at(const CmsisDapPortList *list, unsigned int index)
{
    CmsisDapPort *port = list->ports;
    while (port && index)
    {
        port = port->next;
        index--;
    }
    return port;
}

static int open_bulk_port(const CmsisDapBulkInfo *selected, JTAGClientData *client)
{
    libusb_device_handle *handle = NULL;
    if (libusb_open(selected->device, &handle) != 0)
    {
        return -1;
    }
    (void)libusb_set_auto_detach_kernel_driver(handle, 1);
    if (libusb_claim_interface(handle, selected->interface_number) != 0)
    {
        libusb_close(handle);
        return -1;
    }
    if (selected->alternate_setting &&
        libusb_set_interface_alt_setting(handle, selected->interface_number,
                                         selected->alternate_setting) != 0)
    {
        libusb_release_interface(handle, selected->interface_number);
        libusb_close(handle);
        return -1;
    }
    client->usb_handle = handle;
    client->usb_interface = selected->interface_number;
    client->usb_endpoint_in = selected->endpoint_in;
    client->usb_endpoint_out = selected->endpoint_out;
    client->usb_endpoint_size = selected->endpoint_size;
    client->packet_size = CMSIS_DAP_MAX_PACKET_SIZE / 2U;
    snprintf(client->serial_number, sizeof(client->serial_number), "%s", selected->serial_number);
    snprintf(client->device_path, sizeof(client->device_path), "usb-%03u:%03u",
             libusb_get_bus_number(selected->device), libusb_get_device_address(selected->device));
    return 0;
}

static uint32_t configure_device(JTAGClientData *client)
{
    unsigned char request[8];
    unsigned char response[CMSIS_DAP_MAX_PACKET_SIZE];
    size_t response_size = 0;

    /* CMSIS-DAP reports its logical packet size through DAP_Info. HID probes
       normally use 64 bytes; bulk probes commonly use 512 bytes. */
    request[0] = 0x00;
    request[1] = 0xFF;
    if (cmsis_dap_command(client, request, 2, response, sizeof(response), &response_size) != 0 ||
        response_size < 4 || response[1] != 2)
    {
        return 1;
    }
    uint32_t packet_size = (uint32_t)response[2] | ((uint32_t)response[3] << 8);
    /* A packet must hold at least one complete 64-bit JTAG sequence. */
    if (packet_size < 11 || packet_size > CMSIS_DAP_MAX_PACKET_SIZE)
    {
        return 1;
    }
    client->packet_size = packet_size;

    request[0] = CMSIS_DAP_COMMAND_CONNECT;
    request[1] = CMSIS_DAP_CONNECT_JTAG;
    if (cmsis_dap_command(client, request, 2, response, sizeof(response), &response_size) != 0 ||
        response_size < 2 || response[1] != CMSIS_DAP_CONNECT_JTAG)
    {
        return 1;
    }

    request[0] = CMSIS_DAP_COMMAND_JTAG_CONFIG;
    request[1] = 0; /* The adapter does not need a preconfigured Arm JTAG chain. */
    if (cmsis_dap_command(client, request, 2, response, sizeof(response), &response_size) != 0 ||
        response_size < 2 || response[1] != CMSIS_DAP_STATUS_OK)
    {
        return 1;
    }

    if (cmsis_dap_set_clock(client, client->jtag_clock) != 0)
    {
        return 1;
    }
    return 0;
}

JTAGClientData *cmsis_dap_device_open(unsigned int index, JTAGServerOperations *server_operations,
                                      void *server_instance)
{
    CmsisDapPortList list;

    if (enumerate_ports(&list) != 0)
    {
        return NULL;
    }
    CmsisDapPort *selected = port_at(&list, index);
    JTAGClientData *client = selected ? calloc(1, sizeof(*client)) : NULL;
    if (!client)
    {
        free_port_list(&list);
        return NULL;
    }
    client->server_operations = server_operations;
    client->server_instance = server_instance;
    client->transport_kind = selected->transport_kind;
    client->device_index = index;
    client->jtag_clock = 10000000U;
    if (selected->transport_kind == CMSIS_DAP_TRANSPORT_BULK)
    {
        if (open_bulk_port(&selected->bulk, client) != 0)
        {
            free_port_list(&list);
            goto fail;
        }
    }
    else
    {
        client->device_handle = hid_open_path(selected->hid->path);
        if (!client->device_handle)
        {
            free_port_list(&list);
            goto fail;
        }
        hid_set_nonblocking(client->device_handle, 0);
        client->packet_size = CMSIS_DAP_DEFAULT_PACKET_SIZE;
        snprintf(client->device_path, sizeof(client->device_path), "%s", selected->hid->path);
        wide_to_ascii(client->serial_number, sizeof(client->serial_number),
                      selected->hid->serial_number);
    }
    free_port_list(&list);
    client->clock_setting =
        find_clock_setting(client->serial_number[0] ? client->serial_number : client->device_path);
    if (!client->clock_setting)
    {
        goto fail;
    }
    client->jtag_clock = client->clock_setting->jtag_clock;
    client->auto_adjust = client->clock_setting->auto_adjust;

    if (configure_device(client) != 0)
    {
        goto fail;
    }
    return client;

fail:
    if (client->usb_handle)
    {
        libusb_release_interface(client->usb_handle, client->usb_interface);
        libusb_close(client->usb_handle);
    }
    else if (client->device_handle)
    {
        hid_close(client->device_handle);
    }
    free(client);
    return NULL;
}

JtagBool cmsis_dap_scan_ports(unsigned int index, char *port_name, int size)
{
    CmsisDapPortList list;
    int length;

    if (!port_name || size <= 0)
    {
        return 0;
    }
    port_name[0] = '\0';
    if (enumerate_ports(&list) != 0)
    {
        return 0;
    }
    CmsisDapPort *port = port_at(&list, index);
    if (!port)
    {
        free_port_list(&list);
        return 0;
    }
    if (port->transport_kind == CMSIS_DAP_TRANSPORT_BULK)
    {
#ifdef _WIN32
        const char *transport_name = "WinUSB";
#else
        const char *transport_name = "USB bulk";
#endif
        length = snprintf(port_name, (size_t)size, "%02u CMSIS-DAP %s %04X:%04X %s", index,
                          transport_name, port->bulk.vendor_id, port->bulk.product_id,
                          port->bulk.product[0] ? port->bulk.product : "probe");
    }
    else
    {
        char product[64];
        wide_to_ascii(product, sizeof(product), port->hid->product_string);
        length =
            snprintf(port_name, (size_t)size, "%02u CMSIS-DAP HID %04X:%04X %s", index,
                     port->hid->vendor_id, port->hid->product_id, product[0] ? product : "probe");
    }
    free_port_list(&list);
    if (length < 0 || length >= size)
    {
        port_name[0] = '\0';
        return 0;
    }
    return 1;
}

uint32_t cmsis_dap_open_hardware(JTAGClientData **client, char *device_name,
                                 JTAGServerOperations *server_operations, void *server_instance)
{
    if (!client)
    {
        return 0;
    }
    *client = NULL;
    if (!device_name || !isdigit((unsigned char)device_name[0]))
    {
        return 0;
    }
    char *end;
    errno = 0;
    unsigned long device_index = strtoul(device_name, &end, 10);
    if (errno == ERANGE || device_index > UINT_MAX || (*end && !isspace((unsigned char)*end)))
    {
        return 0;
    }
    *client = cmsis_dap_device_open((unsigned int)device_index, server_operations, server_instance);
    return *client != NULL;
}

uint32_t cmsis_dap_init_driver(JTAGClientData **client, char *device_name,
                               JTAGServerOperations *server_operations, void *server_instance)
{
    // Unlike the legacy cmsis_dap_open_hardware boolean, InitDriver returns zero
    // on success. jtagd uses 0x56 when its legacy open path fails.
    return cmsis_dap_open_hardware(client, device_name, server_operations, server_instance)
               ? 0
               : JTAG_OPEN_FAILED;
}

uint32_t cmsis_dap_close_hardware(JTAGClientData *client)
{
    if (client)
    {
        if (client->device_handle || client->usb_handle)
        {
            unsigned char request[] = {CMSIS_DAP_COMMAND_DISCONNECT};
            unsigned char response[CMSIS_DAP_MAX_PACKET_SIZE];
            size_t response_size;
            (void)cmsis_dap_command(client, request, sizeof(request), response, sizeof(response),
                                    &response_size);
            if (client->transport_kind == CMSIS_DAP_TRANSPORT_BULK)
            {
                libusb_release_interface(client->usb_handle, client->usb_interface);
                libusb_close(client->usb_handle);
            }
            else
            {
                hid_close(client->device_handle);
            }
        }
        free(client);
    }
    return 0;
}

JtagBool cmsis_dap_has_supported_device(void)
{
    CmsisDapPortList list;
    if (enumerate_ports(&list) != 0)
    {
        return 0;
    }
    int found = list.ports != NULL;
    free_port_list(&list);
    return found;
}
