@echo off
call "%~dp0run-test.cmd" --software
exit /b %ERRORLEVEL%
