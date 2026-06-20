#!/usr/bin/env python3
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
BOARD_CC = ROOT / "main" / "boards" / "muselab-nanoesp32-c6-pdm" / "muselab_nanoesp32_c6_pdm_board.cc"
CONFIG_H = ROOT / "main" / "boards" / "muselab-nanoesp32-c6-pdm" / "config.h"


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


def test_muselab_c6_uses_shared_i2s_duplex_audio_codec():
    source = BOARD_CC.read_text()
    body = extract_function_body(source, "virtual AudioCodec* GetAudioCodec() override")

    assert "NoAudioCodecDuplex" in body
    assert "NoAudioCodecSimplexRawPdm" not in body
    assert "AUDIO_PDM_SAMPLE_RATE" not in body
    assert "AUDIO_I2S_SPK_GPIO_BCLK" in body
    assert "AUDIO_I2S_SPK_GPIO_LRCK" in body
    assert "AUDIO_I2S_MIC_GPIO_DIN" in body


def test_muselab_c6_shared_i2s_clock_stays_at_16khz():
    source = CONFIG_H.read_text()

    assert "#define AUDIO_INPUT_SAMPLE_RATE  16000" in source
    assert "#define AUDIO_OUTPUT_SAMPLE_RATE 16000" in source


if __name__ == "__main__":
    test_muselab_c6_uses_shared_i2s_duplex_audio_codec()
    test_muselab_c6_shared_i2s_clock_stays_at_16khz()
