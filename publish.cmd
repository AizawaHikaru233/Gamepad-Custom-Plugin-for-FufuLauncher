@echo off
pwsh.exe -NoProfile -File "%~dp0publish.ps1" %*
set "taskExitCode=%ERRORLEVEL%"
if not "%taskExitCode%"=="0" echo Publish failed. See the error above.
if "%taskExitCode%"=="0" echo Release package created. Default output: dist.
pause
exit /b %taskExitCode%
