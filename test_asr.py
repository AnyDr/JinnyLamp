import whisper

print("Loading model… (this can take ~10–30 seconds)")
model = whisper.load_model("tiny")  # Можно заменить на "base", если ПК тянет

print("Transcribing…")
result = model.transcribe("dump.wav", language="ru")

print("\n=== RESULT ===")
print(result["text"])
