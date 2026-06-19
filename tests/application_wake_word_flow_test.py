#!/usr/bin/env python3
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
APPLICATION_CC = ROOT / "main" / "application.cc"


def extract_function_body(source: str, signature: str) -> str:
    start = source.find(signature)
    assert start >= 0, f"missing function: {signature}"

    brace = source.find("{", start)
    assert brace >= 0, f"missing function body: {signature}"

    depth = 0
    for index in range(brace, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[brace + 1:index]

    raise AssertionError(f"unterminated function body: {signature}")


def test_continue_wake_word_invoke_accepts_reused_open_channel():
    source = APPLICATION_CC.read_text()
    body = extract_function_body(
        source,
        "void Application::ContinueWakeWordInvoke(const std::string& wake_word)",
    )

    assert "kDeviceStateConnecting" in body
    assert "kDeviceStateIdle" in body
    assert "audio_channel_opened = protocol_->IsAudioChannelOpened()" in body
    assert "state == kDeviceStateIdle && audio_channel_opened" in body


if __name__ == "__main__":
    test_continue_wake_word_invoke_accepts_reused_open_channel()
