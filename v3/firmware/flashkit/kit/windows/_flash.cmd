@echo off
rem _flash.cmd MODE   (MODE = update, blank or list; run through the scripts next to it; see ..\README.md)
rem Flashes U3 of garantia-knx-direct over the STM32 ROM bootloader's USB DFU with dfu-util.
setlocal EnableExtensions EnableDelayedExpansion
title garantia-knx flash kit
set "KIT=%~dp0.."
set "DFU=%~dp0tools\dfu-util-static.exe"
set "MODE=%~1"
set "DEV=0483:df11"
set "TMPL=%TEMP%\gk-dfu-list.txt"
if not exist "%KIT%\logs" mkdir "%KIT%\logs"
for /f %%t in ('powershell -NoProfile -Command "Get-Date -Format yyyyMMdd-HHmmss"') do set "TS=%%t"
set "LOG=%KIT%\logs\%MODE%-%TS%.txt"

if not exist "%DFU%" (
  echo dfu-util not found: %DFU%
  goto end_fail
)

if /i "%MODE%"=="update" (
  set "IMG=garantia-knx-u3-app.bin"
  set "ADDR=0x08001000"
) else if /i "%MODE%"=="blank" (
  set "IMG=garantia-knx-u3-full.bin"
  set "ADDR=0x08000000"
) else if /i "%MODE%"=="list" (
  "%DFU%" -l
  goto end_ok
) else (
  echo Unknown mode "%MODE%". Use flash-update.cmd, flash-blank-chip.cmd or list-devices.cmd.
  goto end_fail
)

echo.
echo === garantia-knx U3: %MODE% with %IMG% at %ADDR% ===
echo.

rem ---- 1. The image is the one the kit was built with
set "EXP="
for /f "tokens=1" %%h in ('findstr /c:" %IMG%" "%KIT%\firmware\SHA256SUMS"') do set "EXP=%%h"
set "GOT="
for /f %%h in ('powershell -NoProfile -Command "(Get-FileHash -Algorithm SHA256 -LiteralPath '%KIT%\firmware\%IMG%').Hash.ToLower()"') do set "GOT=%%h"
if not defined EXP (
  echo No checksum for %IMG% in firmware\SHA256SUMS. The kit is incomplete.
  goto end_fail
)
if /i not "!EXP!"=="!GOT!" (
  echo Checksum MISMATCH for firmware\%IMG%. The file is damaged: copy the kit again.
  goto end_fail
)
echo Image checksum OK.

rem ---- 2. U3 is in DFU mode
:find
"%DFU%" -l > "%TMPL%" 2>&1
findstr /c:"[%DEV%]" "%TMPL%" >nul
if errorlevel 1 (
  echo.
  echo No STM32 in DFU mode found. Put U3 into DFU, then press a key here:
  echo   - running board: hold SW1 for 3-10 s ^(D5 blinks^) and release it;
  echo   - new or broken board: hold SW1 while bus power comes on ^(blank chips go to DFU by themselves^);
  echo   - first time on this PC: install the WinUSB driver with install-driver-zadig.cmd ^(see README^).
  echo Ctrl+C stops.
  pause >nul
  goto find
)
echo U3 found in DFU mode.

rem ---- 3. Boot manager check: an update needs one, a blank-chip flash shouldn't be used over one
set "BM=%TEMP%\gk-bootdesc.bin"
if exist "%BM%" del "%BM%"
"%DFU%" -d %DEV% -a 0 -s 0x08000100:16 -U "%BM%" >nul 2>&1
set "HAVEBM=0"
powershell -NoProfile -Command "$b=[IO.File]::ReadAllBytes('%BM%'); if ($b.Length -ge 4 -and [Text.Encoding]::ASCII.GetString($b,0,4) -eq 'U3BM') { exit 0 } else { exit 1 }" >nul 2>&1
if not errorlevel 1 set "HAVEBM=1"
if /i "%MODE%"=="update" if "!HAVEBM!"=="0" (
  echo.
  echo This chip has no garantia boot manager ^(a new board?^). Use flash-blank-chip.cmd instead.
  goto end_fail
)
if /i "%MODE%"=="blank" if "!HAVEBM!"=="1" (
  echo.
  echo This chip already has the boot manager. For a normal update use flash-update.cmd:
  echo rewriting the boot manager risks a board that only an SWD probe can recover if power fails.
  set "ANS="
  set /p "ANS=Type YES to rewrite it anyway: "
  if /i not "!ANS!"=="YES" goto end_fail
)

rem ---- 4. Flash and leave DFU
echo.
echo Flashing, about 10-20 s. Don't unplug USB or the bus ...
"%DFU%" -d %DEV% -a 0 -s %ADDR%:leave -D "%KIT%\firmware\%IMG%" > "%LOG%" 2>&1
type "%LOG%"
findstr /c:"File downloaded successfully" "%LOG%" >nul
if errorlevel 1 (
  echo.
  echo FLASH FAILED: the download didn't complete. U3 stays in DFU; run this script again.
  goto end_fail
)

rem ---- 5. U3 must not come back as a DFU device: the boot manager checks the application's CRC at every
rem      start and returns to DFU if it is bad.
echo.
echo Download complete. Waiting for U3 to start ...
timeout /t 6 /nobreak >nul
"%DFU%" -l > "%TMPL%" 2>&1
findstr /c:"[%DEV%]" "%TMPL%" >nul
if not errorlevel 1 (
  echo.
  echo FAILED: U3 is back in the bootloader, so it rejected the new application ^(or SW1 is still held^).
  echo Release SW1 and run this script again.
  goto end_fail
)
echo.
echo SUCCESS: the new firmware is running ^(U3 left DFU and didn't come back^).
if /i "%MODE%"=="blank" echo Next: in ETS, program the individual address ^(short press on SW1^) and download the application.
echo Log: %LOG%
goto end_ok

:end_fail
echo.
pause
endlocal
exit /b 1

:end_ok
echo.
pause
endlocal
exit /b 0
