"""
=============================================================================
  Edunex ITB — Web Scraper pakai Sesi Chrome (Temp Profile Copy)
  Target: https://edunex.itb.ac.id/assignment

  Cara kerja:
    - Menyalin file sesi Chrome (Cookies, LocalStorage, dll) ke folder temp
    - Selenium memakai folder temp itu — tidak konflik dengan Chrome asli
    - Tidak perlu tutup Chrome, tidak perlu login, tidak perlu MFA

  Cara pakai:
    1. Pastikan kamu sudah login Edunex di Chrome seperti biasa
    2. Jalankan: python edunex_scraper.py
    3. Selesai — hasil tersimpan di output.json
=============================================================================
  Instalasi:
    pip install selenium webdriver-manager
=============================================================================
"""

import json
import logging
import os
import shutil
import tempfile
import time
from datetime import datetime
from typing import Optional

from selenium import webdriver
from selenium.webdriver.chrome.options import Options
from selenium.webdriver.chrome.service import Service
from selenium.webdriver.common.by import By
from selenium.webdriver.support import expected_conditions as EC
from selenium.webdriver.support.ui import WebDriverWait
from webdriver_manager.chrome import ChromeDriverManager

# ── Logging ──────────────────────────────────────────────────────────────────
logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s  [%(levelname)s]  %(message)s",
    datefmt="%Y-%m-%d %H:%M:%S",
)
log = logging.getLogger(__name__)

# ── Konfigurasi ───────────────────────────────────────────────────────────────
WINDOWS_USERNAME = os.environ.get("USERNAME", "brian")

CONFIG = {
    "chrome_user_data_dir": rf"C:\Users\{WINDOWS_USERNAME}\AppData\Local\Google\Chrome\User Data",
    "chrome_profile":       "Default",
    "target_url":           "https://edunex.itb.ac.id/assignment",
    "output_file":          "output.json",
    "wait_timeout":         15,
}


# ─────────────────────────────────────────────────────────────────────────────
#  1. COPY SESI KE FOLDER TEMP
# ─────────────────────────────────────────────────────────────────────────────
def copy_profile_to_temp(config: dict) -> str:
    src_profile = os.path.join(config["chrome_user_data_dir"], config["chrome_profile"])

    if not os.path.exists(src_profile):
        raise FileNotFoundError(
            f"Folder profil Chrome tidak ditemukan: {src_profile}\n"
            f"Cek nilai 'chrome_profile' di CONFIG (buka chrome://version untuk memastikan)."
        )

    temp_dir     = tempfile.mkdtemp(prefix="edunex_chrome_")
    temp_profile = os.path.join(temp_dir, "Default")
    os.makedirs(temp_profile, exist_ok=True)

    log.info("Menyalin sesi Chrome ke folder temp: %s", temp_dir)

    items_to_copy = [
        "Cookies",
        "Local Storage",
        "Session Storage",
        "Web Data",
        "Preferences",
        "Secure Preferences",
        "Network",
    ]

    for item in items_to_copy:
        src = os.path.join(src_profile, item)
        dst = os.path.join(temp_profile, item)
        if not os.path.exists(src):
            continue
        try:
            if os.path.isdir(src):
                shutil.copytree(src, dst)
            else:
                shutil.copy2(src, dst)
            log.info("  ✓ Disalin: %s", item)
        except Exception as e:
            log.warning("  ✗ Gagal salin '%s': %s", item, e)

    return temp_dir


# ─────────────────────────────────────────────────────────────────────────────
#  2. SETUP DRIVER
# ─────────────────────────────────────────────────────────────────────────────
def create_driver(temp_dir: str) -> webdriver.Chrome:
    options = Options()
    options.add_argument(f"--user-data-dir={temp_dir}")
    options.add_argument("--profile-directory=Default")
    options.add_argument("--no-sandbox")
    options.add_argument("--disable-dev-shm-usage")
    options.add_argument("--disable-gpu")
    options.add_argument("--window-size=1920,1080")
    options.add_argument("--no-first-run")
    options.add_argument("--no-default-browser-check")
    options.add_argument("--disable-extensions")
    options.add_argument("--disable-popup-blocking")
    options.add_argument("--headless=new")
    options.add_experimental_option("excludeSwitches", ["enable-automation"])
    options.add_experimental_option("useAutomationExtension", False)

    service = Service(ChromeDriverManager().install())
    driver  = webdriver.Chrome(service=service, options=options)
    log.info("ChromeDriver siap (pakai temp profile).")
    return driver


# ─────────────────────────────────────────────────────────────────────────────
#  3. VERIFIKASI SESI
# ─────────────────────────────────────────────────────────────────────────────
def verify_session(driver: webdriver.Chrome, config: dict) -> bool:
    wait = WebDriverWait(driver, config["wait_timeout"])

    log.info("Membuka halaman target: %s", config["target_url"])
    driver.get(config["target_url"])

    try:
        wait.until(lambda d: "edunex.itb.ac.id" in d.current_url)
        current_url = driver.current_url

        if "login" in current_url:
            log.error(
                "Sesi tidak valid — diarahkan ke halaman login.\n"
                "  → Buka Chrome → login ke Edunex seperti biasa → jalankan skrip lagi."
            )
            return False

        log.info("Sesi valid. Masuk sebagai user yang sudah login.")
        return True

    except Exception as exc:
        log.error("Gagal verifikasi sesi: %s", exc)
        return False


