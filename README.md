# cmsis-dap-blaster

A C11 JTAG adapter implementation for CMSIS-DAP probes. The project builds a
Quartus hardware plugin and `cmsis_dap_scan`, a standalone IDCODE scanner.

Probes must implement CMSIS-DAP JTAG commands and expose TCK, TMS, TDI and TDO.
Build targets are Linux and Windows on x86_64. Both binaries link hidapi for v1
HID and libusb for v2 bulk; on Windows, libusb accesses the WinUSB driver.
SWD-only probes cannot be used. DAPLink is one CMSIS-DAP firmware implementation,
not a requirement for this project.

## Project layout

```text
include/        Quartus ABI definitions and internal module declarations
src/            Device management, USB transport, clocks and JTAG buffering
tool/           Standalone cmsis_dap_scan application
docs/           Quartus integration and callback reference
```

The public ABI is in `include/quartus_abi.h`; implementation types are in
`include/cmsis_dap_internal.h`. See [CONTRIBUTING.md](CONTRIBUTING.md) for module
responsibilities and style.

## Build requirements

- CMake 3.21 or later.
- An x86_64 C11 compiler: GCC/Clang on Linux or Visual Studio 2022 on Windows.
- [hidapi](https://github.com/libusb/hidapi) headers and library. Linux builds
  accept either `hidapi-hidraw` or `hidapi-libusb`.
- [libusb 1.0](https://libusb.info/) headers and library.

Install the development packages for your platform, or set `CMSIS_DAP_ROOT`
to an external dependency installation prefix. CMake searches system locations
by default and checks an explicitly supplied prefix first. Use libraries built
for the same architecture and compiler as the project. Static builds are also
accepted; shared builds require their runtime libraries at deployment.
Windows hardware access still requires the appropriate HID or WinUSB driver.

## Building

Run commands from the repository root.

### Linux

Install the hidapi and libusb development packages for your distribution, or
provide an external dependency prefix, then build:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

### Windows

From PowerShell with Visual Studio's C/C++ build tools installed:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --parallel
```

### Targets and options

| Target | Linux output | Windows output |
| --- | --- | --- |
| `jtag_hw_cmsis_dap` | `export/libjtag_hw_cmsis_dap.so` | `export/jtag_hw_cmsis_dap.dll` |
| `cmsis_dap_scan` | `export/cmsis_dap_scan` | `export/cmsis_dap_scan.exe` |

| CMake option | Default | Purpose |
| --- | --- | --- |
| `BUILD_PLUGIN` | `ON` | Build the Quartus plugin |
| `BUILD_SCAN_TOOL` | `ON` | Build `cmsis_dap_scan` |
| `CMSIS_DAP_ROOT` | Empty (system locations) | Optional prefix for hidapi and libusb dependencies |
| `CMSIS_DAP_OUTPUT_DIRECTORY` | `export/` | Select the production binary output directory |

For example, build only the scanner with dependencies outside the repository:

```bash
cmake -S . -B build -DBUILD_PLUGIN=OFF -DCMSIS_DAP_ROOT=/path/to/dependencies
cmake --build build --target cmsis_dap_scan --parallel
```

If the headers and libraries are stored separately, set `HIDAPI_INCLUDE_DIR`,
`HIDAPI_LIBRARY`, `LIBUSB_INCLUDE_DIR` and `LIBUSB_LIBRARY` explicitly. Include
directories must contain `hidapi.h` and `libusb.h` respectively; library variables
name complete library paths. Debug and Release builds share the default output
directory; use separate `CMSIS_DAP_OUTPUT_DIRECTORY` values to keep both.

MSVC defaults to the static runtime (`/MT`, or `/MTd` in Debug), as in
`mpsse-blaster`. For static dependencies, match their runtime configuration; set
`CMAKE_MSVC_RUNTIME_LIBRARY` explicitly when using dependencies built for `/MD`.

## Installation

### USB access

Both `cmsis_dap_scan` and the Quartus plugin need access to the probe.
On Linux, install the provided access rule. If you installed the previous
`99-cmsis-dap-blaster.rules`, remove it from `/etc/udev/rules.d/` first:

```bash
sudo cp 60-cmsis-dap-blaster.rules /etc/udev/rules.d/
sudo udevadm control --reload-rules
sudo udevadm trigger
```

Reconnect the adapter. The rule follows the generic CMSIS-DAP matching in
[OpenOCD](https://github.com/openocd-org/openocd/blob/master/contrib/60-openocd.rules),
matching product strings containing `CMSIS-DAP`, with an additional `DAPLink`
match for this project. It covers USB, tty and hidraw nodes, using mode `0660`
and `uaccess` to grant the active local session access through ACLs, without
requiring a `plugdev` group. SSH-only sessions and system services need a
separate access rule appropriate to their user or group.
The `60-` prefix lets `uaccess` take effect before `73-seat-late.rules` runs.
Probes identified only by their interface string or VID/PID need a
device-specific rule; OpenOCD also provides rules for many known adapters.

On Windows, v1 uses the system HID driver and v2 uses WinUSB. Firmware with
WCID/Microsoft OS descriptors normally binds WinUSB automatically. Otherwise use
the probe vendor's driver package or [Zadig](https://zadig.akeo.ie/) for the v2
bulk interface. See [Windows driver binding](docs/quartus-integration.md#windows-driver-binding)
for composite-device handling.

### Quartus plugin

To use the adapter with Quartus, follow the
[plugin deployment instructions](docs/quartus-integration.md#deployment).
The standalone scanner can run after USB access is configured.

DLL builds of hidapi and libusb also require their runtime DLLs. See
[Windows runtime libraries](docs/quartus-integration.md#windows-runtime-libraries)
for deployment paths and static-linking details.

### Installation prefix

An optional install prefix places the plugin under `lib` or `lib64`, the scanner
under `bin`, and the Linux udev rule under `share/cmsis-dap-blaster`:

```bash
cmake --install build --config Release --prefix /path/to/install
```

Building or installing to a separate prefix does not deploy the Quartus plugin.

## Using cmsis_dap_scan

Connect the probe's TCK, TMS, TDI, TDO and ground to the target. Use the probe's
pinout and compatible signal levels; connect its target voltage reference if
required.

After configuring [USB access](#usb-access), run on Linux:

```bash
./export/cmsis_dap_scan
```

On Windows, run `.\export\cmsis_dap_scan.exe`.

The scanner resets the TAP and reads the first device's default 32-bit IDCODE.
Press Enter to repeat, or Ctrl+C to exit. EOF exits after the current scan, so
`./export/cmsis_dap_scan </dev/null` performs one scan on Linux. Devices whose reset
instruction selects BYPASS instead of IDCODE need a different scan sequence.

## Quartus integration

Follow the [deployment guide](docs/quartus-integration.md#deployment), then select
`JTAG-CMSIS-DAP-Blaster` in Programmer or enumerate it with `jtagconfig`. This is a
third-party plugin using an unpublished Quartus hardware-driver ABI.

TCK defaults to **10 MHz**. Probe firmware determines the accepted requests and
the physical rate generated. `JtagClock` readback reports the accepted request,
not a physical frequency measurement. Using the cable number returned by
`jtagconfig`, select a manual rate:

```bash
jtagconfig --setparam 1 JtagClockAutoAdjust 0
jtagconfig --setparam 1 JtagClock 10000000
jtagconfig --getparam 1 JtagClock
```

For automatic adjustment, check **Auto-adjust frequency at chain scanning**
in Hardware Setup, or run:

```bash
jtagconfig --setparam 1 JtagClockAutoAdjust 1
jtagconfig
jtagconfig --getparam 1 JtagClock
```

At TAP reset, the plugin selects the highest candidate request, up to **40 MHz**,
that matches repeated low-speed chain reads. Adjustment defaults to off; disable
it to keep a manual rate. Settings last until the JTAG server restarts. See the
[integration guide](docs/quartus-integration.md#automatic-adjustment) for probe
rates and supported chains. Chain scans do not establish programming stability.

## License and origin

Project sources use **GPL-3.0-only**; see [LICENSE](LICENSE). hidapi and libusb
have their own licenses and are not included in this repository.

Based on [SergeyKharenko/mpsse-blaster](https://github.com/SergeyKharenko/mpsse-blaster),
which derives from [tow3rs/jtag-mpsse-blaster](https://github.com/tow3rs/jtag-mpsse-blaster),
originally developed by [tow3rs](https://github.com/tow3rs). Maintained by
Sergey Kharenko.
