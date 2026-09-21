"""Conservative dimension checks, not a guarantee of memory or frame-rate capacity."""
import json
import subprocess

APPLICATION_MAX = 8192


def summarize(data):
    widths, heights, gpus = [], [], []
    for gpu in data.get("gpus", []):
        keys = ("texture", "renderbuffer", "viewportWidth", "viewportHeight")
        if all(type(gpu.get(k)) is int and gpu[k] > 0 for k in keys):
            widths.append(min(gpu["texture"], gpu["renderbuffer"], gpu["viewportWidth"]))
            heights.append(min(gpu["texture"], gpu["renderbuffer"], gpu["viewportHeight"]))
            gpus.append(gpu)
    for card in data.get("cards", []):
        if type(card.get("maxWidth")) is int and card["maxWidth"] > 0:
            widths.append(card["maxWidth"])
        if type(card.get("maxHeight")) is int and card["maxHeight"] > 0:
            heights.append(card["maxHeight"])
    detected = bool(gpus)
    return {"detected": detected, "complete": detected and len(gpus) == len(data.get("gpus", [])),
            "maxWidth": min([APPLICATION_MAX] + widths), "maxHeight": min([APPLICATION_MAX] + heights),
            "hardwareWidth": min(widths) if widths else None,
            "hardwareHeight": min(heights) if heights else None,
            "applicationMax": APPLICATION_MAX, "gpus": data.get("gpus", []), "cards": data.get("cards", [])}


def detect(renderer):
    try:
        result = subprocess.run([str(renderer), "--graphics-limits"], capture_output=True, text=True, timeout=10, check=True)
        data = json.loads(result.stdout)
        return summarize(data)
    except (OSError, ValueError, TypeError, AttributeError, subprocess.SubprocessError) as exc:
        return {**summarize({}), "error": str(exc)}


def validate_dimensions(layout, limits):
    for index, monitor in enumerate(layout["monitors"], 1):
        if monitor["width"] > limits["maxWidth"] or monitor["height"] > limits["maxHeight"]:
            raise ValueError(f"Monitor {index}: {monitor['width']}×{monitor['height']} exceeds this computer's "
                             f"current per-monitor limit of {limits['maxWidth']}×{limits['maxHeight']} pixels")
