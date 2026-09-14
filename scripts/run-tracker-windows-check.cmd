@echo off
setlocal
"%~dp0s3g_tracker_windows_clap_smoke.exe" "%~dp0s3g_tracker.clap"
set "tracker_check_result=%errorlevel%"
echo.
if "%tracker_check_result%"=="0" (echo PASS - Windows integration check completed.) else (echo FAIL - See diagnostics above.)
pause
exit /b %tracker_check_result%
