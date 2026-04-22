from flask import Flask, request, jsonify, send_from_directory
from flask_cors import CORS
import requests
import os
import re
from datetime import datetime, timezone
import json
from dotenv import load_dotenv
load_dotenv()
from pathlib import Path

app = Flask(__name__, static_folder="static", static_url_path="/static")

CORS(
    app,
    resources={r"/api/*": {
        "origins": [
            "https://aliaml.folk.ntnu.no",
            "http://localhost:5500",
            "http://127.0.0.1:5500",
            "http://localhost:5000",
            "http://127.0.0.1:5000",
        ]
    }}
)

# NRF Cloud API configuration

API_KEY = os.getenv("NRF_CLOUD_API_KEY")
BASE_URL = "https://api.nrfcloud.com/v1"
#DEVICE_ID = "50344654-3037-4bdd-8004-2314d6fc32b9"
DEVICE_ID = "5034474b-3731-4738-80d4-0c0ffd414431" #2

CDB_FILE = Path("latest_cdb.txt")

DELAY = 0

if not API_KEY:
    raise ValueError("NRF_CLOUD_API_KEY mangler som miljøvariabel")


def auth_headers():
    return {
        "Authorization": f"Bearer {API_KEY}",
        "Content-Type": "application/json",
    }


def bearer_headers():
    return {
        "Authorization": f"Bearer {API_KEY}",
    }


def safe_json(response):
    try:
        return response.json()
    except ValueError:
        return {"raw": response.text}
    
def save_latest_cdb(text):
    CDB_FILE.write_text(text, encoding="utf-8")


def load_latest_cdb():
    if CDB_FILE.exists():
        return CDB_FILE.read_text(encoding="utf-8")
    return ""


def process_cdb_items(items):
    cdb_items = []

    for item in items:
        msg = item.get("message", {})
        app_id = msg.get("appId")
        data = str(msg.get("data", ""))

        if app_id == "cdb":
            cdb_items.append({
                "receivedAt": item.get("receivedAt", ""),
                "data": data
            })

    if not cdb_items:
        return

    cdb_items.sort(key=lambda x: x["receivedAt"])
    lines = [entry["data"] for entry in cdb_items]
    full_cdb = "\n".join(lines).strip()

    if "Mesh Network Information" in full_cdb:
        save_latest_cdb(full_cdb)

@app.route("/")
def index():
    return send_from_directory(".", "index.html")

@app.route("/api/health", methods=["GET"])
def health():
    return jsonify({"status": "ok"}), 200


@app.route("/api/send", methods=["POST"])
def send_message():
    """Send a message to the nRF Cloud board and return the response."""
    data = request.get_json()
    message = data.get("message", "")

    if not message:
        return jsonify({"error": "Message is required"}), 400

    headers = {
        "Authorization": f"Bearer {API_KEY}",
        "Content-Type": "application/json",
    }

    payload = {
        "topic": f"d/{DEVICE_ID}/c2d",
        "message": {
            "appId": "uart",
            "data": message,
        },
    }

    url = f"{BASE_URL}/devices/{DEVICE_ID}/messages"

    try:
        response = requests.post(url, headers=headers, json=payload)
        status_code = response.status_code

        try:
            body = response.json()
        except ValueError:
            body = {"raw": response.text}

        return jsonify({"status": status_code, "response": body}), status_code

    except requests.exceptions.RequestException as e:
        return jsonify({"error": str(e)}), 500


@app.route("/api/get_messages", methods=["GET"])
def get_messages():
    """Fetch stored device messages from nRF Cloud."""
    headers = {
        "Authorization": f"Bearer {API_KEY}",
    }

    params = {
        "deviceId": DEVICE_ID,
        "pageSort": "desc",
    }

    if appId := request.args.get("appId"):
        params["appId"] = appId
    if topic := request.args.get("topic"):
        params["topic"] = topic
    if pagelimit := request.args.get("pageLimit"):
        params["pageLimit"] = pagelimit
    else:
        params["pageLimit"] = 1
    if start := request.args.get("start"):
        params["start"] = start

    url = f"{BASE_URL}/messages"
    try:
        response = requests.get(url, headers=headers, params=params)
        status_code = response.status_code

        try:
            body = response.json()
        except ValueError:
            body = {"raw": response.text}

        items = body.get("items", [])
        process_cdb_items(items)

        return jsonify({"status": status_code, "response": body}), 200

    except requests.exceptions.RequestException as e:
        return jsonify({"error": str(e)}), 500

@app.route("/api/device", methods=["GET"])
def get_device_state():
    """Fetch the current device state from nRF Cloud."""
    headers = {
        "Authorization": f"Bearer {API_KEY}",
    }

    url = f"{BASE_URL}/devices/{DEVICE_ID}"

    try:
        response = requests.get(url, headers=headers)
        status_code = response.status_code

        try:
            body = response.json()
        except ValueError:
            body = {"raw": response.text}

        return jsonify({"status": status_code, "response": body}), 200

    except requests.exceptions.RequestException as e:
        return jsonify({"error": str(e)}), 500
    
@app.route("/api/cdb/latest", methods=["GET"])
def get_latest_cdb():
    latest = load_latest_cdb()
    print("Returning latest CDB, length =", len(latest))
    return jsonify({
        "status": "ok",
        "cdb": latest
    }), 200


if __name__ == "__main__":
    print("Server running at http://localhost:5000")
    app.run(debug=True, port=5000)
