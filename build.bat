@echo off
chcp 65001 >nul
echo ========================================
echo    VRChat Auto Fishing Build Script
echo ========================================
echo.

echo Checking Python installation...
python --version
if errorlevel 1 (
    echo ERROR: Python not found. Please install Python first.
    pause
    exit /b 1
)

echo.
echo Checking required files...
if not exist "requirements.txt" (
    echo ERROR: requirements.txt not found
    pause
    exit /b 1
)

if not exist "auto_fishing_gui.py" (
    echo ERROR: auto_fishing_gui.py not found
    pause
    exit /b 1
)

if not exist "ico.ico" (
    echo WARNING: ico.ico not found, will use default icon
)

echo.
echo Installing PyInstaller if needed...
python -m PyInstaller --version >nul 2>&1
if errorlevel 1 (
    echo Installing PyInstaller...
    python -m pip install pyinstaller
    if errorlevel 1 (
        echo ERROR: Failed to install PyInstaller
        pause
        exit /b 1
    )
    echo Verifying PyInstaller installation...
    python -m PyInstaller --version >nul 2>&1
    if errorlevel 1 (
        echo ERROR: PyInstaller installation verification failed
        pause
        exit /b 1
    )
)

echo.
echo Installing dependencies if needed...
python -c "import pythonosc, watchdog, pynput, PIL, pystray" >nul 2>&1
if errorlevel 1 (
    echo Installing dependencies from requirements.txt...
    python -m pip install -r requirements.txt
    if errorlevel 1 (
        echo ERROR: Failed to install dependencies
        pause
        exit /b 1
    )
    echo Verifying dependencies...
    python -c "import pythonosc, watchdog, pynput, PIL, pystray" >nul 2>&1
    if errorlevel 1 (
        echo ERROR: Some dependencies are still missing after installation
        pause
        exit /b 1
    )
)

echo.
echo Cleaning build cache and directories...
if exist "build" (
    rmdir /s /q "build"
    echo Cleaned build directory
)
if exist "dist" (
    rmdir /s /q "dist"
    echo Cleaned dist directory
)
if exist "__pycache__" (
    rmdir /s /q "__pycache__"
    echo Cleaned __pycache__ directory
)
if exist "*.spec" (
    for %%f in (*.spec) do (
        if not "%%f"=="auto_fishing_gui.spec" (
            del "%%f"
            echo Cleaned temporary spec file: %%f
        )
    )
)

echo.
echo Building executable...
if exist "auto_fishing_gui.spec" (
    echo Using existing spec file...
    python -m PyInstaller --distpath=build auto_fishing_gui.spec
) else (
    echo Creating new spec file...
    if exist "ico.ico" (
        echo Creating spec file with icon...
        python -m PyInstaller --onefile --windowed --name "VRChat自动钓鱼" --distpath=build --icon=ico.ico auto_fishing_gui.py
    ) else (
        echo Creating spec file without icon...
        python -m PyInstaller --onefile --windowed --name "VRChat自动钓鱼" --distpath=build auto_fishing_gui.py
    )
)

if errorlevel 1 (
    echo.
    echo ERROR: Build failed
    pause
    exit /b 1
)

echo.
echo Build completed successfully!
echo.

REM Handle generated executable
if exist "build\VRChat自动钓鱼\VRChat自动钓鱼.exe" (
    echo Moving executable...
    move "build\VRChat自动钓鱼\VRChat自动钓鱼.exe" "build\VRChat自动钓鱼.exe" > nul
    echo Cleaning up bundle directory...
    rmdir /s /q "build\VRChat自动钓鱼"
    echo Executable is now at: build\VRChat自动钓鱼.exe
) else if exist "build\VRChat自动钓鱼.exe" (
    echo Executable is already at: build\VRChat自动钓鱼.exe
) else (
    echo WARNING: Could not find the generated executable 'VRChat自动钓鱼.exe'.
    echo Please check the 'build' directory manually.
    echo.
    echo Contents of 'build' directory:
    dir build
)

echo.
echo Cleaning temporary files...
if exist "*.spec" (
    for %%f in (*.spec) do (
        if not "%%f"=="auto_fishing_gui.spec" del "%%f"
    )
)
if exist "__pycache__" rmdir /s /q "__pycache__" 2>nul
echo.
echo Build process completed!
pause 