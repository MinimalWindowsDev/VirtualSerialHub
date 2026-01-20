#==============================================================================
# Makefile for VirtualSerial kernel-mode driver (NMAKE)
#
# Part of VirtualSerialHub - FLOSS alternative to com0com
# License: MIT
#
# Usage:
#   nmake              - Build release
#   nmake DEBUG=1      - Build debug
#   nmake clean        - Clean build artifacts
#
# Prerequisites:
#   Run from "x64 Native Tools Command Prompt for VS 2022"
#   WDK 10.0.26100 installed
#==============================================================================

# Configuration
DRIVER_NAME = VirtualSerial
WDK_VERSION = 10.0.26100.0
TARGET_ARCH = x64

# Paths
WDK_ROOT = C:\Program Files (x86)\Windows Kits\10
WDK_INC = $(WDK_ROOT)\Include\$(WDK_VERSION)
WDK_LIB = $(WDK_ROOT)\Lib\$(WDK_VERSION)\km\$(TARGET_ARCH)

# Output directory
!IF "$(DEBUG)" == "1"
OUTDIR = build\$(TARGET_ARCH)\debug
CFLAGS_DBG = /Od /Zi /DDBG=1 /D_DEBUG
LFLAGS_DBG = /DEBUG
!ELSE
OUTDIR = build\$(TARGET_ARCH)\release
CFLAGS_DBG = /O2 /DNDEBUG
LFLAGS_DBG =
!ENDIF

# Compiler
CC = cl.exe
RC = rc.exe
LINK = link.exe

# Compiler flags
CFLAGS = /nologo /c /W4 /WX \
         /D_AMD64_ /DAMD64 /D_WIN64 \
         /DNTDDI_VERSION=0x0A00000C \
         /D_NT_TARGET_VERSION=0x0A00 \
         /DWINNT=1 /DWINVER=0x0A00 /D_WIN32_WINNT=0x0A00 \
         /D__STDC_WANT_SECURE_LIB__=1 \
         /DPOOL_NX_OPTIN=1 \
         /kernel /GS- /Gy /Gm- \
         /Zp8 /Zc:wchar_t /Zc:forScope /Zc:inline \
         /fp:precise /EHs-c- \
         /wd4100 /wd4214 \
         /I"$(WDK_INC)\km" /I"$(WDK_INC)\shared" /I"$(WDK_INC)\um" \
         $(CFLAGS_DBG)

# Resource compiler flags
RCFLAGS = /nologo /D_WIN64 /D_AMD64_ \
          /I"$(WDK_INC)\shared" /I"$(WDK_INC)\um"

# Linker flags
LFLAGS = /nologo /DRIVER /SUBSYSTEM:NATIVE /ENTRY:DriverEntry \
         /MACHINE:$(TARGET_ARCH) /NODEFAULTLIB \
         /INTEGRITYCHECK \
         /MERGE:_TEXT=.text /MERGE:_PAGE=PAGE \
         /SECTION:INIT,d \
         /LIBPATH:"$(WDK_LIB)" \
         $(LFLAGS_DBG)

# Libraries
LIBS = ntoskrnl.lib hal.lib wmilib.lib BufferOverflowFastFailK.lib

# Targets
TARGET = $(OUTDIR)\$(DRIVER_NAME).sys
OBJ = $(OUTDIR)\$(DRIVER_NAME).obj
RES = $(OUTDIR)\$(DRIVER_NAME).res

# Default target
all: dirs $(TARGET)

# Create output directory
dirs:
	@if not exist "$(OUTDIR)" mkdir "$(OUTDIR)"

# Compile C source
$(OBJ): $(DRIVER_NAME).c
	@echo [CL] $<
	$(CC) $(CFLAGS) /Fo"$@" "$<"

# Compile resources
$(RES): $(DRIVER_NAME).rc
	@echo [RC] $<
	$(RC) $(RCFLAGS) /fo"$@" "$<"

# Link driver
$(TARGET): $(OBJ) $(RES)
	@echo [LINK] $@
	$(LINK) $(LFLAGS) /OUT:"$@" $(OBJ) $(RES) $(LIBS)
	@echo.
	@echo Build complete: $@

# Clean
clean:
	@echo Cleaning...
	@if exist "build" rmdir /s /q "build"
	@echo Done.

.PHONY: all dirs clean
