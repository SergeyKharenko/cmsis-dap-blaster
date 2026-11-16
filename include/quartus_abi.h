/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2024 tow3rs
 * Copyright (C) 2026 Sergey Kharenko
 */

#ifndef CMSIS_DAP_BLASTER_QUARTUS_ABI_H
#define CMSIS_DAP_BLASTER_QUARTUS_ABI_H

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/* Keep the original callback signedness on each supported platform. */
#ifdef _WIN32
typedef int32_t JtagBool;
#else
typedef uint32_t JtagBool;
#endif

/* Owned by the plugin. Callers must use the operation table to access it. */
typedef struct JTAGClientData JTAGClientData;

typedef struct
{
    uint32_t Size;
    void (*StoreTDO)(void *, unsigned char *, uint32_t);
    void (*StoreUnknownTDO)(void *, uint32_t);
    void (*StoreNeededTDO)(void *, unsigned char *, uint32_t);
    int (*TDONeeded)(void *, int);
    void (*IndicateFlush)(void *);
} JTAGServerOperations;

typedef struct
{
    uint32_t Size;
    unsigned char Name[32];
    uint32_t Features;
    uint64_t Flags;
    void *Reserved1;
    JtagBool (*ScanPorts)(unsigned int, char *port_name, int);
    void *Reserved2;
    uint32_t (*OpenHardware)(JTAGClientData **client, char *device_name,
                             JTAGServerOperations *server_operations, void *server_instance);
    uint32_t (*CloseHardware)(JTAGClientData *client);
    // Quartus uses 32-bit parameter values on both Linux and Windows x86_64.
    uint32_t (*SetParam)(JTAGClientData *client, char *param_name, uint32_t val);
    uint32_t (*GetParam)(JTAGClientData *client, char *param_name, uint32_t *retval);
    uint32_t (*DirectControl)(JTAGClientData *client, uint32_t, uint32_t *);
    uint32_t (*ClockRaw)(JTAGClientData *client, int, int, int, int);
    uint32_t (*ClockMultiple)(JTAGClientData *client, int, unsigned char *, int, int);
    uint32_t (*Delay)(JTAGClientData *client, uint32_t microseconds);
    JtagBool (*Flush)(JTAGClientData *client, JtagBool store, int size);
    uint32_t (*EnableRecirculate)(JTAGClientData *client, uint32_t);
    uint32_t (*DisableRecirculate)(JTAGClientData *client);
    uint32_t (*Recirculate)(JTAGClientData *client, uint32_t, int, int);
    uint32_t (*SetParamBlock)(JTAGClientData *client, char *name, unsigned char *buffer,
                              uint32_t size);
    uint32_t (*GetParamBlock)(JTAGClientData *client, char *name, unsigned char *buffer,
                              uint32_t *size);
    uint32_t (*InitDriver)(JTAGClientData **client, char *device_name,
                           JTAGServerOperations *server_operations, void *server_instance);
    uint32_t (*EnableActiveSerial)(uint32_t mode); // 1 = JTAG, 2 = Active Serial
    uint32_t (*RemoveDriver)(char *);
} JtagClientOperations;

/* The only symbol loaded by Quartus. The returned table has static lifetime. */
#ifdef _WIN32
__declspec(dllexport)
#else
__attribute__((visibility("default")))
#endif
void *get_supported_hardware(int index);

#ifdef __cplusplus
}
#endif

#endif /* CMSIS_DAP_BLASTER_QUARTUS_ABI_H */
