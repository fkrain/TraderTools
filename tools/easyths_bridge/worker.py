# -*- coding: utf-8 -*-
"""Scan follow_desk orders/outbox and submit to local EasyTHS REST API.

Usage:
  python tools/easyths_bridge/worker.py
  python tools/easyths_bridge/worker.py --host 127.0.0.1 --port 7648 --orders data/orders

Requires EasyTHS server running (xiadan.exe logged in), e.g.:
  uvx "easyths[server]"
  or:  scripts\\start_easyths.ps1
"""
from __future__ import annotations

import argparse
import json
import shutil
import sys
import time
import urllib.error
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def load_settings(data_dir: Path) -> dict:
    p = data_dir / "settings.json"
    if not p.exists():
        return {}
    try:
        return json.loads(p.read_text(encoding="utf-8"))
    except Exception:
        return {}


def code6(code: str) -> str:
    s = (code or "").strip().upper()
    if "." in s:
        s = s.split(".", 1)[0]
    digits = "".join(c for c in s if c.isdigit())
    return digits[-6:] if len(digits) >= 6 else digits


class EasyThsClient:
    def __init__(self, host: str, port: int, api_key: str = "", timeout: float = 60.0):
        self.base = f"http://{host}:{port}"
        self.api_key = api_key or ""
        self.timeout = timeout

    def _headers(self) -> dict:
        h = {"Content-Type": "application/json"}
        if self.api_key:
            h["Authorization"] = f"Bearer {self.api_key}"
        return h

    def request(self, method: str, path: str, body: dict | None = None) -> dict:
        data = None if body is None else json.dumps(body).encode("utf-8")
        req = urllib.request.Request(
            self.base + path, data=data, method=method, headers=self._headers()
        )
        try:
            with urllib.request.urlopen(req, timeout=self.timeout) as resp:
                raw = resp.read().decode("utf-8") or "{}"
                return json.loads(raw)
        except urllib.error.HTTPError as e:
            raw = e.read().decode("utf-8", errors="replace")
            try:
                return json.loads(raw)
            except Exception:
                return {"success": False, "status": "error", "message": raw or str(e)}
        except Exception as e:
            return {"success": False, "status": "error", "message": str(e)}

    def health(self) -> dict:
        return self.request("GET", "/api/v1/system/health")

    def execute(self, op: str, params: dict) -> dict:
        queued = self.request("POST", f"/api/v1/operations/{op}", params)
        if queued.get("success") is False and queued.get("status") == "error":
            return queued
        op_id = None
        data = queued.get("data") or {}
        if isinstance(data, dict):
            op_id = data.get("operation_id")
        op_id = op_id or queued.get("operation_id")
        if not op_id:
            # some versions may return final result directly
            if queued.get("success") is True or queued.get("status") in ("success", "failed", "completed"):
                return queued
            return {"success": False, "status": "error", "message": f"no operation_id: {queued}"}
        # poll result
        deadline = time.time() + self.timeout
        while time.time() < deadline:
            result = self.request("GET", f"/api/v1/operations/{op_id}/result")
            st = (result.get("status") or "").lower()
            if result.get("success") is True or st in ("success", "completed", "done"):
                return result
            if result.get("success") is False or st in ("failed", "error", "cancelled"):
                return result
            # still queued/running
            time.sleep(0.4)
        return {"success": False, "status": "timeout", "message": f"timeout waiting {op_id}"}


def move_order(src: Path, dest_dir: Path, extra: dict | None = None) -> None:
    dest_dir.mkdir(parents=True, exist_ok=True)
    dest = dest_dir / src.name
    try:
        obj = json.loads(src.read_text(encoding="utf-8"))
    except Exception:
        obj = {}
    if extra:
        obj.update(extra)
    obj["bridge"] = "easyths"
    dest.write_text(json.dumps(obj, ensure_ascii=False, indent=2), encoding="utf-8")
    try:
        src.unlink()
    except Exception:
        shutil.move(str(src), str(dest.with_suffix(dest.suffix + ".dup")))


def submit_one(client: EasyThsClient, order: dict, account: str) -> dict:
    side = (order.get("side") or "buy").lower()
    qty = int(order.get("qty") or 0)
    price = float(order.get("price") or 0)
    ptype = (order.get("price_type") or "limit").lower()
    code = code6(order.get("code") or "")
    if not code or qty <= 0:
        return {"success": False, "message": "invalid code/qty"}

    params: dict = {"stock_code": code, "quantity": qty}
    if account:
        params["account_name"] = account

    if ptype == "market":
        op = "market_buy" if side == "buy" else "market_sell"
    else:
        if price <= 0:
            return {"success": False, "message": "limit order needs price"}
        params["price"] = price
        op = "buy" if side == "buy" else "sell"

    return client.execute(op, params)


