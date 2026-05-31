# edunex-scraper-noise-machine

ya intinya bakal ada deskripsi disini

---

## Repo Structure

```
edunex-scraper-noise-machine/
├── microcontroller code/
│   ├── .vscode/
│   ├── include/
│   ├── lib/
│   ├── src/
│   │   └── main.cpp                  # ← ESP32 code to upload
│   ├── test/
│   ├── .gitignore
│   ├── platformio.ini
│   └── projectPRD.code-workspace
├── program (run on laptop)/          # ← every file needed to run on laptop
│   ├── Main.py                       # Edunex scraper (Selenium)
│   ├── server.py                     # Flask server + UDP broadcaster
│   ├── output.json                   # scraper output (data)
│   ├── start.bat                     # launcher
│   └── README.md                     # laptop-side instructions
└── README.md                         # ← you are here :)
```

---

## How to Run

### Prasyarat
1. **Python 3.12** sudah terinstall (`start.bat` cek versi ini secara spesifik).
2. **Chrome** sudah login ke Edunex (scraper pakai profil Chrome default).
3. **WiFi laptop dan ESP32 sama** (bisa diatur setelah ESP32 menyala).

### Langkah-langkah
1. Jalankan `start.bat` — **jangan tutup terminalnya.**
2. Tunggu sampai server siap, ditandai dengan log broadcast:
   ```
   Broadcasting: DEADLINE_SERVER:XXX.XXX.X.X:5000
   ```
3. Nyalakan alat fisik (ESP32).
4. Enjoy (^_^)

---

## How It Works

### `start.bat`
1. Memastikan **Python 3.12** tersedia (kalau tidak, tampilkan link download — tidak meng-install Python sendiri).
2. Auto-install dependencies (`flask`, `selenium`) kalau belum ada.
3. Menjalankan `Main.py` **sekali** agar `output.json` sudah fresh sebelum server menyala.
4. Menjalankan `server.py`.

### `Main.py` (scraper)
1. Membuka browser headless memakai profil Chrome default.
2. Membuka `https://edunex.itb.ac.id/assignment`.
3. Scrape tabel assignment — field: **course, title, end date, completion status**.
4. Filter: (a) belum lewat deadline (`end date > now`), dan (b) status = **Unanswered**.
5. Menyimpan semua assignment yang lolos filter ke `output.json`.

### `server.py` (Flask + UDP)
- **UDP broadcast** (port `4210`, tiap 3 detik) agar ESP32 menemukan server tanpa konfigurasi manual.
- **Scraper background** — menjalankan `Main.py` tiap 30 menit untuk memperbarui `output.json`. Berjalan di thread terpisah, jadi request ESP32 tidak pernah blocking.
- **HTTP endpoints** (port `5000`):

  | Endpoint | Method | Fungsi |
  |----------|--------|--------|
  | `/data`  | GET | Mengembalikan isi `output.json` (daftar tugas) |
  | `/time`  | GET | Timestamp server untuk sinkronisasi countdown ESP32 |
  | `/ping`  | GET | Health check + IP server |

  > Mode demo: jalankan `py -3.12 server.py --demo` untuk membaca `output.json` langsung tanpa menjalankan scraper.

### ESP32 (`main.cpp`)
1. Connect ke WiFi — kalau gagal, tampilkan instruksi setup di LCD.
2. Cari server lewat UDP broadcast.
3. Sinkronkan waktu lewat `/time`, lalu fetch tugas dari `/data`.
4. **0 tugas** → tampilkan `All Tasks Done (^_^)`.
5. **≥ 1 tugas** → bunyikan buzzer (`beepPanic`) dan tampilkan countdown deadline di LCD.
6. Tunggu 30 menit, lalu fetch lagi.

---

## Wiring

**LCD ↔ ESP32 (I2C)**

| LCD | ESP32 |
|-----|-------|
| GND | GND   |
| VCC | 5V    |
| SDA | GPIO 21 |
| SCL | GPIO 22 |

**Buzzer ↔ ESP32**

| Buzzer | ESP32 |
|--------|-------|
| (+)    | GPIO 23 |
| (−)    | GND   |

**Power source:** apa pun yang penting bisa ngasih daya 5V.

---

## Anggota Kelompok

| No | Nama | NIM |
|----|------|-----|
| 1 | Bryan Pamungkas Prahara | 13525092 |
| 2 | Peter Emmanuel Suwardy | 13525125 |
| 3 | Sophia Imelda Rogate Marpaung | 13525090 |
| 4 | Ryuza Nadif Aldebaran | 13525026 |