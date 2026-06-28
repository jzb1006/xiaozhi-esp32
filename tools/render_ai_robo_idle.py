#!/usr/bin/env python3
"""Render the selected Lottie idle marker to a compact GIF for the round LCD."""

from pathlib import Path
import argparse
import io
import json

from PIL import Image
from playwright.sync_api import sync_playwright


def render_idle_gif(lottie_path: Path, lottie_web_path: Path, output_path: Path) -> None:
    data = json.loads(lottie_path.read_text(encoding="utf-8"))
    width = int(data["w"])
    height = int(data["h"])
    frame_indices = list(range(0, 29, 2))

    html = (
        "<!doctype html><html><body style='margin:0;background:transparent'>"
        f"<div id='anim' style='width:{width}px;height:{height}px'></div>"
        "</body></html>"
    )

    frames = []
    with sync_playwright() as playwright:
        browser = playwright.chromium.launch(headless=True)
        page = browser.new_page(viewport={"width": width, "height": height}, device_scale_factor=1)
        page.set_content(html, wait_until="load")
        page.add_script_tag(path=str(lottie_web_path))
        page.evaluate(
            """(animationData) => new Promise((resolve, reject) => {
                const anim = lottie.loadAnimation({
                    container: document.getElementById('anim'),
                    renderer: 'svg',
                    loop: false,
                    autoplay: false,
                    animationData
                });
                window.anim = anim;
                anim.addEventListener('DOMLoaded', () => resolve(true));
                setTimeout(() => reject(new Error('DOMLoaded timeout')), 5000);
            })""",
            data,
        )

        for frame in frame_indices:
            page.evaluate("(frame) => window.anim.goToAndStop(frame, true)", frame)
            page.wait_for_timeout(15)
            png = page.screenshot(omit_background=True)
            image = Image.open(io.BytesIO(png)).convert("RGBA")
            body = image.crop((180, 180, 520, 520))
            body.thumbnail((128, 128), Image.Resampling.LANCZOS)

            canvas = Image.new("RGBA", (128, 128), (255, 255, 255, 255))
            offset = ((128 - body.width) // 2, (128 - body.height) // 2)
            canvas.alpha_composite(body, offset)
            frames.append(canvas.convert("RGB"))

        browser.close()

    output_path.parent.mkdir(parents=True, exist_ok=True)
    frames[0].save(
        output_path,
        save_all=True,
        append_images=frames[1:],
        duration=83,
        loop=0,
        optimize=True,
        disposal=2,
    )


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("lottie", type=Path, help="Path to AI robo.json")
    parser.add_argument("--lottie-web", type=Path, required=True, help="Path to lottie.min.js")
    parser.add_argument(
        "--output",
        type=Path,
        default=Path("main/boards/bread-compact-wifi/assets/ai_robo_idle_128.gif"),
    )
    args = parser.parse_args()

    render_idle_gif(args.lottie, args.lottie_web, args.output)


if __name__ == "__main__":
    main()
