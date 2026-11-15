# Quartus integration

The project implements a third-party hardware plugin for Quartus's JTAG server.
The callback ABI follows
[mpsse-blaster](https://github.com/SergeyKharenko/mpsse-blaster) and inspection of
Quartus behavior; it is not a published Intel hardware-driver SDK. Treat changes
to the operation-table layout as compatibility changes.

## Loading and ownership

```text
Quartus Programmer / jtagconfig
    -> JTAG server (jtagd)
    -> get_supported_hardware / JtagClientOperations
    -> CMSIS-DAP JTAG sequence buffering
    -> hidapi / HID, or libusb / USB bulk
    -> CMSIS-DAP probe / target JTAG TAP
```

`jtagd` loads the plugin from Quartus's platform library directory
through `get_supported_hardware`. Index 0 returns the operation table when a
supported adapter is present; other indexes return null.

`ScanPorts` enumerates adapters; `OpenHardware` creates a plugin-owned handle.
Clock callbacks queue `DAP_JTAG_Sequence` commands, and `Flush` transfers them and
returns TDO through the server callbacks. `CloseHardware` releases the handle.
`cmsis_dap_scan` uses the same interface directly, without Quartus. Handles and
the settings cache require serialized access.

### Transport and device selection

| Interface | Host library | Windows driver |
| --- | --- | --- |
| CMSIS-DAP v1 HID | hidapi | System HID driver |
| CMSIS-DAP v2 bulk | libusb 1.0 | WinUSB |

HID discovery checks vendor usage pages `0xFF00` and `0xFF01` together with known
VID/PID pairs or CMSIS-DAP/DAPLink identity strings. The same bulk transport code
runs on Linux and Windows. On Windows, libusb's
backend calls WinUSB; the plugin does not call the WinUSB API directly.

Bulk discovery requires a vendor-specific interface with class/subclass/protocol
`0xFF/0x00/0x00`. Its endpoints must be command Bulk OUT, response Bulk IN and,
optionally, a third Bulk IN endpoint for SWO. This project uses the command and
response endpoints for JTAG; it does not implement SWO capture.

The plugin lists bulk interfaces first. A HID interface with the same VID/PID
and an identical, nonempty ASCII serial number that can be compared without
loss is merged into the bulk entry. If the bulk interface cannot be enumerated
or inspected, HID remains available. Missing or ambiguous serials leave the
interfaces separate. Use unique probe serial numbers when connecting multiple
adapters.

Discovery, scanning and opening share this selection logic. Discovery does not
claim the bulk interface. A later claim or initialization failure returns an
open error; it does not switch the selected cable to HID. Port numbers describe
the current enumeration and can change when probes are connected or removed.

## Deployment

Build the [plugin and scanner](../README.md#building) and configure
[USB access](../README.md#usb-access) before using the adapter with Quartus.
The standalone scanner can check the connection before plugin deployment.

Finish clients using the adapter and stop the relevant JTAG server before
replacing a loaded plugin. Back up the existing plugin if one is present.

On Linux:

```bash
cp export/libjtag_hw_cmsis_dap.so /path/to/quartus/linux64/
```

Make the linked hidapi and libusb shared libraries available to the server
through the system loader's search paths.

On Windows, from PowerShell:

```powershell
Copy-Item .\export\jtag_hw_cmsis_dap.dll "C:\path\to\quartus\bin64\"
```

Deploy the runtime dependencies described below, then restart the relevant
Quartus tools and JTAG server. Run that installation's `jtagconfig` or select
**JTAG-CMSIS-DAP-Blaster** in **Programmer > Hardware Setup**.

Building or running `cmake --install` with a separate prefix does not deploy the
plugin into Quartus. The `export/` directory contains the plugin and scanner;
the current build does not copy dependency DLLs into it.

### Windows runtime libraries

libusb is an application library; WinUSB is the Windows device driver. Providing
one does not replace the other.

Both production targets link hidapi and libusb. If these are DLL builds, both
runtime DLLs are needed even when only a HID probe is connected:

| Dependency | Typical runtime file | Placement |
| --- | --- | --- |
| libusb 1.0 | `libusb-1.0.dll` | Beside `cmsis_dap_scan.exe` and in the Quartus JTAG server's DLL search path |
| hidapi | `hidapi.dll` | Beside `cmsis_dap_scan.exe` and in the Quartus JTAG server's DLL search path |

Use the DLLs matching the libraries used at build time. For Quartus, a usual
location is beside `jtagserver.exe` in the installation's `bin64` directory;
confirm the location of the server process actually in use. For the scanner,
place the DLLs in `export/` beside the executable. These libraries need no
separate installer or DLL registration.

Use x86_64 dependencies with the x86_64 plugin and server. From a Visual Studio
developer shell, inspect the required DLL names with:

```powershell
dumpbin /DEPENDENTS .\export\jtag_hw_cmsis_dap.dll
dumpbin /DEPENDENTS .\export\cmsis_dap_scan.exe
```

Linking static builds of hidapi and libusb removes their corresponding DLL
requirements. Set `HIDAPI_LIBRARY` and `LIBUSB_LIBRARY` to the actual static
libraries and use a compatible MSVC runtime configuration. A DLL import `.lib`
does not contain the library implementation. The current CMake configuration
does not force static linking. MSVC defaults to `/MT` (`/MTd` in Debug), matching
`mpsse-blaster`; select a compatible `CMAKE_MSVC_RUNTIME_LIBRARY` when using
static dependencies built with `/MD`.

### Windows driver binding

CMSIS-DAP v1 uses the system HID driver. For CMSIS-DAP v2, the bulk interface
must be bound to WinUSB. Windows supplies WinUSB; the project needs neither a
separately distributed `winusb.dll` nor a direct `winusb.lib` link.

Firmware with WCID/Microsoft OS descriptors normally binds WinUSB automatically
on Windows 8 and later. Otherwise use the probe vendor's driver package or
[Zadig](https://zadig.akeo.ie/) to bind only the v2 bulk interface. Preserve the
HID, serial and mass-storage interfaces of a composite device.

The official
[CMSIS-DAP v2 INF template](https://github.com/ARM-software/CMSIS-DAP/blob/main/Firmware/Template/CMSIS_DAP_v2.inf)
uses device interface GUID `{CDB3B5AD-293B-4663-AA36-1AAE46463776}`. Adapt any INF
to the actual hardware and interface identifiers; the template's VID/PID is an
example.

## Clock parameters

`cmsis_dap_clock.c` implements `DAP_SWJ_Clock` requests and the `SetParam`,
`GetParam` and `GetParamBlock` callbacks. Device initialization and automatic
adjustment use the same clock-setting function.

`JtagClock` accepts a nonzero 32-bit frequency request in Hz and sends it through
`DAP_SWJ_Clock`. The default is 10 MHz. Probe firmware determines the accepted
range and the physical frequency generated. The plugin stores the last
accepted request; readback is not a physical frequency measurement. See the
[README commands](../README.md#quartus-integration).

A manual rate change flushes queued clocks first. Settings survive close/reopen
within the same server process, keyed by serial number or device path when no
serial is available. Serial-less bulk probes use their USB bus/address path,
which can change on reconnect. Restarting the server restores 10 MHz with
adjustment disabled; no settings file or probe flash is written.

### Automatic adjustment

`JtagClockAutoAdjust` accepts `0` (off, default) or `1` (on). Use Hardware Setup's
**Auto-adjust frequency at chain scanning** checkbox or the
[README commands](../README.md#quartus-integration).

The ABI has no scan callback, so calibration runs after at least five
consecutive TMS-high clocks, at TAP reset. Enabling the switch generates no
clocks. All TAP resets can trigger calibration, including those outside chain
scanning. Calibration runs at the fifth high clock, once per uninterrupted
TMS-high run; a TMS-low clock rearms it.

Automatic adjustment has a maximum requested clock of **40 MHz**. It tries the
following candidates from highest to lowest:

```text
40, 30, 20, 15, 10, 8, 5, 4, 2, 1, 0.5, 0.1 MHz
```

The plugin first builds a stable reference at 1 MHz, falling back to 100 kHz or
10 kHz. It skips rejected candidates and those at or below the reference. Each
candidate must match five consecutive reads; otherwise it falls back to the next
rate.
If none passes, it keeps the reference rate. If no reference is stable, it
restores the previous rate and lets the normal scan diagnose the chain. A
failure to restore the previous rate fails calibration. Only a successful
selection updates the retained setting.

These are requested clock limits. Probe firmware may map several requests to
the same physical rate, and a successful calibration does not measure TCK.

Calibration supports up to 128 reset-selected IDCODE/BYPASS devices, matching
the reference project's bound. It reads 516 bytes, including room for 32
terminating ones after 128 IDCODEs, and splits the scan into packets that fit
the probe. Calibration rejects stuck high/low TDO and leaves the TAP in reset.
Probe TDO is never delivered to Quartus.
Transport failures during calibration are latched until reopen. Calibration
adds scan latency and checks chain communication, not programming stability or
RTCK adaptive clocking.

Disable adjustment before choosing a manual rate; otherwise the next reset can
change it.

## Callback reference

`include/quartus_abi.h` defines the x86_64 operation tables. The server table is
48 bytes and the client table is 208 bytes, with `Delay` at offset `0x80` and
`InitDriver` at `0xB8`. Compile-time checks in `src/quartus_plugin.c` protect
these layouts. Scalar parameter values and block lengths are 32 bits on both
platforms; the client allocation is opaque to callers.

| Callback | Behavior |
| --- | --- |
| `ScanPorts` | Enumerate merged bulk/HID ports and return a bounded name containing the current port index |
| `OpenHardware` | Legacy initialization; nonzero means success |
| `InitDriver` | Same initialization, with zero for success and `0x56` for failure |
| `CloseHardware` | Send `DAP_Disconnect`, release the HID or USB handle, and free the client |
| `ClockRaw` / `ClockMultiple` | Queue constant-TDI clocks or a TDI bitstream, with constant TMS |
| `Flush` | Execute queued JTAG sequences and deliver TDO through `JTAGServerOperations` |
| `SetParam` / `GetParam` | Set/read `JtagClock` and the `JtagClockAutoAdjust` switch |
| `Delay` | Flush, then wait in microseconds without generating TCK; Windows rounds the wait upward to milliseconds |
| `GetParamBlock` | Read `SerialNumber`; input length is capacity, output length is bytes copied, with no NUL terminator |

Unknown parameter names, zero or firmware-rejected clock requests, and
adjustment values other than `0` or `1` return `0x2D`. Transport failures use the
operation's error result rather than the unsupported-parameter result.

The recirculation callbacks remain null so the server can use its software
fallback. `DirectControl` and `EnableActiveSerial` remain null because this
implementation provides JTAG sequences without nCE/nCS/ASDO control.
`SetParamBlock` and `RemoveDriver` remain null because the plugin has no writable
block parameters or persistent driver configuration.

## Failure handling

Initialization queries `DAP_Info` for the packet size, connects in JTAG mode,
sends `DAP_JTAG_Configure` with no preconfigured Arm chain, and applies the
retained clock request. Missing or malformed packet-size replies and failures
in JTAG setup stop initialization and release the handle. The reported packet
size must fit one 64-bit JTAG sequence and stay within the 1024-byte host limit.

Command transfers check write completion, command identity and response bounds.
JTAG operations also check command status and the returned TDO length. After a
transport failure during queued JTAG operations, clock changes or calibration,
the operation fails and the error remains latched until close/reopen. Failed
flushes discard pending sequences and do not deliver their TDO.

HID replies may fill the entire report, including padding after the command's
payload. Response buffers accommodate that report. Unnumbered reports begin
with the command byte, including zero for `DAP_Info`; a leading zero is only
removed when an extra report-ID byte is present. Bulk transfers use the OUT
endpoint's packet size for zero-length packet handling. Opening a bulk port
selects the discovered alternate setting when it is nonzero.

After clients close their handles, library unload on Linux or process cleanup
on Windows releases cached settings and initialized hidapi/libusb resources.

| Symptom | Check |
| --- | --- |
| Plugin or scanner fails to load | Required runtime libraries, matching x86_64 builds and the active JTAG server installation |
| No cable appears | USB enumeration, identity strings or supported VID/PID, permissions, plugin location and loaded server instance |
| Only HID appears on a dual-interface probe | WinUSB binding, bulk descriptor access and Linux USB permissions |
| One probe appears twice | Missing or ambiguous serial numbers, or differing VID/PID or serial strings between interfaces |
| Cable cannot be opened | Another process using the probe, USB permissions, WinUSB binding, JTAG support and the requested clock |
| Chain scan fails | TCK setting, signal levels, common ground, wiring and target power |
| Clock readback or scan results change | Automatic adjustment, firmware clock handling and whether the JTAG server restarted |
| A transport error remains after reconnecting | Close and reopen the adapter to clear the latched error |

Windows runtime, physical USB transfers and Quartus/JTAG operation still need
validation with actual probes and targets.
