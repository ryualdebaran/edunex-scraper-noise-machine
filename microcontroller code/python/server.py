from flask import Flask, jsonify
import json
import os
import subprocess
import sys
import socket
import threading
import time
from datetime import datetime

import sys
DEMO_MODE = "--demo" in sys.argv

app = Flask(__name__)

BASE_DIR  = os.path.dirname(os.path.abspath(__file__))
JSON_FILE = os.path.join(BASE_DIR, "output.json")
MAIN_FILE = os.path.join(BASE_DIR, "Main.py")
UDP_PORT  = 4210
BROADCAST_INTERVAL = 3

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
            ip = get_local_ip()
            message = f"DEADLINE_SERVER:{ip}:5000".encode()
            sock.sendto(message, ("<broadcast>", UDP_PORT))
            print(f"Broadcasting: {message.decode()}")
        except Exception as e:
            print(f"Broadcast error: {e}")
        time.sleep(BROADCAST_INTERVAL)

@app.route('/data', methods=['GET'])
@app.route('/data', methods=['GET'])
def get_data():
    if DEMO_MODE:
        if not os.path.exists(JSON_FILE):
            return jsonify({"error": "output.json not found"}), 404
        with open(JSON_FILE, 'r', encoding='utf-8') as f:
            return jsonify(json.load(f))

    try:
        subprocess.run([sys.executable, MAIN_FILE], cwd=BASE_DIR, timeout=120)
    except subprocess.TimeoutExpired:
        return jsonify({"error": "Scraping timeout"}), 500
    except Exception as e:
        return jsonify({"error": str(e)}), 500

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
    # Jalankan UDP broadcaster di background thread
    t = threading.Thread(target=udp_broadcaster, daemon=True)
    t.start()

    ip = get_local_ip()
    print("=" * 40)
    print(f"  Deadline Tracker Server")
    print(f"  IP      : {ip}")
    print(f"  Port    : 5000")
    print(f"  Data    : http://{ip}:5000/data")
    print(f"  Time    : http://{ip}:5000/time")
    print(f"  UDP     : broadcasting tiap {BROADCAST_INTERVAL}s")
    print("=" * 40)

    app.run(host='0.0.0.0', port=5000, debug=False)