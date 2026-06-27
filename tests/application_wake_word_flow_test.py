#!/usr/bin/env python3
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
APPLICATION_CC = ROOT / "main" / "application.cc"
WEBSOCKET_PROTOCOL_CC = ROOT / "main" / "protocols" / "websocket_protocol.cc"


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


def test_activation_done_opens_push_channel_without_listening():
    source = APPLICATION_CC.read_text()
    body = extract_function_body(
        source,
        "void Application::HandleActivationDoneEvent()",
    )

    assert "EnsurePushChannelOpen()" in body

    push_body = extract_function_body(
        source,
        "void Application::EnsurePushChannelOpen()",
    )
    assert "protocol_->OpenAudioChannel(false)" in push_body
    assert "protocol_->KeepAlive()" in push_body
    assert "SetListeningMode" not in push_body
    assert "SendStartListening" not in push_body


def test_websocket_open_check_does_not_expire_idle_push_channel():
    source = WEBSOCKET_PROTOCOL_CC.read_text()
    body = extract_function_body(
        source,
        "bool WebsocketProtocol::IsAudioChannelOpened() const",
    )

    assert "websocket_->IsConnected()" in body
    assert "IsTimeout()" not in body


def test_websocket_open_failure_drops_half_open_connection():
    source = WEBSOCKET_PROTOCOL_CC.read_text()
    body = extract_function_body(
        source,
        "bool WebsocketProtocol::OpenAudioChannel(bool report_error)",
    )

    assert "websocket_.reset()" in body


def test_tts_finish_plays_completion_sound():
    source = APPLICATION_CC.read_text()
    body = extract_function_body(
        source,
        "void Application::FinishServerPlayback()",
    )

    assert "ServerPlaybackKind::kTts" in body
    assert "!aborted_.load()" in body
    assert "audio_service_.PlaySound(Lang::Sounds::OGG_SUCCESS)" in body


if __name__ == "__main__":
    test_continue_wake_word_invoke_accepts_reused_open_channel()
    test_activation_done_opens_push_channel_without_listening()
    test_websocket_open_check_does_not_expire_idle_push_channel()
    test_websocket_open_failure_drops_half_open_connection()
    test_tts_finish_plays_completion_sound()
