@echo off
title Deadline Tracker Server
color 0A

echo ================================
echo    Deadline Tracker Server
echo ================================
echo.

:: Cek Python 3.12 tersedia
py -3.12 --version >nul 2>&1
if errorlevel 1 (
    echo [ERROR] Python 3.12 tidak ditemukan!
    echo Pastikan Python 3.12 sudah terinstall.
    echo Download: https://www.python.org/downloads/
    pause
    exit /b
)

:: Pindah ke folder script
cd /d "%~dp0"

:: Cek dependencies
echo [INFO] Mengecek dependencies...
py -3.12 -c "import flask, selenium" >nul 2>&1
if errorlevel 1 (
    echo [INFO] Installing dependencies...
    py -3.12 -m pip install flask selenium --quiet
    if errorlevel 1 (
        echo [ERROR] Gagal install dependencies!
        pause
        exit /b
    )
)

:: Cek output.json ada, kalau tidak jalankan Main.py dulu
if not exist "output.json" (
    echo [1/2] output.json belum ada, menjalankan Main.py...
    py -3.12 Main.py
    if errorlevel 1 (
        echo [ERROR] Main.py gagal dijalankan!
        echo Pastikan Chrome tersedia dan internet aktif.
        pause
        exit /b
    )
) else (
    echo [INFO] output.json ditemukan, skip Main.py
)

echo.
echo [2/2] Menjalankan Flask server...
echo.
echo ================================
echo  Server siap!
echo  Nyalakan ESP32 sekarang.
echo  Jangan tutup window ini!
echo  Tekan Ctrl+C untuk berhenti.
echo ================================
echo.

py -3.12 server.py

echo.
echo [INFO] Server berhenti.
pause