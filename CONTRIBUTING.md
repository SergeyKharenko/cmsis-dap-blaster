# Contributing

The project uses C11, CMake and GPL-3.0-only. Keep changes focused and include a
description of the problem, the resulting behavior and the validation performed.
Preserve existing copyright notices and add an SPDX identifier to new source files.

## Code organization

| Path | Responsibility |
| --- | --- |
| `include/quartus_abi.h` | SDK-independent Quartus operation tables and opaque client handle |
| `src/quartus_plugin.c` | Operation table, ABI layout checks and exported entry point |
| `src/cmsis_dap_device.c` | HID/bulk discovery, initialization, cleanup and clock settings retention |
| `src/cmsis_dap_transport.c` | CMSIS-DAP command exchange over HID and USB bulk |
| `src/cmsis_dap_clock.c` | Clock requests and scalar/block parameters |
| `src/cmsis_dap_autotune.c` | Bounded chain probes and automatic clock selection at TAP reset |
| `src/cmsis_dap_jtag.c` | JTAG command buffering, TDO delivery and delay barriers |
| `include/cmsis_dap_internal.h` | Private types and cross-module declarations |
| `tool/cmsis_dap_scan.c` | Standalone consumer of the operation table |

Keep hidapi/libusb types and implementation details out of the public ABI
header. The Quartus table uses its existing member names and layout; private
functions and variables use `snake_case`, types use `PascalCase`, and constants use `UPPER_CASE`.
Private helpers should be `static`. Only `get_supported_hardware` is exported by
the plugin. Do not add dependencies between modules through included `.c` files.

Use `.clang-format` and `.editorconfig`. Format only project sources, not
dependency headers. Handle USB errors immediately, bound buffers, release handles
on initialization failure, and keep transport errors latched until reopen. Preserve
queued-operation ordering and serialized access to handles and settings.

The structure follows `mpsse-blaster`. Keep shared ABI, scanner, callback and
calibration logic in the same shape as the reference. Backend differences belong
in USB discovery, packet encoding and clock requests: CMSIS-DAP uses HID/bulk
commands and firmware-selected rates instead of D2XX and MPSSE dividers.

## Custom builds

The build scripts default to `Release`. Set `CMSIS_DAP_BUILD_DIR` to change the
build directory, `CMAKE_BUILD_TYPE` to select a Linux configuration, or
`CONFIGURATION` to select a Windows configuration. Rerun `build.ps1` after
changing `CONFIGURATION` so the dependency libraries match the selected runtime.

| CMake option | Default | Purpose |
| --- | --- | --- |
| `BUILD_PLUGIN` | `ON` | Build the Quartus plugin |
| `BUILD_SCAN_TOOL` | `ON` | Build `cmsis_dap_scan` |
| `CMSIS_DAP_ROOT` | `lib/<platform>-x86_64` | Prefix for hidapi and libusb dependencies |
| `CMSIS_DAP_OUTPUT_DIRECTORY` | `export/` | Output directory for the plugin and scanner |

For example, build only the scanner with a separately built dependency prefix:

```bash
cmake -S . -B build -DBUILD_PLUGIN=OFF -DCMSIS_DAP_ROOT=/path/to/dependencies
cmake --build build --target cmsis_dap_scan --parallel
```

If headers and libraries are stored separately, set `HIDAPI_INCLUDE_DIR`,
`HIDAPI_LIBRARY`, `LIBUSB_INCLUDE_DIR` and `LIBUSB_LIBRARY` explicitly. Include
directories must contain `hidapi.h` and `libusb.h` respectively; library variables
name complete library paths.

Debug and Release share the default binary output directory. Use separate
`CMSIS_DAP_OUTPUT_DIRECTORY` values in separate build directories to keep both,
or clean the targets before switching configurations in the same output directory.

MSVC defaults to the static runtime (`/MT`, or `/MTd` in Debug). Match the runtime
of static dependencies; set `CMAKE_MSVC_RUNTIME_LIBRARY` explicitly when linking
dependencies built for `/MD`. The Windows dependency script builds hidapi and
libusb with `/MTd` for Debug and `/MT` for other supported configurations.

An optional installation prefix places the plugin under `lib` or `lib64`, the
scanner under `bin`, and the Linux udev rule under `share/cmsis-dap-blaster`:

```bash
cmake --install build --config Release --prefix /path/to/install
```

Installing to a separate prefix does not deploy the plugin into Quartus. Follow
the [deployment guide](docs/quartus-integration.md#deployment) for that step.

## Validation

Build both targets using the [README instructions](README.md#building), then
check formatting:

```bash
clang-format --dry-run --Werror src/*.c include/*.h tool/*.c
```

Describe the platforms built and verification performed. Use an available
adapter and an isolated JTAG server for hardware checks; do not interrupt other
sessions. Disable automatic adjustment and restore 10 MHz afterward. Building
does not deploy the plugin, and chain-scan success does not prove programming
stability.
