"""
flex_tts_filipino.py
────────────────────────────────────────────────────────────────────────────
Sign Language Text-to-Speech — Filipino/Tagalog phrases
Receives BLE notifications from one or two ESP32 flex-sensor gloves and
speaks the detected phrase immediately using gTTS (Google TTS, Filipino).

Protocol expected from ESP32:
  "Kamusta"   → speak the phrase
  "RESET"     → open hand, clear device state (matches ESP32 .ino)

Install:
    pip install bleak gtts pygame

Run:
    python flex_tts_filipino.py
────────────────────────────────────────────────────────────────────────────
"""

import asyncio
import threading
import queue
import os
import tempfile
from gtts import gTTS
import pygame
from bleak import BleakClient, BleakScanner

# ── Init pygame mixer for audio playback ──────────────────────────────────
pygame.mixer.init()

# ── BLE device names (must match DEVICE_BLE_NAME in .ino) ─────────────────
LEFT_HAND  = "ESP32-LeftHand"
RIGHT_HAND = "ESP32-RightHand"   # comment out / set to None if not used yet
NOTIFY_UUID = "6E400003-B5A4-F393-E0A9-E50E24DCCA9E"

# ── Set to False to skip RIGHT_HAND entirely until you have the second glove
USE_RIGHT_HAND = False

# ── Speech queue (single worker keeps speech serial, never overlapping) ───
speech_queue = queue.Queue()

# ── Per-device: just track the last spoken word (simple string, not a list)
last_spoken = {
    LEFT_HAND:  "",
    RIGHT_HAND: "",
}

# ── Filipino phrase reference ─────────────────────────────────────────────
PHRASE_GUIDE = """
╔══════════════════════════════════════════════════════════════════════╗
║           FLEX GLOVE — FILIPINO PHRASE GUIDE                         ║
╠════════════════════════╦═════════════════════════════════════════════╣
║  GESTURE               ║  PHRASE                                     ║
╠════════════════════════╬═════════════════════════════════════════════╣
║ Thumb                  ║  Kamusta          (Hello / How are you)     ║
║ Index                  ║  Salamat          (Thank you)               ║
║ Middle                 ║  Tulong           (Help)                    ║
║ Ring                   ║  Oo               (Yes)                     ║
║ Pinky                  ║  Hindi            (No)                      ║
╠════════════════════════╬═════════════════════════════════════════════╣
║ Thumb + Index          ║  Magandang umaga  (Good morning)            ║
║ Index + Middle         ║  Magandang hapon  (Good afternoon)          ║
║ Middle + Ring          ║  Magandang gabi   (Good evening)            ║
║ Ring + Pinky           ║  Paalam           (Goodbye)                 ║
║ Thumb + Pinky          ║  Paumanhin        (Sorry / Excuse me)       ║
║ Thumb + Middle         ║  Pakiulit         (Please repeat)           ║
║ Index + Ring           ║  Hindi ko alam    (I don't know)            ║
║ Index + Pinky          ║  Gutom ako        (I am hungry)             ║
║ Middle + Pinky         ║  Uhaw ako         (I am thirsty)            ║
║ Thumb + Ring           ║  Mahal kita       (I love you)              ║
╠════════════════════════╬═════════════════════════════════════════════╣
║ Thumb+Index+Middle     ║  Saan ang CR      (Where is the restroom)   ║
║ Index+Middle+Ring      ║  Ayaw ko          (I don't want)            ║
║ Middle+Ring+Pinky      ║  Gusto ko         (I want)                  ║
║ Thumb+Index+Pinky      ║  Ano ito          (What is this)            ║
║ Thumb+Ring+Pinky       ║  Tawagan mo ako   (Call me)                 ║
║ Thumb+Index+Ring       ║  Kailangan ko ng tulong  (I need help)      ║
║ Index+Middle+Pinky     ║  Masaya ako       (I am happy)              ║
║ Index+Ring+Pinky       ║  Malungkot ako    (I am sad)                ║
╠════════════════════════╬═════════════════════════════════════════════╣
║ All except Pinky       ║  Hintay ka        (Wait)                    ║
║ All except Thumb       ║  Halika na        (Come here / Let's go)    ║
║ All except Index       ║  Ingat ka         (Take care)               ║
║ All except Middle      ║  Sige na          (Go ahead / Please)       ║
║ All except Ring        ║  Mag-ingat        (Be careful)              ║
╠════════════════════════╬═════════════════════════════════════════════╣
║ ALL 5 fingers bent     ║  Mabuhay          (Cheers / Long live!)     ║
║ Open hand (none bent)  ║  [Reset — ready for next gesture]           ║
╚════════════════════════╩═════════════════════════════════════════════╝
"""

