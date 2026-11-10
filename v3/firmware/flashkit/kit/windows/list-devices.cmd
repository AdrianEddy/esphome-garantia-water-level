@echo off
rem Lists the DFU devices dfu-util can see (U3 in DFU shows as [0483:df11]).
call "%~dp0_flash.cmd" list
