@echo off
rem Pianificatore radio PippaNet: avvia il server locale e apre il browser.
cd /d "%~dp0"
start "" /b cmd /c "timeout /t 2 >nul & start http://localhost:8787"
node server.js
pause