# ─────────────────────────────────────────────────────────────────────────────
#  4. PARSE END DATE
# ─────────────────────────────────────────────────────────────────────────────
def parse_end_date(end_date_str: str) -> Optional[datetime]:
    s = end_date_str.strip()

    # Terjemahan bulan Indonesia → English
    BULAN_ID = {
        "Januari": "January", "Februari": "February", "Maret": "March",
        "April": "April",     "Mei": "May",           "Juni": "June",
        "Juli": "July",       "Agustus": "August",    "September": "September",
        "Oktober": "October", "November": "November", "Desember": "December",
    }
    for id_name, en_name in BULAN_ID.items():
        s = s.replace(id_name, en_name)

    FORMATS = [
        "%d/%m/%y %H:%M",     # 12/05/26 10:10  ← format Edunex
        "%d %b %Y, %H:%M",    # 30 May 2025, 23:59
        "%d %B %Y, %H:%M",    # 30 Mei 2025, 23:59
        "%d/%m/%Y %H:%M",     # 30/05/2025 23:59
        "%d-%m-%Y %H:%M",     # 30-05-2025 23:59
        "%Y-%m-%d %H:%M",     # 2025-05-30 23:59
        "%d %b %Y %H:%M",     # 30 May 2025 23:59
        "%d/%m/%Y",           # 30/05/2025
        "%d %b %Y",           # 30 May 2025
    ]

    for fmt in FORMATS:
        try:
            return datetime.strptime(s, fmt)
        except ValueError:
            continue

    return None


# ─────────────────────────────────────────────────────────────────────────────
#  5. SCRAPE DATA
# ─────────────────────────────────────────────────────────────────────────────
def scrape_data(driver: webdriver.Chrome, config: dict) -> list[dict]:
    results: list[dict] = []

    try:
        wait = WebDriverWait(driver, config["wait_timeout"])

        log.info("Menunggu tabel tugas termuat...")
        wait.until(
            EC.presence_of_element_located((By.CSS_SELECTOR, "table.vs-table--tbody-table tr.tr-values"))
        )

        rows = driver.find_elements(By.CSS_SELECTOR, "table.vs-table--tbody-table tr.tr-values")
        now           = datetime.now()
        timestamp_now = now.strftime("%Y-%m-%d %H:%M")

        for idx, row in enumerate(rows, start=1):
            try:
                course    = row.find_element(By.CSS_SELECTOR, "td:nth-child(2)").text.strip()
                title     = row.find_element(By.CSS_SELECTOR, "td:nth-child(3)").text.strip()
                full_date = row.find_element(By.CSS_SELECTOR, "td:nth-child(5)").text.strip()
                end_date  = full_date.split(" - ")[1] if " - " in full_date else full_date
                status    = row.find_element(By.CSS_SELECTOR, "td:nth-child(7) span.badge").text.strip()

                # ── Filter 1: Hanya Unanswered ────────────────────────────
                if status != "Unanswered":
                    log.info("  Skip baris %d (%s) — status: %s", idx, title, status)
                    continue

                # ── Filter 2: End date belum terlewat ─────────────────────
                end_dt = parse_end_date(end_date)
                if end_dt is None:
                    log.warning("  Gagal parse end_date baris %d: '%s' — tetap disertakan.", idx, end_date)
                elif end_dt < now:
                    log.info("  Skip baris %d (%s) — sudah lewat: %s", idx, title, end_date)
                    continue

                results.append({
                    "id":         idx,
                    "course":     course.replace("\n", " "),
                    "title":      title,
                    "end_date":   end_date,
                    "status":     status,
                    "scraped_at": timestamp_now,
                })

            except Exception as e:
                log.warning("Gagal memproses baris %d: %s", idx, e)
                continue

        log.info("Ekstraksi selesai. Total lolos filter: %d tugas.", len(results))

    except Exception as exc:
        log.error("Gagal scrape: %s", exc)
        driver.save_screenshot("scrape_error.png")
        log.error("Screenshot error disimpan ke scrape_error.png")

    return results


# ─────────────────────────────────────────────────────────────────────────────
#  6. SAVE TO JSON
# ─────────────────────────────────────────────────────────────────────────────
def save_to_json(data: list[dict], filepath: str) -> None:
    with open(filepath, "w", encoding="utf-8") as f:
        json.dump(data, f, ensure_ascii=False, indent=4)

    log.info("Tersimpan ke '%s'. Total: %d tugas.", filepath, len(data))
    if not data:
        log.info("Tidak ada tugas aktif — ESP32 akan tampilkan 'All done'.")


# ─────────────────────────────────────────────────────────────────────────────
#  7. MAIN
# ─────────────────────────────────────────────────────────────────────────────
def main() -> None:
    driver:   Optional[webdriver.Chrome] = None
    temp_dir: Optional[str]              = None

    try:
        temp_dir = copy_profile_to_temp(CONFIG)
        driver   = create_driver(temp_dir)

        if not verify_session(driver, CONFIG):
            return

        data = scrape_data(driver, CONFIG)
        save_to_json(data, CONFIG["output_file"])

    except FileNotFoundError as e:
        log.error(str(e))

    finally:
        if driver:
            driver.quit()
            log.info("Browser ditutup.")
        if temp_dir and os.path.exists(temp_dir):
            shutil.rmtree(temp_dir, ignore_errors=True)
            log.info("Folder temp dibersihkan.")


if __name__ == "__main__":
    main()

time.sleep(40)