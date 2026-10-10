from __future__ import annotations

from app.broker.mouse_sim import MouseSimBroker
from app.broker.paper import PaperBroker
from app.store import load_settings

_LIVE = {"mouse_sim", "easyths", "file_scan"}

_instance = None
_channel = None


def list_broker_channels() -> list[dict]:
    return [
        {"id": "paper", "label": "仅模拟账户"},
        {"id": "mouse_sim", "label": "模拟鼠键"},
    ]


def normalize_broker_channel(channel: str | None) -> str:
    key = (channel or "paper").strip().lower()
    if key in _LIVE:
        return "mouse_sim"
    return "paper"


def get_broker(channel: str | None = None, *, force_new: bool = False):
    global _instance, _channel
    settings = load_settings()
    key = normalize_broker_channel(channel or settings.get("broker_channel"))
    if not force_new and _instance is not None and _channel == key:
        return _instance
    cls = MouseSimBroker if key == "mouse_sim" else PaperBroker
    _instance = cls()
    _channel = key
    return _instance


def reset_broker() -> None:
    global _instance, _channel
    if _instance is not None:
        try:
            _instance.disconnect()
        except Exception:
            pass
    _instance = None
    _channel = None
