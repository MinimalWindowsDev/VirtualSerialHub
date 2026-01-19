# VirtualSerialHub

A minimalistic, single-file C# CLI tool that acts as a user-mode serial bridge and TCP loopback utility. It serves as a lightweight, user-mode alternative to complex kernel-mode drivers like `com0com` for basic bridging and debugging needs.

## Features
* **Bridge:** Relay data bi-directionally between two physical COM ports.
* **TCP Serial:** Expose a physical COM port over a generic TCP socket.
* **Loopback:** Create a pure TCP echo server for testing network-serial applications.
* **Hex Dump:** Live traffic inspection with color-coded Rx/Tx logging (`--hex`).
* **Dependency Free:** Runs on standard .NET Framework 4.x using only native libraries.

## Build
Compile using the standard Windows C# compiler:

```cmd
C:\Windows\Microsoft.NET\Framework64\v4.0.30319\csc.exe /target:exe /out:VirtualSerialHub.exe VirtualSerialHub.cs

```

## Usage

Run in **Interactive Mode**:

```cmd
.\VirtualSerialHub.exe

```

Or use **Command-Line Arguments**:

```cmd
:: Bridge two physical ports
VirtualSerialHub.exe bridge COM3 COM4

:: Bridge COM port to TCP (Access COM3 via Telnet on port 9000)
VirtualSerialHub.exe tcpserial COM3 9000

:: Start a TCP Echo Server
VirtualSerialHub.exe loopback 9600

```

### Debugging

To view raw traffic bytes in real-time, use the hex flag:

```cmd
VirtualSerialHub.exe --hex

```

*Or type `hex` inside the interactive console to toggle it on/off.*
