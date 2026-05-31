from flask import Flask, jsonify
import json
import os
import subprocess
import sys
import socket
import threading
import time
from datetime import datetime

app = Flask(__name__)

BASE_DIR  = os.path.dirname(os.path.abspath(__file__))
JSON_FILE = os.path.join(BASE_DIR, "output.json")
MAIN_FILE = os.path.join(BASE_DIR, "Main.py")
UDP_PORT  = 4210
BROADCAST_INTERVAL = 3
SCRAPE_INTERVAL    = 1800  # 30 menit

DEMO_MODE = "--demo" in sys.argv

def get_local_ip():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect(("8.8.8.8", 80))
        return s.getsockname()[0]
    finally:
        s.close()

def udp_broadcaster():
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
    while True:
        try:
            ip      = get_local_ip()
            message = f"DEADLINE_SERVER:{ip}:5000".encode()
            sock.sendto(message, ("<broadcast>", UDP_PORT))
            print(f"Broadcasting: {message.decode()}")
        except Exception as e:
            print(f"Broadcast error: {e}")
        time.sleep(BROADCAST_INTERVAL)

def scrape_loop():
    while True:
        print("Running Main.py...")
        try:
            subprocess.run([sys.executable, MAIN_FILE], cwd=BASE_DIR, timeout=180)
            print("Main.py done.")
        except subprocess.TimeoutExpired:
            print("Main.py timeout!")
        except Exception as e:
            print(f"Main.py error: {e}")
        time.sleep(SCRAPE_INTERVAL)

@app.route('/data', methods=['GET'])
def get_data():
    if not os.path.exists(JSON_FILE):
        return jsonify({"error": "output.json not found"}), 404
    with open(JSON_FILE, 'r', encoding='utf-8') as f:
        return jsonify(json.load(f))

@app.route('/time', methods=['GET'])
def get_time():
    return jsonify({"timestamp": int(datetime.now().timestamp())})

@app.route('/ping', methods=['GET'])
def ping():
    return jsonify({"status": "ok", "ip": get_local_ip()})

if __name__ == '__main__':
    # UDP broadcaster
    threading.Thread(target=udp_broadcaster, daemon=True).start()

    # Scraper background — skip kalau demo mode
    if not DEMO_MODE:
        threading.Thread(target=scrape_loop, daemon=True).start()
        print("Scraper running in background.")
    else:
        print("DEMO MODE — skipping scraper, reading output.json directly.")

    ip = get_local_ip()
    print("=" * 40)
    print(f"  Deadline Tracker Server")
    print(f"  IP      : {ip}")
    print(f"  Port    : 5000")
    print(f"  Mode    : {'DEMO' if DEMO_MODE else 'NORMAL'}")
    print(f"  UDP     : broadcasting every {BROADCAST_INTERVAL}s")
    print("=" * 40)

    app.run(host='0.0.0.0', port=5000, debug=False)