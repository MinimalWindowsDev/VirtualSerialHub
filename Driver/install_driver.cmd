@echo off
setlocal EnableDelayedExpansion

::=============================================================================
:: install_driver.cmd - Installation helper for VirtualSerial driver
::
:: Part of VirtualSerialHub - FLOSS alternative to com0com
:: License: MIT
::
:: Usage:
::   install_driver.cmd install     - Install and start the driver
::   install_driver.cmd uninstall   - Stop and remove the driver
::   install_driver.cmd start       - Start the driver service
::   install_driver.cmd stop        - Stop the driver service
::   install_driver.cmd status      - Show driver status
::   install_driver.cmd testsign    - Enable test signing (requires reboot)
::=============================================================================

set "DRIVER_NAME=VirtualSerial"
set "DRIVER_PATH=%~dp0build\x64\release\%DRIVER_NAME%.sys"

:: Check for admin rights
net session >nul 2>&1
if errorlevel 1 (
    echo [ERROR] This script requires Administrator privileges.
    echo         Right-click and select "Run as administrator"
    exit /b 1
)

if "%~1"=="" goto :usage

if /i "%~1"=="install" goto :install
if /i "%~1"=="uninstall" goto :uninstall
if /i "%~1"=="start" goto :start
if /i "%~1"=="stop" goto :stop
if /i "%~1"=="status" goto :status
if /i "%~1"=="testsign" goto :testsign

:usage
echo.
echo VirtualSerial Driver Installation Helper
echo ========================================
echo.
echo Usage: %~nx0 ^<command^>
echo.
echo Commands:
echo   install     Install and start the driver
echo   uninstall   Stop and remove the driver
echo   start       Start the driver service
echo   stop        Stop the driver service
echo   status      Show driver status
echo   testsign    Enable test signing mode (requires reboot)
echo.
exit /b 0

:install
echo [INFO] Installing %DRIVER_NAME% driver...

:: Check if driver file exists
if not exist "%DRIVER_PATH%" (
    echo [ERROR] Driver not found: %DRIVER_PATH%
    echo         Run build_driver.cmd first.
    exit /b 1
)

:: Copy driver to System32\drivers
echo [INFO] Copying driver to System32\drivers...
copy /y "%DRIVER_PATH%" "%SystemRoot%\System32\drivers\%DRIVER_NAME%.sys" >nul
if errorlevel 1 (
    echo [ERROR] Failed to copy driver
    exit /b 1
)

:: Create service
echo [INFO] Creating kernel service...
sc create %DRIVER_NAME% type= kernel binPath= "%SystemRoot%\System32\drivers\%DRIVER_NAME%.sys" start= demand >nul 2>&1
if errorlevel 1 (
    :: Service might already exist, try to update it
    sc config %DRIVER_NAME% binPath= "%SystemRoot%\System32\drivers\%DRIVER_NAME%.sys" >nul 2>&1
)

:: Start service
echo [INFO] Starting driver...
sc start %DRIVER_NAME%
if errorlevel 1 (
    echo [ERROR] Failed to start driver. Check Event Viewer for details.
    echo         You may need to enable test signing: %~nx0 testsign
    exit /b 1
)

echo.
echo [SUCCESS] Driver installed and started.
echo           Virtual ports available: \\.\VCOM0 and \\.\VCOM1
echo.
goto :eof

:uninstall
echo [INFO] Uninstalling %DRIVER_NAME% driver...

:: Stop service if running
sc query %DRIVER_NAME% 2>nul | findstr "RUNNING" >nul
if not errorlevel 1 (
    echo [INFO] Stopping driver...
    sc stop %DRIVER_NAME% >nul 2>&1
    timeout /t 2 /nobreak >nul
)

:: Delete service
echo [INFO] Removing service...
sc delete %DRIVER_NAME% >nul 2>&1

:: Remove driver file
echo [INFO] Removing driver file...
del /f "%SystemRoot%\System32\drivers\%DRIVER_NAME%.sys" >nul 2>&1

echo.
echo [SUCCESS] Driver uninstalled.
echo.
goto :eof

:start
echo [INFO] Starting %DRIVER_NAME% driver...
sc start %DRIVER_NAME%
if errorlevel 1 (
    echo [ERROR] Failed to start driver
    exit /b 1
)
echo [SUCCESS] Driver started.
goto :eof

:stop
echo [INFO] Stopping %DRIVER_NAME% driver...
sc stop %DRIVER_NAME%
if errorlevel 1 (
    echo [ERROR] Failed to stop driver
    exit /b 1
)
echo [SUCCESS] Driver stopped.
goto :eof

:status
echo.
echo %DRIVER_NAME% Driver Status
echo ==========================
sc query %DRIVER_NAME% 2>nul
if errorlevel 1 (
    echo Service not installed.
)
echo.
echo Test Signing Status:
bcdedit /enum {current} 2>nul | findstr -i "testsigning"
echo.
goto :eof

:testsign
echo [INFO] Enabling test signing mode...
echo.
echo WARNING: This will enable Windows Test Signing Mode.
echo          A watermark will appear on the desktop.
echo          A reboot is required for changes to take effect.
echo.
set /p "CONFIRM=Continue? (Y/N): "
if /i not "%CONFIRM%"=="Y" (
    echo Cancelled.
    exit /b 0
)

bcdedit /set testsigning on
if errorlevel 1 (
    echo [ERROR] Failed to enable test signing.
    echo         If Secure Boot is enabled, you may need to disable it in BIOS.
    exit /b 1
)

echo.
echo [SUCCESS] Test signing enabled. Please reboot your system.
echo.
goto :eof
