@echo off
setlocal EnableDelayedExpansion

::=============================================================================
:: build_driver.cmd - Build script for VirtualSerial kernel-mode driver
::
:: Part of VirtualSerialHub - FLOSS alternative to com0com
:: License: MIT
::
:: Usage:
::   build_driver.cmd [debug|release] [clean]
::
:: Requirements:
::   - Visual Studio 2019/2022/2026 Build Tools (cl.exe, link.exe)
::   - Windows Driver Kit 10.0.26100
::
:: IMPORTANT: This script uses delayed expansion (!VAR!) for paths containing
::            parentheses like "Program Files (x86)" to avoid CMD parsing errors.
::=============================================================================

:: Configuration
set DRIVER_NAME=VirtualSerial
set WDK_VERSION=10.0.26100.0
set TARGET_ARCH=x64

:: WDK Paths - NOTE: Contains (x86) so must use !VAR! in if blocks
set "WDK_ROOT=C:\Program Files (x86)\Windows Kits\10"
set "WDK_INC=!WDK_ROOT!\Include\!WDK_VERSION!"
set "WDK_LIB=!WDK_ROOT!\Lib\!WDK_VERSION!"

:: VS Build Tools - Try to find vcvarsall.bat
:: Check VS 2026 (v18), VS 2022 (v17), VS 2019 (v16)
set "VCVARSALL="

:: VS 2026 paths
if exist "C:\Program Files\Microsoft Visual Studio\18\Enterprise\VC\Auxiliary\Build\vcvarsall.bat" (
    set "VCVARSALL=C:\Program Files\Microsoft Visual Studio\18\Enterprise\VC\Auxiliary\Build\vcvarsall.bat"
    goto :found_vcvars
)
if exist "C:\Program Files\Microsoft Visual Studio\18\Professional\VC\Auxiliary\Build\vcvarsall.bat" (
    set "VCVARSALL=C:\Program Files\Microsoft Visual Studio\18\Professional\VC\Auxiliary\Build\vcvarsall.bat"
    goto :found_vcvars
)
if exist "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat" (
    set "VCVARSALL=C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat"
    goto :found_vcvars
)
if exist "C:\Program Files\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" (
    set "VCVARSALL=C:\Program Files\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat"
    goto :found_vcvars
)

:: VS 2022 paths
if exist "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvarsall.bat" (
    set "VCVARSALL=C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvarsall.bat"
    goto :found_vcvars
)
if exist "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvarsall.bat" (
    set "VCVARSALL=C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvarsall.bat"
    goto :found_vcvars
)
if exist "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" (
    set "VCVARSALL=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat"
    goto :found_vcvars
)
if exist "C:\Program Files\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" (
    set "VCVARSALL=C:\Program Files\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat"
    goto :found_vcvars
)

:: VS 2019 paths (x86 Program Files)
if exist "C:\Program Files (x86)\Microsoft Visual Studio\2019\Enterprise\VC\Auxiliary\Build\vcvarsall.bat" (
    set "VCVARSALL=C:\Program Files (x86)\Microsoft Visual Studio\2019\Enterprise\VC\Auxiliary\Build\vcvarsall.bat"
    goto :found_vcvars
)
if exist "C:\Program Files (x86)\Microsoft Visual Studio\2019\Professional\VC\Auxiliary\Build\vcvarsall.bat" (
    set "VCVARSALL=C:\Program Files (x86)\Microsoft Visual Studio\2019\Professional\VC\Auxiliary\Build\vcvarsall.bat"
    goto :found_vcvars
)
if exist "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvarsall.bat" (
    set "VCVARSALL=C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvarsall.bat"
    goto :found_vcvars
)
if exist "C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" (
    set "VCVARSALL=C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\VC\Auxiliary\Build\vcvarsall.bat"
    goto :found_vcvars
)

echo [ERROR] Could not find vcvarsall.bat
echo         Please install Visual Studio Build Tools or set VCVARSALL manually.
echo.
echo         Searched locations:
echo           - C:\Program Files\Microsoft Visual Studio\18\*
echo           - C:\Program Files\Microsoft Visual Studio\2022\*
echo           - C:\Program Files (x86)\Microsoft Visual Studio\2019\*
exit /b 1

:found_vcvars
echo [INFO] Found VS: !VCVARSALL!

:: Parse arguments
set BUILD_CONFIG=release
set DO_CLEAN=0

:parse_args
if "%~1"=="" goto :done_args
if /i "%~1"=="debug" set BUILD_CONFIG=debug
if /i "%~1"=="release" set BUILD_CONFIG=release
if /i "%~1"=="clean" set DO_CLEAN=1
shift
goto :parse_args
:done_args

:: Set up output directory
set "OUT_DIR=%~dp0build\%TARGET_ARCH%\%BUILD_CONFIG%"

:: Clean if requested
if "!DO_CLEAN!"=="1" (
    echo [INFO] Cleaning build directory...
    if exist "%~dp0build" rmdir /s /q "%~dp0build"
    echo [INFO] Clean complete.
    if /i "!BUILD_CONFIG!"=="clean" exit /b 0
)

:: Create output directory
if not exist "!OUT_DIR!" mkdir "!OUT_DIR!"

:: Verify WDK installation BEFORE calling vcvarsall
:: CRITICAL: Use !VAR! because WDK_INC contains (x86) which breaks if ( ) blocks
set "WDK_CHECK=!WDK_INC!\km\ntddk.h"
if not exist "!WDK_CHECK!" (
    echo [ERROR] WDK not found at: !WDK_INC!\km
    echo         Please install Windows Driver Kit !WDK_VERSION!
    echo         Expected: !WDK_CHECK!
    exit /b 1
)
echo [INFO] WDK found at: !WDK_ROOT!

