import speech_recognition as sr
import requests

ESP32_IP = "192.168.1.100"   # your ESP32 IP
ENDPOINT = f"http://{ESP32_IP}:80/message"

def send_to_esp32(text):
    try:
        response = requests.post(ENDPOINT, json={"text": text}, timeout=5)
        print(f"[SENT] {text}")
        print(f"[ESP32] Status: {response.status_code}")
    except requests.exceptions.ConnectionError:
        print("[ERROR] Cannot reach ESP32 — check IP and WiFi")
    except requests.exceptions.Timeout:
        print("[ERROR] ESP32 did not respond in time")

def main():
    recognizer = sr.Recognizer()

    # ── Tune these to control pause sensitivity ──
    recognizer.pause_threshold = 0.8      # seconds of silence to end phrase
    recognizer.energy_threshold = 300     # mic sensitivity (raise if too noisy)
    recognizer.dynamic_energy_threshold = True  # auto-adjusts to environment

    mic = sr.Microphone()

    print("Calibrating microphone...")
    with mic as source:
        recognizer.adjust_for_ambient_noise(source, duration=2)
    print(f"Energy threshold set to: {recognizer.energy_threshold:.0f}")
    print(f"Sending to: {ENDPOINT}")
    print("Listening... (speak naturally, pause to send)\n")

    while True:
        try:
            with mic as source:
                print("🎤 Waiting for speech...")
                audio = recognizer.listen(
                    source,
                    timeout=None,
                    phrase_time_limit=15
                )

            print("Transcribing...")
            text = recognizer.recognize_google(audio)
            print(f"✅ Transcribed: '{text}'")
            send_to_esp32(text)          # ← sends to ESP32 after every pause

        except sr.UnknownValueError:
            print("Could not understand, try again.")
        except sr.RequestError as e:
            print(f"Speech service error: {e}")
        except KeyboardInterrupt:
            print("\nStopped.")
            break

if __name__ == "__main__":
    main()