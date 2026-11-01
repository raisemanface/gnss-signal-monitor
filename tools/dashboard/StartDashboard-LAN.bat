@echo off
rem GNSS dashboard - LAN mode: web page open to other PCs on this network
rem Data ports (UDP/TCP 9911) are always 0.0.0.0; this only changes the WEB bind.
setlocal
rem script dir - works wherever the folder is put
set "DASH_DIR=%~dp0"
set DASH_URL=http://127.0.0.1:8787/

rem python: GNSS_PY env wins, then the usual local install, then PATH
set "PY=%GNSS_PY%"
if not defined PY if exist "C:\Python311\python.exe" set "PY=C:\Python311\python.exe"
if not defined PY set "PY=python"

cd /d "%DASH_DIR%"

rem already running? check the HTTP port 8787
netstat -ano | findstr /C:":8787" | findstr /C:"LISTENING" >nul
if not errorlevel 1 (
    echo Dashboard already running - opening page.
    start "" "%DASH_URL%"
    timeout /t 2 >nul
    exit /b 0
)

rem open the browser a few seconds later, once the server is up
start "" cmd /c "timeout /t 4 >nul & start "" %DASH_URL%"

echo.
echo   GNSS dashboard starting in LAN mode (web bind 0.0.0.0).
echo   Other PCs on this network can open:  http://THIS-PC-IP:8787/
echo   (Find THIS-PC-IP with:  ipconfig ^| findstr IPv4)
echo.
echo   First time only, run ONCE as Administrator to allow inbound HTTP:
echo     netsh advfirewall firewall add rule name="GNSS-Bench-WEB" dir=in action=allow protocol=TCP localport=8787
echo.
echo   Close this window to stop the dashboard.
echo.
"%PY%" -u dashboard.py --bind 0.0.0.0

if errorlevel 1 (
    echo.
    echo [ERROR] dashboard failed to start. Check the messages above.
    pause
)
