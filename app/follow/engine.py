from __future__ import annotations

from datetime import datetime
from math import floor

from app.broker import get_broker
from app.broker.base import OrderIntent
from app.models import FollowStartRequest, PortfolioDetail
from app.quotes import fetch_quotes
from app.sources import eastmoney, xueqiu
from app.store import clear_follow, load_follow, save_follow


def _now() -> str:
    return datetime.now().strftime("%Y-%m-%d %H:%M:%S")


def _lot(code: str) -> int:
    num = code.split(".")[0]
    if num.startswith(("11", "12")):
        return 10
    return 100


def _floor_lot(shares: float, code: str) -> int:
    lot = _lot(code)
    return max(0, int(floor(shares / lot) * lot))


def _nav(state: dict, prices: dict[str, float]) -> float:
    equity = 0.0
    for pos in state["positions"]:
        px = prices.get(pos["code"], pos.get("price") or pos.get("cost") or 0)
        equity += pos["shares"] * px
    return state["cash"] + equity


def _load_target(source: str, portfolio_id: str) -> PortfolioDetail:
    if source == "eastmoney":
        return eastmoney.get_portfolio(portfolio_id)
    if source == "xueqiu":
        return xueqiu.get_portfolio(portfolio_id)
    raise ValueError(f"未知数据源: {source}")


def _prices_for(holdings, extra_codes: list[str] | None = None) -> dict[str, float]:
    codes = [h.code for h in holdings]
    if extra_codes:
        codes.extend(extra_codes)
    try:
        fetched = fetch_quotes(codes)
    except Exception:
        fetched = {}
    prices: dict[str, float] = {}
    for holding in holdings:
        quote = fetched.get(holding.code)
        if quote and quote.price:
            prices[holding.code] = quote.price
        elif holding.price:
            prices[holding.code] = holding.price
    for code in extra_codes or []:
        quote = fetched.get(code)
        if quote and quote.price:
            prices[code] = quote.price
    return prices


def _mark(state: dict) -> dict:
    codes = [p["code"] for p in state["positions"]] + [
        h["code"] for h in state.get("target_holdings", [])
    ]
    try:
        fetched = fetch_quotes(codes)
    except Exception:
        fetched = {}
    prices = {}
    names = {}
    for code, quote in fetched.items():
        if quote.price:
            prices[code] = quote.price
        if quote.name:
            names[code] = quote.name
    for pos in state["positions"]:
        if pos["code"] in prices:
            pos["price"] = prices[pos["code"]]
        if pos["code"] in names and not pos.get("name"):
            pos["name"] = names[pos["code"]]
        pos["market_value"] = round(pos["shares"] * (pos.get("price") or pos.get("cost") or 0), 2)
        cost_px = pos.get("cost") or 0
        pos["pnl"] = round((pos.get("price") or cost_px) * pos["shares"] - cost_px * pos["shares"], 2)
        pos["pnl_pct"] = round(pos["pnl"] / (cost_px * pos["shares"]) * 100, 2) if cost_px and pos["shares"] else 0
    nav = _nav(state, prices)
    state["nav"] = round(nav, 2)
    state["pnl"] = round(nav - state["initial"], 2)
    state["pnl_pct"] = round(state["pnl"] / state["initial"] * 100, 2) if state["initial"] else 0
    state["updated_at"] = _now()
    for pos in state["positions"]:
        pos["weight"] = round(pos["market_value"] / nav * 100, 2) if nav else 0
    return state


def _emit_broker_orders(actions: list[dict]) -> list[dict]:
    """非 paper 通道时，把成交意图交给模拟鼠键队列。"""
    broker = get_broker()
    if getattr(broker, "channel", "paper") == "paper":
        return []
    results = []
    for action in actions:
        side = "buy" if "买" in str(action.get("side")) else "sell"
        intent = OrderIntent(
            code=action["code"],
            side=side,
            qty=int(action["shares"]),
            price_type="limit",
            price=float(action.get("price") or 0),
            name=str(action.get("name") or ""),
            reason=str(action.get("reason") or ""),
        )
        try:
            result = broker.place(intent)
            results.append(result.to_dict())
            action["broker_order_id"] = result.order_id
            action["broker_status"] = result.status
            action["client_order_id"] = intent.client_order_id
        except Exception as exc:
            results.append(
                {
                    "client_order_id": intent.client_order_id,
                    "status": "rejected",
                    "message": str(exc),
                }
            )
            action["broker_status"] = "rejected"
            action["broker_error"] = str(exc)
    return results