def process_outbox(client: EasyThsClient, orders_dir: Path, account: str) -> int:
    outbox = orders_dir / "outbox"
    if not outbox.exists():
        return 0
    n = 0
    files = sorted(outbox.glob("*.json"))
    for f in files:
        if f.name.endswith(".tmp"):
            continue
        try:
            order = json.loads(f.read_text(encoding="utf-8"))
        except Exception as e:
            move_order(f, orders_dir / "reject", {"reject_reason": f"bad json: {e}"})
            continue
        # mark sent first to avoid double-submit on crash mid-flight
        sent_path = orders_dir / "sent" / f.name
        (orders_dir / "sent").mkdir(parents=True, exist_ok=True)
        try:
            shutil.copy2(f, sent_path)
            f.unlink()
        except Exception as e:
            print("move to sent failed", f.name, e)
            continue

        cid = str(order.get("client_order_id") or "")
        cancel_dir = orders_dir / "cancel"
        if cid and cancel_dir.exists() and any(p.name.startswith(cid) or cid in p.name for p in cancel_dir.glob("*.json")):
            move_order(sent_path, orders_dir / "reject", {"status": "cancelled", "reject_reason": "prebury cancel"})
            print("  CANCEL", cid)
            n += 1
            continue
        print(f"submit {order.get('side')} {order.get('code')} x{order.get('qty')} @ {order.get('price')}")
        result = submit_one(client, order, account)
        ok = result.get("success") is True
        extra = {
            "easyths_result": result,
            "finished_at": time.strftime("%Y-%m-%d %H:%M:%S"),
        }
        if ok:
            extra["status"] = "filled"
            extra["filled_qty"] = int(order.get("qty") or 0)
            avg = order.get("price") or 0
            for key in ("avg_price", "price", "deal_price"):
                if isinstance(result.get(key), (int, float)) and result.get(key):
                    avg = result[key]
                    break
            extra["avg_price"] = float(avg or 0)
            move_order(sent_path, orders_dir / "done", extra)
            print("  OK", result.get("message") or result.get("status"))
        else:
            move_order(sent_path, orders_dir / "reject", {**extra, "reject_reason": result.get("message") or str(result)})
            print("  FAIL", result.get("message") or result)
        n += 1
    return n


def main() -> int:
    ap = argparse.ArgumentParser(description="EasyTHS outbox bridge for follow_desk")
    ap.add_argument("--data", default=str(ROOT / "data"), help="data dir with settings.json")
    ap.add_argument("--orders", default="", help="orders dir (default from settings)")
    ap.add_argument("--host", default="", help="easyths host")
    ap.add_argument("--port", type=int, default=0, help="easyths port")
    ap.add_argument("--api-key", default="", help="easyths API key")
    ap.add_argument("--account", default="", help="easyths account_name")
    ap.add_argument("--interval", type=float, default=1.0, help="poll seconds")
    ap.add_argument("--once", action="store_true", help="single scan then exit")
    args = ap.parse_args()

    data_dir = Path(args.data)
    settings = load_settings(data_dir)
    host = args.host or settings.get("easyths_host") or "127.0.0.1"
    port = args.port or int(settings.get("easyths_port") or 7648)
    api_key = args.api_key or settings.get("easyths_api_key") or ""
    account = args.account or settings.get("easyths_account") or ""
    orders = Path(args.orders) if args.orders else Path(settings.get("orders_dir") or (data_dir / "orders"))

    client = EasyThsClient(host, port, api_key)
    print(f"bridge → {client.base}  orders={orders}")
    h = client.health()
    if h.get("success") is False and h.get("status") == "error":
        print("WARN: EasyTHS health failed:", h.get("message"))
        print("Start server first: scripts\\start_easyths.ps1  (or uvx \"easyths[server]\")")
    else:
        print("EasyTHS health:", json.dumps(h, ensure_ascii=False)[:200])

    while True:
        try:
            process_outbox(client, orders, account)
        except KeyboardInterrupt:
            print("bye")
            return 0
        except Exception as e:
            print("loop error:", e)
        if args.once:
            return 0
        time.sleep(max(0.2, args.interval))


if __name__ == "__main__":
    sys.exit(main())
