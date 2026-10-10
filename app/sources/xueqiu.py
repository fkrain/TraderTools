from __future__ import annotations

from curl_cffi import requests as cffi_requests

from app.models import Holding, PortfolioCard, PortfolioDetail, RebalanceItem
from app.sources.quotes import guess_market, normalize_code
from app.store import load_settings

HEADERS = {
    "User-Agent": (
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64) "
        "AppleWebKit/537.36 (KHTML, like Gecko) Chrome/122.0.0.0 Safari/537.36"
    ),
    "Accept": "application/json, text/plain, */*",
    "Accept-Language": "zh-CN,zh;q=0.9",
    "X-Requested-With": "XMLHttpRequest",
}


class XueqiuAuthError(RuntimeError):
    pass


def cookie_status() -> dict:
    cookie = (load_settings().get("xueqiu_cookie") or "").strip()
    return {"configured": bool(cookie), "length": len(cookie)}


def _session() -> cffi_requests.Session:
    session = cffi_requests.Session(impersonate="chrome120")
    session.headers.update(HEADERS)
    cookie = (load_settings().get("xueqiu_cookie") or "").strip()
    if cookie:
        session.headers["Cookie"] = cookie
    return session


def _get(session: cffi_requests.Session, url: str, params: dict | None = None) -> dict:
    resp = session.get(url, params=params, headers={"Referer": "https://xueqiu.com/"}, timeout=15)
    if resp.status_code in (401, 403):
        raise XueqiuAuthError("雪球拒绝访问，请在设置里更新 Cookie")
    text = resp.text or ""
    if "400016" in text or "10026" in text or "重新登录" in text:
        raise XueqiuAuthError("雪球登录已失效，请重新粘贴 Cookie")
    resp.raise_for_status()
    try:
        return resp.json()
    except Exception as exc:
        raise XueqiuAuthError("雪球返回了非 JSON 数据，多半需要登录 Cookie") from exc


def _num(value) -> float | None:
    if value in (None, "", "-"):
        return None
    try:
        return float(value)
    except (TypeError, ValueError):
        return None


def _int(value) -> int | None:
    if value in (None, "", "-"):
        return None
    try:
        return int(float(value))
    except (TypeError, ValueError):
        return None


RANK_CATEGORIES = {
    "daily": 12,
    "5d": 12,
    "20d": 13,
    "250d": 14,
    "total": 12,
}


def list_portfolios(page: int = 1, size: int = 20, rank: str = "daily") -> tuple[list[PortfolioCard], int]:
    session = _session()
    category = RANK_CATEGORIES.get(rank, 12)
    data = _get(
        session,
        "https://xueqiu.com/cubes/discover/rank/cube/list.json",
        {"category": category, "market": "cn", "page": page, "count": size},
    )
    rows = data.get("list") or data.get("cubes") or []
    cards = []
    for row in rows:
        symbol = str(row.get("symbol") or row.get("cube_symbol") or "")
        cards.append(
            PortfolioCard(
                source="xueqiu",
                id=symbol,
                name=str(row.get("name") or row.get("cube_name") or symbol),
                owner=str((row.get("owner") or {}).get("screen_name") or row.get("screen_name") or ""),
                daily_return=_num(row.get("daily_gain")),
                return_5d=_num(row.get("weekly_gain")),
                return_20d=_num(row.get("monthly_gain")),
                return_250d=_num(row.get("annualized_gain_rate") or row.get("annual_gain")),
                total_return=_num(row.get("total_gain")),
                followers=_int(row.get("follower_count") or row.get("net_value")),
                rank=row.get("rank"),
            )
        )
    total = int(data.get("totalCount") or data.get("total") or data.get("count") or len(cards))
    return cards, total


def _holdings_from_current(session: cffi_requests.Session, symbol: str) -> list[Holding]:
    data = _get(
        session,
        "https://xueqiu.com/cubes/rebalancing/current.json",
        {"cube_symbol": symbol},
    )
    last_rb = data.get("last_rb") or (data.get("data") or {}).get("last_rb") or {}
    rows = last_rb.get("holdings") or []
    holdings = []
    for row in rows:
        weight = _num(row.get("weight")) or 0
        if weight <= 0:
            continue
        raw = str(row.get("stock_symbol") or row.get("stock_id") or "")
        holdings.append(
            Holding(
                code=normalize_code(raw),
                name=str(row.get("stock_name") or ""),
                weight=round(weight, 2),
                prev_weight=_num(row.get("prev_weight")),
                price=_num(row.get("price")),
                market=guess_market(normalize_code(raw).split(".")[0]),
            )
        )
    return holdings


