import asyncio
import threading
import queue
import subprocess
from bleak import BleakClient, BleakScanner

DEVICE_NAME = "ESP32-FlexSensor"
NOTIFY_UUID  = "6E400003-B5A4-F393-E0A9-E50E24DCCA9E"

speech_queue = queue.Queue()
last_spoken = ""

def speak_text(text):
    """Use Windows built-in PowerShell TTS — no pyttsx3 issues."""
    subprocess.run(
        ["powershell", "-Command", f'Add-Type -AssemblyName System.Speech; '
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

def handle_notification(sender, data):
    global last_spoken
    message = data.decode("utf-8").strip()

    if message == "RESET":
        last_spoken = ""
        print("\n[Ready for next gesture]")
        return

    if message and message != last_spoken:
        print("\nESP32:", message)
        last_spoken = message
        speech_queue.put(message)

async def ble_listener():
    print("Scanning for ESP32-FlexSensor...")
    device = await BleakScanner.find_device_by_name(DEVICE_NAME, timeout=10)
    if not device:
        print("Device not found!")
        return

    async with BleakClient(device) as client:
        print(f"Connected to {DEVICE_NAME}")
        await client.start_notify(NOTIFY_UUID, handle_notification)
        while True:
            await asyncio.sleep(1)

def main():
    print("=== BLE Text to Speech ===")

    worker = threading.Thread(target=speech_worker, daemon=True)
    worker.start()

    ble_thread = threading.Thread(
        target=lambda: asyncio.run(ble_listener()), daemon=True
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