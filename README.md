# VirtualSerialHub

A minimalistic, single-file C# CLI tool that acts as a user-mode serial bridge and TCP loopback utility. It serves as a lightweight alternative to kernel-mode drivers like `com0com`.

## Features
* **Bridge:** Relay data between two physical COM ports.
* **TCP Serial:** Expose a physical COM port over TCP.
* **Loopback:** Create a pure TCP echo server.
* **Hex Dump:** Live traffic inspection (`--hex`).
* **Configurable:** Support for custom Baud Rate, Parity, Data Bits, and Stop Bits.
* **Dependency Free:** Runs on standard .NET Framework 4.x.

## Build
```cmd
C:\Windows\Microsoft.NET\Framework64\v4.0.30319\csc.exe /target:exe /out:VirtualSerialHub.exe VirtualSerialHub.cs

```


## Usage


```cmd
:: Interactive Mode
.\VirtualSerialHub.exe

:: Bridge two ports with custom settings
VirtualSerialHub.exe bridge COM3:115200 COM4:9600,7,E,1

:: Expose COM port to TCP (Access via Telnet)
VirtualSerialHub.exe tcpserial COM3:38400,8,N,1 9000

:: Debugging (Hex Dump)
VirtualSerialHub.exe --hex

```