def _rebalance(state: dict, target: PortfolioDetail) -> dict:
    holdings = [h for h in target.holdings if h.weight and h.weight > 0]
    extra = [p["code"] for p in state["positions"]]
    prices = _prices_for(holdings, extra)
    nav = _nav(state, prices)
    budget = nav * float(state.get("follow_ratio") or 1)
    target_map = {h.code: h for h in holdings}

    sells = []
    for pos in list(state["positions"]):
        target_weight = target_map[pos["code"]].weight if pos["code"] in target_map else 0
        px = prices.get(pos["code"]) or pos.get("price") or pos.get("cost")
        if not px:
            continue
        target_shares = _floor_lot(budget * target_weight / 100 / px, pos["code"]) if target_weight else 0
        delta = pos["shares"] - target_shares
        if delta < _lot(pos["code"]):
            continue
        proceeds = delta * px
        state["cash"] += proceeds
        pos["shares"] -= delta
        sells.append(
            {
                "time": _now(),
                "side": "卖出",
                "code": pos["code"],
                "name": pos["name"],
                "shares": delta,
                "price": px,
                "amount": round(proceeds, 2),
                "reason": "对齐目标仓位",
            }
        )
        if pos["shares"] <= 0:
            state["positions"] = [p for p in state["positions"] if p["code"] != pos["code"]]

    buys = []
    for holding in holdings:
        px = prices.get(holding.code) or holding.price
        if not px:
            continue
        target_shares = _floor_lot(budget * holding.weight / 100 / px, holding.code)
        current = next((p for p in state["positions"] if p["code"] == holding.code), None)
        have = current["shares"] if current else 0
        delta = target_shares - have
        if delta < _lot(holding.code):
            continue
        cost = delta * px
        if cost > state["cash"]:
            delta = _floor_lot(state["cash"] / px, holding.code)
            cost = delta * px
        if delta < _lot(holding.code):
            continue
        state["cash"] -= cost
        if current:
            total_cost = current["cost"] * current["shares"] + cost
            current["shares"] += delta
            current["cost"] = total_cost / current["shares"]
            current["price"] = px
            current["name"] = holding.name or current["name"]
        else:
            state["positions"].append(
                {
                    "code": holding.code,
                    "name": holding.name,
                    "shares": delta,
                    "cost": px,
                    "price": px,
                    "market_value": delta * px,
                    "weight": holding.weight,
                    "pnl": 0,
                    "pnl_pct": 0,
                }
            )
        buys.append(
            {
                "time": _now(),
                "side": "买入",
                "code": holding.code,
                "name": holding.name,
                "shares": delta,
                "price": px,
                "amount": round(cost, 2),
                "reason": "对齐目标仓位",
            }
        )

    actions = sells + buys
    broker_results = _emit_broker_orders(actions)
    state["cash"] = round(state["cash"], 2)
    state["target_holdings"] = [h.model_dump() for h in holdings]
    state["target_name"] = target.name
    state["target_owner"] = target.owner
    state["last_sync_at"] = _now()
    state["trades"] = (actions + state.get("trades", []))[:200]
    state["last_actions"] = actions
    state["last_broker_results"] = broker_results
    return _mark(state)


def start(req: FollowStartRequest) -> dict:
    if req.capital <= 0:
        raise ValueError("模拟资金必须大于 0")
    if not (0 < req.follow_ratio <= 1):
        raise ValueError("跟单比例应在 0 到 1 之间")
    target = _load_target(req.source, req.portfolio_id)
    state = {
        "source": req.source,
        "portfolio_id": target.id,
        "target_name": target.name,
        "target_owner": target.owner,
        "source_url": target.source_url,
        "initial": round(req.capital, 2),
        "cash": round(req.capital, 2),
        "follow_ratio": req.follow_ratio,
        "positions": [],
        "trades": [],
        "started_at": _now(),
        "status": "running",
    }
    state = _rebalance(state, target)
    _append_log(state, "ok", f"开始跟单 {target.name}（{target.id}）")
    return save_follow(state)


def sync() -> dict:
    state = load_follow()
    if not state:
        raise LookupError("还没有开始模拟跟单")
    if state.get("status") == "paused":
        raise ValueError("跟单已暂停，先继续再同步")
    target = _load_target(state["source"], state["portfolio_id"])
    state = _rebalance(state, target)
    _append_log(state, "ok", f"同步 {target.name}，成交 {len(state.get('last_actions') or [])} 笔")
    return save_follow(state)


def pause() -> dict:
    state = load_follow()
    if not state:
        raise LookupError("还没有开始模拟跟单")
    state["status"] = "paused"
    _append_log(state, "ok", f"已暂停 {state.get('target_name') or state.get('portfolio_id')}")
    return save_follow(_mark(state))


def resume() -> dict:
    state = load_follow()
    if not state:
        raise LookupError("还没有开始模拟跟单")
    state["status"] = "running"
    _append_log(state, "ok", f"已继续 {state.get('target_name') or state.get('portfolio_id')}")
    return save_follow(_mark(state))


def snapshot() -> dict | None:
    state = load_follow()
    if not state:
        return None
    return save_follow(_mark(state))


def stop() -> None:
    clear_follow()


def _append_log(state: dict, result: str, message: str, detail: str | None = None) -> None:
    entry = {"time": _now(), "result": result, "message": message}
    if detail:
        entry["detail"] = detail
    state["logs"] = [entry, *(state.get("logs") or [])][:100]
