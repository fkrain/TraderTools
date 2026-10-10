from __future__ import annotations

import json
from pathlib import Path
from threading import Lock

ROOT = Path(__file__).resolve().parents[1]
DATA_DIR = ROOT / "data"
DATA_DIR.mkdir(exist_ok=True)

SETTINGS_PATH = DATA_DIR / "settings.json"
FOLLOW_PATH = DATA_DIR / "follow.json"
ORDERS_DIR = DATA_DIR / "orders"

DEFAULT_SETTINGS = {
    "xueqiu_cookie": "",
    "quote_source": "tencent",
    "broker_channel": "paper",
    "orders_dir": str(ORDERS_DIR),
    "mouse_window_title": "",
    "easyths_host": "127.0.0.1",
    "easyths_port": 7648,
    "easyths_api_key": "",
    "easyths_account": "",
}

_lock = Lock()


def _read(path: Path, default):
    if not path.exists():
        return default
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except json.JSONDecodeError:
        return default


def _write(path: Path, payload) -> None:
    tmp = path.with_suffix(".tmp")
    tmp.write_text(json.dumps(payload, ensure_ascii=False, indent=2), encoding="utf-8")
    tmp.replace(path)


def load_settings() -> dict:
    with _lock:
        data = _read(SETTINGS_PATH, {})
    merged = {**DEFAULT_SETTINGS, **data}
    return merged


def save_settings(patch: dict) -> dict:
    with _lock:
        data = _read(SETTINGS_PATH, {})
        data.update({k: v for k, v in patch.items() if v is not None})
        _write(SETTINGS_PATH, data)
    return load_settings()


def public_settings() -> dict:
    s = load_settings()
    cookie = s.get("xueqiu_cookie") or ""
    return {
        "xueqiu_cookie_set": bool(cookie.strip()),
        "xueqiu_cookie_preview": _mask_cookie(cookie),
        "quote_source": _quote_source(s.get("quote_source")),
        "broker_channel": _broker_channel(s.get("broker_channel")),
        "orders_dir": s.get("orders_dir") or str(ORDERS_DIR),
        "mouse_window_title": s.get("mouse_window_title") or "",
        "easyths_host": s.get("easyths_host") or "127.0.0.1",
        "easyths_port": s.get("easyths_port") or 7648,
        "easyths_api_key_set": bool(s.get("easyths_api_key")),
        "easyths_account": s.get("easyths_account") or "",
    }


def _quote_source(value: str | None) -> str:
    key = (value or "tencent").strip().lower()
    if key in {"tencent", "sina", "eastmoney"}:
        return key
    return "tencent"


def _broker_channel(value: str | None) -> str:
    key = (value or "paper").strip().lower()
    if key in {"mouse_sim", "easyths", "file_scan"}:
        return "mouse_sim"
    return "paper"


def _mask_cookie(cookie: str) -> str:
    text = cookie.strip()
    if not text:
        return ""
    if len(text) <= 12:
        return "*" * len(text)
    return f"{text[:6]}...{text[-4:]} ({len(text)} 字)"


def load_follow() -> dict | None:
    with _lock:
        data = _read(FOLLOW_PATH, None)
    return data


def save_follow(payload: dict) -> dict:
    with _lock:
        _write(FOLLOW_PATH, payload)
    return payload


def clear_follow() -> None:
    with _lock:
        if FOLLOW_PATH.exists():
            FOLLOW_PATH.unlink()


def ensure_orders_dirs(root: str | Path | None = None) -> Path:
    base = Path(root or load_settings().get("orders_dir") or ORDERS_DIR)
    for name in ("outbox", "sent", "ack", "done", "reject"):
        (base / name).mkdir(parents=True, exist_ok=True)
    return base
