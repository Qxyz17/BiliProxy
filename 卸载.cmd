@echo off
chcp 936 >nul
cd /d "%~dp0"
if exist "bin\BiliProxy.exe" goto usedir
start "" "%LOCALAPPDATA%\BiliProxy\BiliProxy.exe" --uninstall
goto done
:usedir
start "" "%~dp0bin\BiliProxy.exe" --uninstall
:done
timeout /t 3 >nul
echo 已卸载：根证书、PAC、开机自启。
pause