:: Initialize VS environment
echo [INFO] Initializing Visual Studio environment for !TARGET_ARCH!...
call "!VCVARSALL!" !TARGET_ARCH!
if errorlevel 1 (
    echo [ERROR] Failed to initialize VS environment
    echo         Try running from a clean command prompt.
    exit /b 1
)

:: Verify cl.exe is available
where cl.exe >nul 2>&1
if errorlevel 1 (
    echo [ERROR] cl.exe not found after VS environment setup
    exit /b 1
)
echo [INFO] Compiler ready: cl.exe

:: Set include paths for kernel-mode (append to existing INCLUDE)
set "INCLUDE=!WDK_INC!\km;!WDK_INC!\shared;!WDK_INC!\um;!INCLUDE!"

:: Set library paths for kernel-mode
set "LIB=!WDK_LIB!\km\!TARGET_ARCH!;!LIB!"

:: Compiler flags (split for readability)
set CFLAGS=/nologo /c /W4 /WX
set CFLAGS=!CFLAGS! /D_AMD64_ /DAMD64 /D_WIN64
set CFLAGS=!CFLAGS! /DNTDDI_VERSION=0x0A00000C
set CFLAGS=!CFLAGS! /D_NT_TARGET_VERSION=0x0A00
set CFLAGS=!CFLAGS! /DWINNT=1 /DWINVER=0x0A00 /D_WIN32_WINNT=0x0A00
set CFLAGS=!CFLAGS! /D__STDC_WANT_SECURE_LIB__=1
set CFLAGS=!CFLAGS! /DPOOL_NX_OPTIN=1
set CFLAGS=!CFLAGS! /kernel /GS- /Gy /Gm-
set CFLAGS=!CFLAGS! /Zp8 /Zc:wchar_t /Zc:forScope /Zc:inline
set CFLAGS=!CFLAGS! /fp:precise /EHs-c-
set CFLAGS=!CFLAGS! /wd4100 /wd4214

:: Debug vs Release flags
if /i "!BUILD_CONFIG!"=="debug" (
    set CFLAGS=!CFLAGS! /Od /Zi /DDBG=1 /D_DEBUG
    set LFLAGS_DBG=/DEBUG
) else (
    set CFLAGS=!CFLAGS! /O2 /DNDEBUG
    set LFLAGS_DBG=
)

:: Linker flags
set LFLAGS=/nologo /DRIVER /SUBSYSTEM:NATIVE /ENTRY:DriverEntry
set LFLAGS=!LFLAGS! /MACHINE:!TARGET_ARCH! /NODEFAULTLIB
set LFLAGS=!LFLAGS! !LFLAGS_DBG!
set LFLAGS=!LFLAGS! /INTEGRITYCHECK
set LFLAGS=!LFLAGS! /MERGE:_TEXT=.text /MERGE:_PAGE=PAGE
set LFLAGS=!LFLAGS! /SECTION:INIT,d

:: Libraries
set LIBS=ntoskrnl.lib hal.lib wmilib.lib BufferOverflowFastFailK.lib

:: Resource compiler flags
set RCFLAGS=/nologo /D_WIN64 /D_AMD64_

echo.
echo ============================================================
echo  Building !DRIVER_NAME!.sys (!BUILD_CONFIG!, !TARGET_ARCH!)
echo ============================================================
echo.

:: Change to script directory
pushd "%~dp0"

:: Compile the C source
echo [CL] Compiling !DRIVER_NAME!.c ...
cl.exe !CFLAGS! /Fo"!OUT_DIR!\!DRIVER_NAME!.obj" "!DRIVER_NAME!.c"
if errorlevel 1 (
    echo [ERROR] Compilation failed
    popd
    exit /b 1
)
echo [CL] Compilation successful.

:: Compile resources (optional - don't fail if rc.exe has issues)
echo [RC] Compiling !DRIVER_NAME!.rc ...
rc.exe !RCFLAGS! /I"!WDK_INC!\shared" /I"!WDK_INC!\um" /fo"!OUT_DIR!\!DRIVER_NAME!.res" "!DRIVER_NAME!.rc" 2>nul
if errorlevel 1 (
    echo [WARNING] Resource compilation failed - continuing without version info
    set "RES_OBJ="
) else (
    set "RES_OBJ=!OUT_DIR!\!DRIVER_NAME!.res"
    echo [RC] Resource compilation successful.
)

:: Link
echo [LINK] Linking !DRIVER_NAME!.sys ...
link.exe !LFLAGS! /OUT:"!OUT_DIR!\!DRIVER_NAME!.sys" "!OUT_DIR!\!DRIVER_NAME!.obj" !RES_OBJ! !LIBS!
if errorlevel 1 (
    echo [ERROR] Linking failed
    popd
    exit /b 1
)
echo [LINK] Linking successful.

popd

echo.
echo ============================================================
echo  Build successful!
echo ============================================================
echo.
echo  Output: !OUT_DIR!\!DRIVER_NAME!.sys
echo.
echo  Next steps:
echo    1. Sign the driver (test signing or production certificate)
echo    2. Install using:
echo         install_driver.cmd install
echo    Or manually:
echo         sc create VirtualSerial type= kernel binPath= "!OUT_DIR!\!DRIVER_NAME!.sys"
echo         sc start VirtualSerial
echo.
echo  NOTE: Windows requires driver signing. For testing:
echo    - Enable test signing: bcdedit /set testsigning on
echo    - Reboot, then install the driver
echo.

exit /b 0