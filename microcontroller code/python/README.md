# PROGRESS TUBES PRD

# CARA RUN
server.py, Main.py, output.json hrus dlm satu folder

Step 1: nyalain server.py, jgn dimatiin terminalnya (kalau blm ada flask, pip install flask)
Step 2: Di main.cpp, sesuain bagian ini (line 8-10)

/* YG INI
    const char* WIFI_SSID     = "NamaWifiLu";
    const char* WIFI_PASSWORD = "kosonginAja";
    const char* SERVER_URL    = "http://196.128.X.X:5000/data"; // cek di cmd ->ipconfig (yg ipv4 address) */

Step 3: Upload code to ESP32

## CARA KERJA:
setiap 30 detik esp bakal ngecek output.json melalui wifi, trs hasilnya ditampilin
harusnya sih run Main.py ya tapi keknya skrg blm