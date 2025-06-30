@echo off
chcp 65001 >nul
echo ========================================
echo    VRChat 自动钓鱼程序 - 依赖安装
echo ========================================
echo.

:: 检查Python是否安装
python --version >nul 2>&1
if errorlevel 1 (
    echo [错误] 未找到Python，请先安装Python 3.8+
    echo 下载地址: https://www.python.org/downloads/
    pause
    exit /b 1
)

echo [检查] 正在检查Python环境...
python --version

echo.
echo [检查] 正在检查requirements.txt文件...
if not exist "requirements.txt" (
    echo [错误] 未找到requirements.txt文件
    pause
    exit /b 1
)

echo.
echo [安装] 正在安装依赖包...
python -m pip install -r requirements.txt

if errorlevel 1 (
    echo.
    echo [错误] 安装失败，请检查网络连接或手动安装
    echo 手动安装命令: python -m pip install python-osc watchdog pynput pillow pystray
    pause
    exit /b 1
)

echo.
echo [成功] 依赖安装完成！
echo [提示] 现在可以运行程序了：
echo        python auto_fishing_gui.py
echo.
pause