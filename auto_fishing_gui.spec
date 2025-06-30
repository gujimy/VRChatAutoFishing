# -*- mode: python ; coding: utf-8 -*-

block_cipher = None

# 检查图标文件是否存在
import os
icon_file = 'ico.ico' if os.path.exists('ico.ico') else None

a = Analysis(
    ['auto_fishing_gui.py'],
    pathex=[],
    binaries=[],
    datas=[('ico.ico', '.')] if icon_file else [],
    hiddenimports=['watchdog.observers.polling', 'pythonosc', 'watchdog'],
    hookspath=[],
    hooksconfig={},
    runtime_hooks=[],
    excludes=[],
    win_no_prefer_redirects=False,
    win_private_assemblies=False,
    cipher=block_cipher,
    noarchive=False,
)
pyz = PYZ(a.pure, a.zipped_data, cipher=block_cipher)

exe = EXE(
    pyz,
    a.scripts,
    a.binaries,
    a.zipfiles,
    a.datas,
    [],
    name='VRChatAutoFishing',
    debug=False,
    bootloader_ignore_signals=False,
    strip=False,
    upx=True,
    upx_exclude=[],
    runtime_tmpdir=None,
    console=False,
    disable_windowed_traceback=False,
    argv_emulation=False,
    target_arch=None,
    codesign_identity=None,
    entitlements_file=None,
    icon=icon_file,
) 