def _holdings_from_history(session: cffi_requests.Session, symbol: str) -> list[Holding]:
    data = _get(
        session,
        "https://xueqiu.com/cubes/rebalancing/history.json",
        {"cube_symbol": symbol, "count": 1, "page": 1},
    )
    records = data.get("list") or []
    if not records:
        return []
    holdings = []
    for row in records[0].get("rebalancing_histories") or []:
        weight = _num(row.get("target_weight") or row.get("weight")) or 0
        if weight <= 0:
            continue
        raw = str(row.get("stock_symbol") or "")
        holdings.append(
            Holding(
                code=normalize_code(raw),
                name=str(row.get("stock_name") or ""),
                weight=round(weight, 2),
                prev_weight=_num(row.get("prev_weight")),
                price=_num(row.get("price")),
                market=guess_market(normalize_code(raw).split(".")[0]),
            )
        )
    return holdings


def _rebalances(session: cffi_requests.Session, symbol: str) -> list[RebalanceItem]:
    data = _get(
        session,
        "https://xueqiu.com/cubes/rebalancing/history.json",
        {"cube_symbol": symbol, "count": 8, "page": 1},
    )
    items: list[RebalanceItem] = []
    for record in data.get("list") or []:
        created = record.get("created_at")
        time_text = None
        if isinstance(created, (int, float)) and created > 0:
            from datetime import datetime

            time_text = datetime.fromtimestamp(created / 1000).strftime("%Y-%m-%d %H:%M")
        for row in record.get("rebalancing_histories") or []:
            prev = _num(row.get("prev_weight")) or 0
            target = _num(row.get("target_weight") or row.get("weight")) or 0
            if prev == 0 and target > 0:
                side = "买"
            elif prev > 0 and target == 0:
                side = "卖"
            elif target > prev:
                side = "加"
            elif target < prev:
                side = "减"
            else:
                continue
            items.append(
                RebalanceItem(
                    code=normalize_code(str(row.get("stock_symbol") or "")),
                    name=str(row.get("stock_name") or ""),
                    side=side,
                    prev_weight=prev,
                    weight=target,
                    price=_num(row.get("price")),
                    time=time_text,
                )
            )
    return items[:20]


def _quote(session: cffi_requests.Session, symbol: str) -> dict:
    data = _get(session, "https://xueqiu.com/cubes/quote.json", {"code": symbol})
    row = data.get(symbol) or {}
    if isinstance(row, dict):
        return row
    return {}


def get_portfolio(symbol: str) -> PortfolioDetail:
    symbol = symbol.strip().upper()
    if not symbol.startswith("ZH"):
        symbol = f"ZH{symbol}" if symbol.isdigit() else symbol
    session = _session()
    quote = {}
    try:
        quote = _quote(session, symbol)
    except XueqiuAuthError:
        raise
    except Exception:
        quote = {}

    holdings: list[Holding] = []
    warning = None
    cash_weight = None
    try:
        current = _get(session, "https://xueqiu.com/cubes/rebalancing/current.json", {"cube_symbol": symbol})
        last_rb = current.get("last_rb") or (current.get("data") or {}).get("last_rb") or {}
        cash_weight = _num(last_rb.get("cash"))
        holdings = []
        for row in last_rb.get("holdings") or []:
            weight = _num(row.get("weight")) or 0
            if weight <= 0:
                continue
            raw = str(row.get("stock_symbol") or row.get("stock_id") or "")
            holdings.append(
                Holding(
                    code=normalize_code(raw),
                    name=str(row.get("stock_name") or ""),
                    weight=round(weight, 2),
                    prev_weight=_num(row.get("prev_weight")),
                    price=_num(row.get("price")),
                    market=guess_market(normalize_code(raw).split(".")[0]),
                )
            )
        if not holdings:
            holdings = _holdings_from_history(session, symbol)
            warning = "当前持仓为空，已用最近一次调仓记录还原"
    except XueqiuAuthError:
        raise
    except Exception:
        try:
            holdings = _holdings_from_history(session, symbol)
            warning = "当前持仓接口不可用，已用最近一次调仓记录还原"
        except XueqiuAuthError:
            raise
        except Exception as exc:
            raise RuntimeError(f"无法读取雪球持仓: {exc}") from exc

    if not quote and not holdings:
        raise LookupError(f"找不到雪球组合 {symbol}")

    rebalances = []
    try:
        rebalances = _rebalances(session, symbol)
    except Exception:
        rebalances = []

    position_weight = round(sum(h.weight for h in holdings), 2)
    if cash_weight is None:
        cash_weight = round(max(0.0, 100 - position_weight), 2)
    return PortfolioDetail(
        source="xueqiu",
        id=symbol,
        name=str(quote.get("name") or symbol),
        owner="",
        description="",
        nav=_num(quote.get("net_value")),
        cash_weight=round(cash_weight, 2),
        position_weight=position_weight,
        daily_return=_num(quote.get("daily_gain")),
        return_5d=None,
        return_20d=_num(quote.get("monthly_gain")),
        return_250d=_num(quote.get("annualized_gain")),
        total_return=_num(quote.get("total_gain")),
        followers=None,
        start_date=None,
        holdings=holdings,
        rebalances=rebalances,
        source_url=f"https://xueqiu.com/P/{symbol}",
        warning=warning,
    )
