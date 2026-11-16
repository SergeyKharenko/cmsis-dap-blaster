# cmsis-dap-blaster

Use CMSIS-DAP probes as JTAG adapters in Quartus, or read a target's IDCODE with
the standalone `cmsis_dap_scan` tool. Supports Linux and Windows on x86_64.

Probes must implement CMSIS-DAP JTAG commands and expose TCK, TMS, TDI and TDO.
CMSIS-DAP v1 HID and v2 bulk probes are supported. SWD-only probes cannot be used.

## Build requirements

- CMake 3.21 or later.
- Git.
- Linux: GCC or Clang with C11 support, Make, Autoconf, Automake, Libtool and pkg-config.
- Windows: Visual Studio with the C++ desktop workload and Windows SDK, plus PowerShell.

## Building

Run commands from the repository root. Both scripts build **Release** by default
and download the required dependencies on the first run.

On Linux:

```bash
./build.sh
```

On Windows, from PowerShell:

```powershell
powershell -ExecutionPolicy Bypass -File .\build.ps1
```

The plugin and scanner are written to `export/`:

| Component | Linux output | Windows output |
| --- | --- | --- |
| Quartus plugin | `export/libjtag_hw_cmsis_dap.so` | `export/jtag_hw_cmsis_dap.dll` |
| IDCODE scanner | `export/cmsis_dap_scan` | `export/cmsis_dap_scan.exe` |

For custom build options, see [CONTRIBUTING.md](CONTRIBUTING.md#custom-builds).

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

Reconnect the adapter. The rule grants the active local user access to probes
identified as `CMSIS-DAP` or `DAPLink`. Other probes, SSH-only sessions and
system services may need a separate access rule.

On Windows, v1 uses the system HID driver and v2 uses WinUSB. If the v2 interface
does not get a driver automatically, use the vendor's driver package or
[Zadig](https://zadig.akeo.ie/) to bind WinUSB to that interface. Preserve the
probe's other interfaces. See the [driver guide](docs/quartus-integration.md#windows-driver-binding).

### Quartus plugin

Close clients using the adapter and stop the relevant JTAG server, then copy
the plugin into your Quartus installation.

On Linux:

```bash
cp export/libjtag_hw_cmsis_dap.so /path/to/quartus/linux64/
```

On Windows:

```powershell
Copy-Item .\export\jtag_hw_cmsis_dap.dll "C:\path\to\quartus\bin64\"
```

Restart the Quartus tools and JTAG server. For more detail, see the
[deployment guide](docs/quartus-integration.md#deployment).

## Using cmsis_dap_scan

Connect the probe's TCK, TMS, TDI, TDO and ground to the target. Use the probe's
pinout and compatible signal levels; connect its target voltage reference if
required.

After configuring [USB access](#usb-access), run on Linux:

```bash
./export/cmsis_dap_scan
```

On Windows, run `.\export\cmsis_dap_scan.exe`.

The scanner reads the first device's default 32-bit IDCODE. Press Enter to
repeat, or Ctrl+C to exit. Devices that reset to BYPASS instead of IDCODE need
a different scan sequence.

## Quartus integration

Select `JTAG-CMSIS-DAP-Blaster` in Programmer or enumerate it with `jtagconfig`.

TCK defaults to **10 MHz**. The actual rate depends on the probe firmware.
Using the cable number returned by `jtagconfig`, select a manual rate:

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

Automatic adjustment defaults to off and tries rates up to **40 MHz**. Disable
it to keep a manual rate. Settings last until the JTAG server restarts.
Successful chain scanning does not guarantee reliable programming; lower the
rate if needed. See the [integration guide](docs/quartus-integration.md#automatic-adjustment)
for details.

## License and origin

Project sources use **GPL-3.0-only**; see [LICENSE](LICENSE). hidapi and libusb
have their own licenses and are fetched as submodules during the build.

Based on [SergeyKharenko/mpsse-blaster](https://github.com/SergeyKharenko/mpsse-blaster),
which derives from [tow3rs/jtag-mpsse-blaster](https://github.com/tow3rs/jtag-mpsse-blaster),
originally developed by [tow3rs](https://github.com/tow3rs). Maintained by
Sergey Kharenko.
