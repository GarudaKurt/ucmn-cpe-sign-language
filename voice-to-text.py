"""
Voice to Text Console App
-------------------------
Listens to your microphone and converts speech to text.

Requirements:
    pip install SpeechRecognition pyaudio

On Linux, you may also need:
    sudo apt-get install portaudio19-dev python3-pyaudio

On macOS:
    brew install portaudio
"""

import speech_recognition as sr
import sys


def listen_once(recognizer: sr.Recognizer, mic: sr.Microphone) -> str | None:
    """Capture one phrase from the microphone and return transcribed text."""
    print("\n🎤  Listening... (speak now, then pause)")
    with mic as source:
        recognizer.adjust_for_ambient_noise(source, duration=0.5)
        try:
            audio = recognizer.listen(source, timeout=10, phrase_time_limit=30)
        except sr.WaitTimeoutError:
            print("⏱️  No speech detected within timeout.")
            return None

    print("⚙️  Transcribing...")
    try:
        text = recognizer.recognize_google(audio)
        return text
    except sr.UnknownValueError:
        print("❓  Could not understand audio.")
        return None
    except sr.RequestError as e:
        print(f"❌  API error: {e}")
        return None


def run_continuous(recognizer: sr.Recognizer, mic: sr.Microphone) -> None:
    """Keep listening until the user says 'quit' or presses Ctrl+C."""
    print("\n📋  Mode: Continuous  (say 'quit' or 'exit' to stop, or press Ctrl+C)")
    all_text: list[str] = []

    while True:
        text = listen_once(recognizer, mic)
        if text:
            print(f"\n📝  You said: {text}")
            all_text.append(text)
            if text.strip().lower() in {"quit", "exit", "stop"}:
                break

    if all_text:
        print("\n=== Full Transcript ===")
        for i, line in enumerate(all_text, 1):
            print(f"  {i}. {line}")


def run_single(recognizer: sr.Recognizer, mic: sr.Microphone) -> None:
    """Listen for a single phrase and print it."""
    print("\n📋  Mode: Single phrase")
    text = listen_once(recognizer, mic)
    if text:
        print(f"\n📝  Transcription: {text}")


def print_banner() -> None:
    print("=" * 50)
    print("       🎙️  Voice to Text — Console App")
    print("=" * 50)
    print("  Uses Google Speech Recognition (requires internet)")


def choose_mode() -> str:
    print("\nChoose a mode:")
    print("  1 - Single phrase (listen once and stop)")
    print("  2 - Continuous   (keep listening until you say 'quit')")
    print("  q - Quit")
    return input("\nYour choice: ").strip().lower()


def main() -> None:
    print_banner()

    recognizer = sr.Recognizer()
    recognizer.energy_threshold = 300          # sensitivity; lower = more sensitive
    recognizer.dynamic_energy_threshold = True # auto-adjust for background noise
    recognizer.pause_threshold = 0.8           # seconds of silence that ends a phrase

    # Check for available microphone
    try:
        mic = sr.Microphone()
    except OSError:
        print("\n❌  No microphone found. Please connect one and try again.")
        sys.exit(1)

    choice = choose_mode()

    try:
        if choice == "1":
            run_single(recognizer, mic)
        elif choice == "2":
            run_continuous(recognizer, mic)
        elif choice == "q":
            print("👋  Goodbye!")
        else:
            print("⚠️  Invalid choice. Exiting.")
    except KeyboardInterrupt:
        print("\n\n👋  Interrupted by user. Goodbye!")


if __name__ == "__main__":
    main()