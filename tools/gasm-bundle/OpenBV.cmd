@echo off
rem OpenBV @VERSION@ on gasm-run @GASM_VERSION@. Double-click to play; OpenBV.cmd --help for the options.
rem The work is done by openbv.ps1 (the gasm-run command line).
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0openbv.ps1" %*
set rc=%errorlevel%
rem Keep the window open on errors when started by double-click (OPENBV_NO_PAUSE=1 skips this).
if not "%rc%"=="0" if not "%OPENBV_NO_PAUSE%"=="1" pause
exit /b %rc%
