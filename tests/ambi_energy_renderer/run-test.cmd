@echo off
setlocal
pushd "%~dp0"
if errorlevel 1 exit /b 1
set "energy_mode=hardware"
set "energy_option="
if "%~1"=="--software" (
  set "energy_mode=software"
  set "energy_option=--software"
)
set "energy_results=results-%energy_mode%-%RANDOM%-%RANDOM%"
echo Ambi Energy GPU comparison prototype - this does not install a plugin.
echo Running %energy_mode% test. Please wait for PASS or FAIL.
"s3g_ambi_energy_renderer_probe.exe" --compare "input.bin" "metal-reference.bin" "%energy_results%" %energy_option%
set "energy_exit=%ERRORLEVEL%"
echo.
echo Exit code: %energy_exit%   Results: "%energy_results%"
echo Send report.txt from that folder back with your Windows test results.
echo If no report was created, copy or photograph the error above.
pause
popd
exit /b %energy_exit%
