from __future__ import annotations

import json
from pathlib import Path
from threading import Lock

from app.grid.models import GridStrategy

ROOT = Path(__file__).resolve().parents[2]
DATA_DIR = ROOT / "data"
GRID_PATH = DATA_DIR / "grid.json"
_lock = Lock()

_DEFAULTS = [
    GridStrategy(
        code="601288.SH",
        name="农业银行",
        status="running",
        last_price=6.97,
        last_qty=1000,
        buy_qty=1000,
        sell_qty=0,
        sell_up_pct=1.0,
        buy_down_pct=1.0,
        price_ceil=8.5,
        price_floor=5.0,
        range_pnl=186.4,
        range_save=240.0,
        note="低价股示意",
    ).model_dump(),
    GridStrategy(
        code="600019.SH",
        name="宝钢股份",
        status="paused",
        last_price=6.88,
        last_qty=500,
        buy_qty=500,
        sell_up_pct=2.0,
        buy_down_pct=2.0,
        rebound_pct=0.3,
        pullback_pct=0.3,
        preorder_sell=True,
        check_fill=True,
        range_pnl=52.0,
        range_save=80.0,
    ).model_dump(),
]


def _read() -> list[dict]:
    DATA_DIR.mkdir(exist_ok=True)
    if not GRID_PATH.exists():
        _write(_DEFAULTS)
        return list(_DEFAULTS)
    try:
        data = json.loads(GRID_PATH.read_text(encoding="utf-8"))
        return data if isinstance(data, list) else list(_DEFAULTS)
    except json.JSONDecodeError:
        return list(_DEFAULTS)


def _write(items: list[dict]) -> None:
    DATA_DIR.mkdir(exist_ok=True)
    tmp = GRID_PATH.with_suffix(".tmp")
    tmp.write_text(json.dumps(items, ensure_ascii=False, indent=2), encoding="utf-8")
    tmp.replace(GRID_PATH)


def list_strategies() -> list[GridStrategy]:
    with _lock:
        return [GridStrategy.model_validate(x) for x in _read()]


def get_strategy(sid: str) -> GridStrategy | None:
    for item in list_strategies():
        if item.id == sid:
            return item
    return None


def add_strategy(item: GridStrategy) -> GridStrategy:
    with _lock:
        rows = _read()
        rows.append(item.model_dump())
        _write(rows)
    return item


def update_strategy(sid: str, patch: dict) -> GridStrategy | None:
    with _lock:
        rows = _read()
        for i, row in enumerate(rows):
            if row.get("id") == sid:
                row.update({k: v for k, v in patch.items() if v is not None})
                rows[i] = row
                _write(rows)
                return GridStrategy.model_validate(row)
    return None


def delete_strategy(sid: str) -> bool:
    with _lock:
        rows = _read()
        new_rows = [r for r in rows if r.get("id") != sid]
        if len(new_rows) == len(rows):
            return False
        _write(new_rows)
    return True


def summary() -> dict:
    items = list_strategies()
    return {
        "count": len(items),
        "running": sum(1 for x in items if x.status == "running"),
        "range_pnl": round(sum(x.range_pnl for x in items), 2),
        "range_save": round(sum(x.range_save for x in items), 2),
    }
