@echo off
chcp 936 >nul
cd /d "%~dp0"
if not exist "bin\BiliProxy.exe" goto nofile
echo 正在安装并启动 BiliProxy ...
start "" "%~dp0bin\BiliProxy.exe"
timeout /t 3 >nul
echo.
echo 安装完成。请重启 Edge 后打开 B 站。
echo 首次运行若弹出“安全警告”，请点“是”。
pause
exit /b 0
:nofile
echo 找不到 bin\BiliProxy.exe，请先运行 build.ps1
pause
exit /b 1