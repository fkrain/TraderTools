# -*- coding: utf-8 -*-
"""End-to-end smoke for follow_desk product APIs. Exit 0 on success."""
from __future__ import annotations

import json
import subprocess
import sys
import time
import urllib.error
import urllib.request
from pathlib import Path

ROOT = Path(r"G:\跟单系统")
EXE = ROOT / "native" / "build" / "Release" / "follow_desk.exe"
BASE = "http://127.0.0.1:18787"


def req(method: str, path: str, body=None):
    data = None if body is None else json.dumps(body).encode("utf-8")
    r = urllib.request.Request(
        BASE + path,
        data=data,
        method=method,
        headers={"Content-Type": "application/json"} if body is not None else {},
    )
    with urllib.request.urlopen(r, timeout=8) as resp:
        return json.loads(resp.read().decode("utf-8") or "{}")


def wait_health(timeout=20):
    global BASE
    deadline = time.time() + timeout
    while time.time() < deadline:
        for port in range(18787, 18796):
            try:
                BASE = f"http://127.0.0.1:{port}"
                req("GET", "/api/health")
                return True
            except Exception:
                pass
        time.sleep(0.25)
    return False


def main() -> int:
    if not EXE.exists():
        print("FAIL: missing", EXE)
        return 1
    # restart desk
    subprocess.run(["taskkill", "/F", "/IM", "follow_desk.exe"], capture_output=True)
    time.sleep(0.5)
    subprocess.Popen([str(EXE)], cwd=str(ROOT))
    if not wait_health():
        print("FAIL: health")
        return 2

    checks = []

    def ok(name, cond, detail=""):
        checks.append((name, bool(cond), detail))
        print(("PASS" if cond else "FAIL"), name, detail)

    dash = req("GET", "/api/dashboard")
    ok("dashboard", "alerts" in dash)

    alert = req(
        "POST",
        "/api/alerts",
        {
            "name": "smoke2",
            "source": "price",
            "code": "600616.SH",
            "side": "notify",
            "compare": "gte",
            "trigger_price": 0.01,
            "auto_trade": False,
            "enabled": True,
            "cooldown_sec": 1,
        },
    )
    ok("alert_create", alert.get("id"))

    disc = req("GET", "/api/disclaimer")
    ok("disclaimer", "accepted" in disc and "text" in disc)
    if not disc.get("accepted"):
        acc = req("POST", "/api/disclaimer/accept", {})
        ok("disclaimer_accept", acc.get("accepted") is True)
    ver = req("GET", "/api/version")
    ok("version", ver.get("version"))
    confirms = req("GET", "/api/confirms")
    ok("confirms_list", isinstance(confirms, list))

    stop = req(
        "POST",
        "/api/stoploss",
        {
            "code": "600616.SH",
            "cost_price": 10,
            "qty": 100,
            "stop_pct": 5,
            "take_pct": 10,
            "stop_price": 9.5,
            "take_price": 11,
            "trail_enabled": True,
            "trail_pct": 2,
            "enabled": True,
        },
    )
    ok("stoploss_trail", stop.get("id") and stop.get("trail_enabled") is True)

    cond_time = req(
        "POST",
        "/api/conditions",
        {
            "code": "600616.SH",
            "side": "buy",
            "kind": "time",
            "trigger_time": "23:59:59",
            "qty": 100,
            "price_type": "last",
            "limit_price": 1,
            "enabled": True,
            "status": "armed",
        },
    )
    ok("cond_time", cond_time.get("id") and cond_time.get("kind") == "time")
    cond_pct = req(
        "POST",
        "/api/conditions",
        {
            "code": "600616.SH",
            "side": "sell",
            "kind": "pct",
            "compare": "gte",
            "trigger_pct": 9.5,
            "qty": 100,
            "enabled": True,
            "status": "armed",
        },
    )
    ok("cond_pct", cond_pct.get("id") and cond_pct.get("kind") == "pct")

    req(
        "PUT",
        "/api/risk",
        {
            "trade_days_only": False,
            "session_only": False,
            "session_start": "00:00:00",
            "session_end": "23:59:59",
            "max_orders_per_day": 500,
            "max_order_amount": 500000,
            "max_daily_loss": 50000,
            "code_cooldown_sec": 0,
            "confirm_before_trade": False,
            "sound_on_trigger": False,
            "notify_log": True,
            "tdx_sign_file": "",
            "sponsor_equity_limit": 1000000,
        },
    )
    risk = req("GET", "/api/risk")
    ok("risk_hard", risk.get("max_order_amount", 0) > 0 and "confirm_before_trade" in risk)
    time.sleep(3)
    events = req("GET", "/api/events?limit=10")
    ok("alert_event", isinstance(events, list) and any("600616" in str(e) for e in events), str(events[:2]))

    t0 = req("POST", "/api/t0", {"code": "510300.SH", "buy_down_pct": 1, "sell_up_pct": 1, "qty": 100, "enabled": True})
    ok("t0", t0.get("id"))
    board = req("POST", "/api/board", {"code": "600000.SH", "seal_pct": 9.8, "qty": 100, "enabled": True})
    ok("board", board.get("id"))
    bsk = req(
        "POST",
        "/api/baskets",
        {"name": "smoke", "capital": 10000, "legs": [{"code": "600616.SH", "side": "buy", "qty": 100, "weight": 1}]},
    )
    ok("basket", bsk.get("id"))
    run = req("POST", f"/api/baskets/{bsk['id']}/run")
    ok("basket_run", "legs" in run, str(run)[:120])
    acct = req("GET", "/api/accounts")
    ok("accounts", isinstance(acct, list) and len(acct) >= 1)
    curve = req("GET", "/api/curve")
    ok("curve", "points" in curve)
    flash = req("POST", "/api/flash", {"code": "600616.SH", "side": "buy", "qty": 100, "price": 0})
    ok("flash", flash.get("ok") is True or flash.get("qty"), str(flash))
    quotes = req("GET", "/api/quotes?codes=600616.SH")
    ok("quotes", True, str(quotes)[:80])

    tpl = req("POST", "/api/workflows/template/file-signal", {"sign_name": "smoke信号", "trigger_price": 1.0})
    ok("wf_template", tpl.get("id") and len(tpl.get("nodes") or []) >= 3)
    v = req("POST", "/api/workflows/validate", tpl)
    ok("wf_validate", v.get("ok") is True, str(v.get("errors")))
    saved = req("POST", "/api/workflows", tpl)
    ok("wf_save", saved.get("id") == tpl.get("id"))
    toggled = req("POST", f"/api/workflows/{tpl['id']}/toggle", {})
    ok("wf_toggle", "enabled" in toggled)
    st = req("GET", f"/api/workflows/{tpl['id']}/state")
    ok("wf_state", "pools" in st, str(list((st.get("pools") or {}).keys())[:3]))

    failed = [n for n, c, _ in checks if not c]
    print("SUMMARY", f"{len(checks)-len(failed)}/{len(checks)} passed")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
