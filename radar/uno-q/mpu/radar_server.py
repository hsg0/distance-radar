#!/usr/bin/env python3
"""Radar page for the Uno Q Linux side.

Serves the scope and the map, and forwards one-line commands to the
microcontroller serial monitor on 127.0.0.1:7500. On a desktop, adb
forward is opened first so that same port reaches the board.
"""

import json
import queue
import re
import shutil
import socket
import subprocess
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

ROOT = Path(__file__).resolve().parent
STATIC = ROOT / "static"
HOST = "0.0.0.0"
PORT = 8080
MCU_HOST = "127.0.0.1"
MCU_PORT = 7500

COMMANDS = re.compile(
    r"^(?:mode (?:idle|servo-cal|servo-run|luna-cal|luna-run|radar|buzzer-cal|buzzer-run)"
    r"|zero \d+"
    r"|cal luna -?\d+"
    r"|buzz (?:hold|\d+)"
    r"|key [adADczmt]"
    r"|tape \d+)$"
)

clients = []
clients_lock = threading.Lock()
commands = queue.Queue()
link_up = False


def publish(payload):
    raw = "data: " + json.dumps(payload) + "\n\n"
    with clients_lock:
        for client in clients:
            if client.qsize() > 200:
                try:
                    client.get_nowait()
                except queue.Empty:
                    pass
            client.put(raw)


def adb_bin():
    found = shutil.which("adb")
    if found:
        return found
    candidate = Path.home() / "Android/Sdk/platform-tools/adb"
    if candidate.exists():
        return str(candidate)
    return None


def forward_mcu():
    adb = adb_bin()
    if not adb:
        return
    subprocess.run(
        [adb, "forward", f"tcp:{MCU_PORT}", f"tcp:{MCU_PORT}"],
        capture_output=True,
        text=True,
        timeout=5,
        check=False,
    )


def mcu_loop():
    global link_up
    while True:
        try:
            try:
                sock = socket.create_connection((MCU_HOST, MCU_PORT), timeout=3)
            except OSError:
                forward_mcu()
                sock = socket.create_connection((MCU_HOST, MCU_PORT), timeout=3)
            sock.settimeout(0.3)
            link_up = True
            publish({"type": "link", "up": True})
            pending = ""
            while True:
                while True:
                    try:
                        line = commands.get_nowait()
                    except queue.Empty:
                        break
                    sock.sendall((line + "\n").encode("utf-8"))
                try:
                    data = sock.recv(4096)
                except socket.timeout:
                    continue
                if not data:
                    break
                pending += data.decode("utf-8", errors="replace")
                while "\n" in pending:
                    line, pending = pending.split("\n", 1)
                    text = line.strip()
                    if text:
                        publish({"type": "line", "text": text})
        except OSError:
            pass
        finally:
            if link_up:
                link_up = False
                publish({"type": "link", "up": False})
        threading.Event().wait(1.5)


class Handler(BaseHTTPRequestHandler):
    def log_message(self, fmt, *args):
        return

    def send_json(self, code, payload):
        body = json.dumps(payload).encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        path = self.path.split("?", 1)[0]
        if path == "/events":
            self.stream_events()
            return
        if path == "/":
            path = "/index.html"
        file_path = (STATIC / path.lstrip("/")).resolve()
        if not str(file_path).startswith(str(STATIC.resolve())) or not file_path.is_file():
            self.send_error(404)
            return
        kind = {
            ".html": "text/html; charset=utf-8",
            ".css": "text/css; charset=utf-8",
            ".js": "text/javascript; charset=utf-8",
        }.get(file_path.suffix, "application/octet-stream")
        data = file_path.read_bytes()
        self.send_response(200)
        self.send_header("Content-Type", kind)
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(data)

    def stream_events(self):
        client = queue.Queue()
        with clients_lock:
            clients.append(client)
        try:
            self.send_response(200)
            self.send_header("Content-Type", "text/event-stream")
            self.send_header("Cache-Control", "no-cache")
            self.send_header("Connection", "keep-alive")
            self.end_headers()
            self.wfile.write(b": ok\n\n")
            self.wfile.flush()
            client.put("data: " + json.dumps({"type": "link", "up": link_up}) + "\n\n")
            while True:
                try:
                    chunk = client.get(timeout=15)
                    self.wfile.write(chunk.encode("utf-8"))
                except queue.Empty:
                    self.wfile.write(b": ping\n\n")
                self.wfile.flush()
        except (BrokenPipeError, ConnectionResetError, OSError):
            pass
        finally:
            with clients_lock:
                if client in clients:
                    clients.remove(client)

    def do_POST(self):
        if self.path.split("?", 1)[0] != "/api/command":
            self.send_error(404)
            return
        length = int(self.headers.get("Content-Length", "0"))
        raw = self.rfile.read(length) if length else b"{}"
        try:
            line = json.loads(raw.decode("utf-8")).get("line", "")
        except json.JSONDecodeError:
            self.send_json(400, {"ok": False, "error": "bad json"})
            return
        line = str(line).strip()
        if not COMMANDS.match(line):
            self.send_json(400, {"ok": False, "error": "unknown command"})
            return
        if not link_up:
            self.send_json(503, {"ok": False, "error": "MCU offline"})
            return
        commands.put(line)
        self.send_json(200, {"ok": True})


def main():
    threading.Thread(target=mcu_loop, daemon=True).start()
    server = ThreadingHTTPServer((HOST, PORT), Handler)
    print(f"radar ui http://127.0.0.1:{PORT}", flush=True)
    server.serve_forever()


if __name__ == "__main__":
    main()