# ─────────────────────────────────────────────────────────────────────────
#  TTS — Windows PowerShell / SAPI
# ─────────────────────────────────────────────────────────────────────────

def speak_text(text: str):
    """Speak using Google TTS with Filipino (Tagalog) accent."""
    try:
        tts = gTTS(text=text, lang="tl")   # 'fil' = Filipino / Tagalog
        with tempfile.NamedTemporaryFile(delete=False, suffix=".mp3") as f:
            tmp_path = f.name
        tts.save(tmp_path)
        pygame.mixer.music.load(tmp_path)
        pygame.mixer.music.play()
        while pygame.mixer.music.get_busy():
            pygame.time.Clock().tick(10)
        pygame.mixer.music.unload()
        os.remove(tmp_path)
    except Exception as e:
        print(f"  ⚠️  TTS error: {e}")

def speech_worker():
    """Single background thread — processes speech queue serially."""
    while True:
        text = speech_queue.get()
        if text is None:
            break
        speak_text(text)
        speech_queue.task_done()

# ─────────────────────────────────────────────────────────────────────────
#  BLE notification handler
# ─────────────────────────────────────────────────────────────────────────

def make_notification_handler(device_name: str):
    def handle_notification(sender, data: bytearray):
        message = data.decode("utf-8").strip()
        print(f"[{device_name}] RAW received: '{message}'")   # ← debug line

        # ── RESET: ESP32 sends "RESET" on open hand ────────────────────
        # Handles all variants: "RESET", "RESET_ALL", "RESET_0" etc.
        if message.upper().startswith("RESET"):
            last_spoken[device_name] = ""
            print(f"[{device_name}] ✋ Hand open — ready for next gesture")
            return

        # ── Duplicate guard: don't re-speak same phrase until RESET ────
        if message == last_spoken[device_name]:
            print(f"[{device_name}] (duplicate '{message}' — waiting for RESET)")
            return

        # ── Speak it ───────────────────────────────────────────────────
        print(f"\n{'═'*54}")
        print(f"  Hand   : {device_name}")
        print(f"  Phrase : {message}")
        print(f"{'═'*54}\n")

        last_spoken[device_name] = message
        speech_queue.put(message)

    return handle_notification

# ─────────────────────────────────────────────────────────────────────────
#  BLE connection loop (auto-reconnects on drop)
# ─────────────────────────────────────────────────────────────────────────

async def connect_device(device_name: str):
    while True:
        try:
            print(f"Scanning for {device_name}...")
            device = await BleakScanner.find_device_by_name(device_name, timeout=15)

            if not device:
                print(f"  {device_name} not found — retrying in 5 s...")
                await asyncio.sleep(5)
                continue

            print(f"  Found {device_name} at {device.address} — connecting...")

            async with BleakClient(device) as client:
                print(f"  ✅  Connected to {device_name}")
                handler = make_notification_handler(device_name)
                await client.start_notify(NOTIFY_UUID, handler)

                while client.is_connected:
                    await asyncio.sleep(1)

                print(f"  ⚠️  {device_name} disconnected — reconnecting...")

        except Exception as exc:
            print(f"  ❌  {device_name} error: {exc} — retrying in 3 s...")
            await asyncio.sleep(3)

# ─────────────────────────────────────────────────────────────────────────
#  Entry point
# ─────────────────────────────────────────────────────────────────────────

async def ble_main():
    tasks = [connect_device(LEFT_HAND)]
    if USE_RIGHT_HAND:
        tasks.append(connect_device(RIGHT_HAND))
    await asyncio.gather(*tasks)


def main():
    print("=" * 56)
    print("   Sign Language Text-to-Speech  —  Filipino / Tagalog")
    print("=" * 56)
    print(PHRASE_GUIDE)

    # Start TTS worker thread
    worker = threading.Thread(target=speech_worker, daemon=True)
    worker.start()

    # Start BLE listener thread
    ble_thread = threading.Thread(
        target=lambda: asyncio.run(ble_main()), daemon=True
    )
    ble_thread.start()

    # Manual input
    while True:
        try:
            text = input("\nManual speak (or 'exit'): ").strip()
            if text.lower() == "exit":
                speech_queue.put(None)
                print("Goodbye!")
                break
            if text:
                speech_queue.put(text)
        except KeyboardInterrupt:
            speech_queue.put(None)
            print("\nGoodbye!")
            break


if __name__ == "__main__":
    main()