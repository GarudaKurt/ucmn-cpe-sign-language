"""
voice_http.py  —  Windows 10
─────────────────────────────
Listens to the microphone, converts speech to text via Google Speech
Recognition, then HTTP-POSTs each transcription to the ESP32 web server.

Requirements (run once in Command Prompt or PowerShell):
    pip install SpeechRecognition requests pyaudio

If pyaudio fails to install on Windows, grab the prebuilt wheel:
    pip install pipwin
    pipwin install pyaudio

Usage:
    python voice_http.py

Edit ESP32_IP below to match your ESP32's IP (shown in Serial Monitor after boot).
"""

import sys
import time

import requests
import speech_recognition as sr

# ── Config ─────────────────────────────────────────────────────────────────────
ESP32_IP      = "192.168.1.100"          # ← change to your ESP32's IP address
ESP32_PORT    = 80
POST_ENDPOINT = f"http://{ESP32_IP}:{ESP32_PORT}/message"
POST_TIMEOUT  = 5                         # seconds to wait for ESP32 response
# ───────────────────────────────────────────────────────────────────────────────


def send_to_esp32(text: str) -> bool:
    """HTTP POST the transcribed text to the ESP32. Returns True on success."""
    try:
        resp = requests.post(
            POST_ENDPOINT,
            json={"text": text},
            timeout=POST_TIMEOUT,
        )
        if resp.status_code == 200:
            return True
        print(f"  [!] ESP32 returned HTTP {resp.status_code}")
        return False
    except requests.exceptions.ConnectionError:
        print(f"  [!] Cannot reach ESP32 at {ESP32_IP}. Check IP and Wi-Fi.")
        return False
    except requests.exceptions.Timeout:
        print(f"  [!] ESP32 did not respond within {POST_TIMEOUT}s.")
        return False
    except Exception as e:
        print(f"  [!] HTTP error: {e}")
        return False


def check_esp32_reachable() -> bool:
    """Quick connectivity check before starting the listen loop."""
    try:
        r = requests.get(f"http://{ESP32_IP}:{ESP32_PORT}/ping", timeout=3)
        return r.status_code == 200
    except Exception:
        return False


def listen_loop(recognizer: sr.Recognizer, mic: sr.Microphone) -> None:
    """Main loop — capture a phrase, transcribe, send over HTTP."""
    print("\n  Listening... (press Ctrl+C to stop)\n")

    while True:
        # ── Listen ──────────────────────────────────────────────────
        try:
            with mic as source:
                print("  [mic] Speak now...")
                audio = recognizer.listen(source, timeout=10, phrase_time_limit=30)
        except sr.WaitTimeoutError:
            print("  [---] No speech detected, listening again...\n")
            continue
        except KeyboardInterrupt:
            raise

        # ── Transcribe ──────────────────────────────────────────────
        print("  [ * ] Transcribing...")
        try:
            text = recognizer.recognize_google(audio)
        except sr.UnknownValueError:
            print("  [ ? ] Could not understand audio.\n")
            continue
        except sr.RequestError as e:
            print(f"  [net] Google API error: {e}\n")
            continue

        print(f"  [txt] {text}")

        # ── Send ────────────────────────────────────────────────────
        ok = send_to_esp32(text)
        status = "sent to ESP32" if ok else "FAILED to send"
        print(f"  [{'ok' if ok else '!!'}] {status}\n")


def main() -> None:
    print("=" * 54)
    print("   Voice -> HTTP -> ESP32 Web Server  (Windows 10)")
    print("=" * 54)
    print(f"\n  Target: {POST_ENDPOINT}")

    # Connectivity check
    print("\n  Checking ESP32 connection...")
    if check_esp32_reachable():
        print("  [ok] ESP32 is reachable!\n")
    else:
        print("  [!!] ESP32 not reachable. Make sure:")
        print(f"       - ESP32 is powered on and connected to Wi-Fi")
        print(f"       - Your PC is on the same Wi-Fi network")
        print(f"       - IP address '{ESP32_IP}' is correct (check Serial Monitor)")
        print("\n  Continuing anyway — will retry on each message...\n")

    # Set up microphone
    recognizer = sr.Recognizer()
    recognizer.dynamic_energy_threshold = True
    recognizer.pause_threshold = 0.8

    try:
        mic = sr.Microphone()
    except OSError:
        print("\n  [!!] No microphone detected. Please connect one and retry.")
        sys.exit(1)

    # Calibrate for ambient noise once at startup
    print("  Calibrating microphone for ambient noise (1 second)...")
    with mic as source:
        recognizer.adjust_for_ambient_noise(source, duration=1)
    print("  Calibration done.\n")

    # Run
    try:
        listen_loop(recognizer, mic)
    except KeyboardInterrupt:
        print("\n\n  Stopped. Goodbye!")


if __name__ == "__main__":
    main()