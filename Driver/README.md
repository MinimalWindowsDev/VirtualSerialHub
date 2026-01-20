# VirtualSerialHub

A minimalistic, FLOSS alternative to `com0com` providing both user-mode serial bridging and kernel-mode virtual COM port pairs.

## Features

### User-Mode (VirtualSerialHub.exe)
* **Bridge:** Relay data between two physical COM ports
* **TCP Serial:** Expose a physical COM port over TCP
* **Loopback:** Create a pure TCP echo server
* **Hex Dump:** Live traffic inspection (`--hex`)
* **Configurable:** Baud Rate, Parity, Data Bits, Stop Bits

### Kernel-Mode (VirtualSerial.sys) - NEW
* **True Virtual COM Ports:** Creates `VCOM0` and `VCOM1` as a null-modem pair
* **Visible in Device Manager:** Listed under "Ports (COM & LPT)"
* **Standard Serial API:** Works with any application expecting COM ports
* **Signal Emulation:** DTR/RTS handshaking between port pairs
* **Lightweight:** Single monolithic WDM driver (~30KB)

## Project Structure

```
VirtualSerialHub/
├── VirtualSerialHub.cs      # User-mode CLI tool (C#/.NET 4.x)
├── README.md
└── Driver/
    ├── VirtualSerial.c      # Kernel-mode WDM driver source
    ├── VirtualSerial.inf    # PnP installation file
    ├── VirtualSerial.rc     # Version resource
    ├── build_driver.cmd     # Build script (VS + WDK)
    ├── install_driver.cmd   # Installation helper
    └── Makefile             # Alternative NMAKE build
```

## Build Instructions

### User-Mode Tool (VirtualSerialHub.exe)

```cmd
C:\Windows\Microsoft.NET\Framework64\v4.0.30319\csc.exe /target:exe /out:VirtualSerialHub.exe VirtualSerialHub.cs
```

### Kernel-Mode Driver (VirtualSerial.sys)

**Prerequisites:**
* Visual Studio 2022/2026 Build Tools
* Windows Driver Kit (WDK) 10.0.26100

**Build:**
```cmd
cd Driver
build_driver.cmd release
```

Output: `Driver\build\x64\release\VirtualSerial.sys`

**Alternative (NMAKE):**
```cmd
:: Run from "x64 Native Tools Command Prompt for VS 2022"
cd Driver
nmake
```

## Installation

### Driver Installation

> **Note:** Windows requires driver signing. For development/testing, enable test signing mode first.

**1. Enable Test Signing (one-time, requires reboot):**
```cmd
:: Run as Administrator
bcdedit /set testsigning on
:: Reboot
```

**2. Install the Driver:**
```cmd
:: Run as Administrator
cd Driver
install_driver.cmd install
```

**3. Verify Installation:**
```cmd
:: Check that VCOM0 and VCOM1 are available
mode VCOM0
mode VCOM1
```

### Uninstall
```cmd
install_driver.cmd uninstall
```

## Usage

### Kernel Driver (Virtual Port Pair)

Once installed, `VCOM0` and `VCOM1` act as a null-modem pair:
- Data written to `VCOM0` is readable from `VCOM1`
- Data written to `VCOM1` is readable from `VCOM0`

**Example - Connect two terminal applications:**
```
Terminal 1: Open VCOM0 at 9600 baud
Terminal 2: Open VCOM1 at 9600 baud
Type in one terminal → appears in the other
```

**Example - Use with VirtualSerialHub for TCP bridging:**
```cmd
:: Expose VCOM0 over TCP (your app connects to VCOM1)
VirtualSerialHub.exe tcpserial VCOM0:9600 5000
```

### User-Mode Tool

```cmd
:: Interactive Mode
VirtualSerialHub.exe

:: Bridge two physical ports
VirtualSerialHub.exe bridge COM3:115200 COM4:9600,7,E,1

:: Expose COM port to TCP
VirtualSerialHub.exe tcpserial COM3:38400,8,N,1 9000

:: TCP Loopback server
VirtualSerialHub.exe loopback 9600

:: Enable hex dump
VirtualSerialHub.exe --hex
```

## Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                        User Space                                │
│  ┌─────────────┐     ┌──────────────────┐     ┌─────────────┐  │
│  │ Your App    │     │ VirtualSerialHub │     │ Your App    │  │
│  │ (COM/VCOM)  │     │ (TCP Bridge)     │     │ (TCP Client)│  │
│  └──────┬──────┘     └────────┬─────────┘     └──────┬──────┘  │
│         │                     │                       │         │
├─────────┼─────────────────────┼───────────────────────┼─────────┤
│         │            Kernel Space                     │         │
│         │                     │                       │         │
│         v                     v                       │         │
│  ┌──────────────────────────────────────────┐        │         │
│  │         VirtualSerial.sys                 │        │         │
│  │  ┌─────────────┐   ┌─────────────┐       │        │         │
│  │  │   VCOM0     │◄─►│   VCOM1     │       │        │         │
│  │  │ (DevExt[0]) │   │ (DevExt[1]) │       │        │         │
│  │  │  RxBuffer   │   │  RxBuffer   │       │        │         │
│  │  └─────────────┘   └─────────────┘       │        │         │
│  └──────────────────────────────────────────┘        │         │
│                                                       │         │
│  ┌─────────────────────────────────────────────────────────────┤
│  │                    TCP/IP Stack                              │
│  └─────────────────────────────────────────────────────────────┤
└─────────────────────────────────────────────────────────────────┘
```

## Supported IOCTLs

The kernel driver implements these standard serial port IOCTLs:

| IOCTL | Description |
|-------|-------------|
| `IOCTL_SERIAL_GET/SET_BAUD_RATE` | Baud rate configuration |
| `IOCTL_SERIAL_GET/SET_LINE_CONTROL` | Data bits, parity, stop bits |
| `IOCTL_SERIAL_GET/SET_TIMEOUTS` | Read/write timeouts |
| `IOCTL_SERIAL_GET_MODEMSTATUS` | CTS/DSR/DCD status (from peer) |
| `IOCTL_SERIAL_SET/CLR_DTR` | DTR signal control |
| `IOCTL_SERIAL_SET/CLR_RTS` | RTS signal control |
| `IOCTL_SERIAL_PURGE` | Clear buffers |
| `IOCTL_SERIAL_GET_COMMSTATUS` | Buffer status |
| `IOCTL_SERIAL_GET_PROPERTIES` | Port capabilities |

## Signal Mapping (Null Modem)

The driver implements standard null-modem signal crossing:

| Port A Output | Port B Input |
|---------------|--------------|
| DTR | DSR, DCD |
| RTS | CTS |

## Troubleshooting

### "Driver failed to start"
1. Ensure test signing is enabled: `bcdedit /enum {current} | findstr testsigning`
2. Check Event Viewer → Windows Logs → System for driver errors
3. Verify WDK version matches build

### "Access Denied" when opening ports
- Run your application as Administrator, or
- Adjust security descriptor in the INF file

### Building fails with "ntddk.h not found"
- Verify WDK installation path in `build_driver.cmd`
- Run from VS Developer Command Prompt

## License

MIT License - See LICENSE file

## Contributing

Contributions welcome! Please ensure:
1. Code compiles with `/W4 /WX` (warnings as errors)
2. Driver passes Driver Verifier checks
3. Test on both debug and release builds
