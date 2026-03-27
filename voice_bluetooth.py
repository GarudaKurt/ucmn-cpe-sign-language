"""
voice_bluetooth.py
------------------
Listens to the microphone, converts speech to text via Google Speech Recognition,
then sends each transcribed line to an ESP32 over Bluetooth Classic (RFCOMM).

Requirements:
    pip install SpeechRecognition pyaudio

Bluetooth pairing must be done beforehand:
    bluetoothctl
        > scan on
        > pair <ESP32_MAC>
        > trust <ESP32_MAC>
        > connect <ESP32_MAC>

Edit ESP32_MAC below to match your board's Bluetooth address.
"""

import bluetooth
import speech_recognition as sr
import sys
import time

# ── Config ─────────────────────────────────────────────────────────────────────
ESP32_MAC  = "AA:BB:CC:DD:EE:FF"   # ← replace with your ESP32's MAC address
ESP32_PORT = 1                      # RFCOMM channel (must match ESP32 sketch)
RECONNECT_DELAY = 3                 # seconds to wait before retrying a lost connection
# ───────────────────────────────────────────────────────────────────────────────


def connect_bluetooth(mac: str, port: int) -> bluetooth.BluetoothSocket:
    """Open a Bluetooth RFCOMM socket to the ESP32, retrying until connected."""
    while True:
        try:
            print(f"🔵  Connecting to ESP32 at {mac}:{port} …")
            sock = bluetooth.BluetoothSocket(bluetooth.RFCOMM)
            sock.connect((mac, port))
            print("✅  Bluetooth connected!\n")
            return sock
        except bluetooth.BluetoothError as e:
            print(f"⚠️   Connection failed: {e}. Retrying in {RECONNECT_DELAY}s …")
            time.sleep(RECONNECT_DELAY)


def send_text(sock: bluetooth.BluetoothSocket, text: str) -> bool:
    """Send a UTF-8 string terminated with a newline. Returns False on error."""
    try:
        payload = (text.strip() + "\n").encode("utf-8")
        sock.send(payload)
        return True
    except bluetooth.BluetoothError as e:
        print(f"❌  Send failed: {e}")
        return False


def listen_loop(sock: bluetooth.BluetoothSocket) -> None:
    """Main loop: capture audio, transcribe, send over Bluetooth."""
    recognizer = sr.Recognizer()
    recognizer.dynamic_energy_threshold = True
    recognizer.pause_threshold = 0.8

    mic = sr.Microphone()
    print("🎙️  Listening… (press Ctrl+C to quit)\n")

    with mic as source:
        recognizer.adjust_for_ambient_noise(source, duration=1)

    while True:
        # ── Listen ──────────────────────────────────────────────────
        try:
            with mic as source:
                print("🎤  Speak now…")
                audio = recognizer.listen(source, timeout=10, phrase_time_limit=30)
        except sr.WaitTimeoutError:
            print("⏱️  No speech detected, listening again…\n")
            continue
        except KeyboardInterrupt:
            raise

        # ── Transcribe ──────────────────────────────────────────────
        print("⚙️  Transcribing…")
        try:
            text = recognizer.recognize_google(audio)
        except sr.UnknownValueError:
            print("❓  Could not understand audio.\n")
            continue
        except sr.RequestError as e:
            print(f"🌐  Google API error: {e}\n")
            continue

        print(f"📝  Transcribed: {text}")

        # ── Send ────────────────────────────────────────────────────
        ok = send_text(sock, text)
        if not ok:
            print("🔄  Reconnecting…")
            try:
                sock.close()
            except Exception:
                pass
            sock = connect_bluetooth(ESP32_MAC, ESP32_PORT)

        print()  # blank line for readability


def main() -> None:
    print("=" * 52)
    print("   🎙️  Voice → Bluetooth → ESP32 Web Server")
    print("=" * 52)

    sock = connect_bluetooth(ESP32_MAC, ESP32_PORT)

    try:
        listen_loop(sock)
    except KeyboardInterrupt:
        print("\n\n👋  Stopped by user.")
    finally:
        try:
            sock.close()
        except Exception:
            pass
        print("🔌  Bluetooth socket closed.")


if __name__ == "__main__":
    main()
