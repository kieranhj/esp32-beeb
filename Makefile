# Wrapper so that `make` works from PowerShell / Windows Terminal.
# GNU make runs recipes with cmd.exe here (no sh on PATH), so the host
# scripts run under Git's sh and the firmware build under pwsh with the
# ESP-IDF environment activated. Flashing is left to you (see `make help`).

SH ?= C:\Program Files\Git\bin\sh.exe
IDF_PROFILE ?= C:\Espressif\tools\Microsoft.v5.5.5.PowerShell_profile.ps1

.PHONY: all host firmware size help
all: host

# Host build of the core with zig cc + the Klaus Dormann CPU tests (under a second).
host:
	"$(SH)" host/build.sh

# Board firmware with ESP-IDF v5.5.5 (idf.py build into build\).
firmware:
	@pwsh -NoProfile -ExecutionPolicy Bypass -Command "if (-not (Test-Path '$(IDF_PROFILE)')) { Write-Host 'ESP-IDF is not set up: install ESP-IDF v5.5.5 with Espressif eim (C:\esp\v5.5.5\esp-idf, tools in C:\Espressif), or set IDF_PROFILE to its PowerShell activation script. See START_HERE.md.'; exit 1 }; if ((Test-Path build/CMakeCache.txt) -and (Select-String -Quiet -Path build/CMakeCache.txt -Pattern 'OneDrive/Projects/esp32-beeb')) { Write-Host 'build\ was configured at the old OneDrive path; CMake will refuse it. Delete the build folder (it is gitignored) and run make firmware again. Note: the BEEB_DISC choice in build\CMakeCache.txt goes with it (default crtc-tmoyos.ssd).'; exit 1 }; . '$(IDF_PROFILE)' | Out-Null; idf.py build; exit $$LASTEXITCODE"

size:
	@pwsh -NoProfile -ExecutionPolicy Bypass -Command ". '$(IDF_PROFILE)' | Out-Null; idf.py size; exit $$LASTEXITCODE"

help:
	@echo make           - host build of the core (zig cc) + Klaus Dormann CPU tests, via host/build.sh
	@echo make firmware  - idf.py build for the ESP32-S3 board (activates ESP-IDF v5.5.5 first)
	@echo make size      - idf.py size
	@echo make help      - this list
	@echo Flash and monitor by hand (board on COM3), from PowerShell after
	@echo   . $(IDF_PROFILE)
	@echo   idf.py -p COM3 flash ; python tools/capture.py 8 COM3
	@echo See START_HERE.md and CLAUDE.md for the rest (trace/frame diffs against beebjit, disc bundle).
