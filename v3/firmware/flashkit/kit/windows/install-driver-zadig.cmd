@echo off
rem One-time driver setup: Zadig installs WinUSB for "STM32 BOOTLOADER" (USB ID 0483 DF11). See ..\README.md.
rem U3 must be in DFU mode while Zadig runs. Windows asks for administrator rights.
start "" "%~dp0tools\zadig-2.9.exe"
