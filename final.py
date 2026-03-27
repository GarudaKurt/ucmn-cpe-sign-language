import asyncio
import threading
import queue
import subprocess
from bleak import BleakClient, BleakScanner

LEFT_HAND  = "ESP32-LeftHand"
RIGHT_HAND = "ESP32-RightHand"
NOTIFY_UUID = "6E400003-B5A4-F393-E0A9-E50E24DCCA9E"

speech_queue = queue.Queue()

# Separate debounce state per device, per finger
last_spoken = {
    LEFT_HAND:  [""] * 5,
    RIGHT_HAND: [""] * 5,
}

def speak_text(text):
    subprocess.run(
        ["powershell", "-Command",
         f'Add-Type -AssemblyName System.Speech; '
         f'$s = New-Object System.Speech.Synthesis.SpeechSynthesizer; '
         f'$s.Speak("{text}")'],
        creationflags=subprocess.CREATE_NO_WINDOW
    )

def speech_worker():
    while True:
        text = speech_queue.get()
        if text is None:
            break
        speak_text(text)
        speech_queue.task_done()

def make_notification_handler(device_name):
    def handle_notification(sender, data):
        message = data.decode("utf-8").strip()

        # ── Handle RESET per finger ──────────────────────────────────────
        if message.startswith("RESET_"):
            finger_index = int(message.split("_")[1])
            last_spoken[device_name][finger_index] = ""
            print(f"\n[{device_name}] Finger {finger_index} reset — ready")
            return

        # ── Speak if new word ────────────────────────────────────────────
        # Find which finger sent this (match by word)
        for i in range(5):
            if last_spoken[device_name][i] == message:
                return  # already spoken, skip

        print(f"\n[{device_name}] {message}")
        speech_queue.put(message)

        # Mark all fingers of this device as spoken for this word
        for i in range(5):
            if last_spoken[device_name][i] == "":
                last_spoken[device_name][i] = message
                break

    return handle_notification

async def connect_device(device_name):
    while True:  # auto-reconnect loop
        try:
            print(f"Scanning for {device_name}...")
            device = await BleakScanner.find_device_by_name(device_name, timeout=15)
            if not device:
                print(f"{device_name} not found, retrying...")
                await asyncio.sleep(3)
                continue

            async with BleakClient(device) as client:
                print(f"Connected to {device_name}")
                handler = make_notification_handler(device_name)
                await client.start_notify(NOTIFY_UUID, handler)
                while client.is_connected:
                    await asyncio.sleep(1)

            print(f"{device_name} disconnected — reconnecting...")

        except Exception as e:
            print(f"{device_name} error: {e} — retrying in 3s...")
            await asyncio.sleep(3)

async def ble_main():
    # Connect to both hands concurrently
    await asyncio.gather(
        connect_device(LEFT_HAND),
        connect_device(RIGHT_HAND),
    )

def main():
    print("=== Sign Language Text to Speech ===")

    worker = threading.Thread(target=speech_worker, daemon=True)
    worker.start()

    ble_thread = threading.Thread(
        target=lambda: asyncio.run(ble_main()), daemon=True
    )
    ble_thread.start()

    while True:
        text = input("\nEnter text to speak (type 'exit' to quit): ")
        if text.lower() == "exit":
            speech_queue.put(None)
            print("Goodbye!")
            break
        speech_queue.put(text)

if __name__ == "__main__":
    main()