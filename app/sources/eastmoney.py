from __future__ import annotations

import httpx

from app.models import Holding, PortfolioCard, PortfolioDetail, RebalanceItem
from app.sources.quotes import guess_market, normalize_code

BASE = "https://simqry2.eastmoney.com/qry_tzzh_v2"
HEADERS = {
    "User-Agent": (
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64) "
        "AppleWebKit/537.36 (KHTML, like Gecko) Chrome/122.0.0.0 Safari/537.36"
    ),
    "Referer": "https://group.eastmoney.com/mcombin.html",
    "Accept": "application/json, text/plain, */*",
}

RANK_TYPES = {
    "daily": "10001",
    "5d": "10005",
    "20d": "10020",
    "250d": "10250",
    "total": "10000",
}


def _get(params: dict) -> dict:
    query = {"plat": "2", "ver": "web20", "utToken": "", "ctToken": "", **params}
    with httpx.Client(timeout=15.0, headers=HEADERS, trust_env=False) as client:
        resp = client.get(BASE, params=query)
        resp.raise_for_status()
        payload = resp.json()
    if payload.get("result") not in (0, "0", None) and payload.get("message") not in ("OK", None, ""):
        raise RuntimeError(payload.get("message") or "东财接口返回异常")
    return payload


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


def list_portfolios(rank: str = "daily", page: int = 1, size: int = 30) -> tuple[list[PortfolioCard], int]:
    syl = RANK_TYPES.get(rank, rank if rank.isdigit() else "10001")
    rec_idx = max(1, (page - 1) * size + 1)
    payload = _get(
        {
            "type": "spo_rank_syl",
            "sylType": syl,
            "recIdx": rec_idx,
            "recCnt": size,
        }
    )
    cards = []
    for row in payload.get("data") or []:
        cards.append(
            PortfolioCard(
                source="eastmoney",
                id=str(row.get("zjzh") or ""),
                name=str(row.get("zuheName") or ""),
                owner=str(row.get("username") or row.get("userid") or ""),
                daily_return=_num(row.get("syl_dr")),
                return_5d=_num(row.get("syl_5r")),
                return_20d=_num(row.get("syl_20r")),
                return_250d=_num(row.get("syl_250r")),
                total_return=_num(row.get("zsyl")),
                followers=_int(row.get("concerned")),
                last_action=str(row.get("mmbz") or "") or None,
                last_stock=str(row.get("stkName") or "") or None,
                rank=_int(row.get("rank")),
                extra={
                    "userid": str(row.get("userid") or ""),
                    "last_time": row.get("cjsj"),
                },
            )
        )
    return cards, int(payload.get("totalCnt") or len(cards))


def _holdings(zjzh: str, nav: float | None) -> tuple[list[Holding], float | None]:
    payload = _get(
        {
            "type": "spo_hold_detail",
            "zjzh": zjzh,
            "recIdx": 1,
            "recCnt": 80,
        }
    )
    rows = payload.get("data") or []
    hold_sum = _num(payload.get("holdPosSum"))
    holdings: list[Holding] = []
    market_values = []
    for row in rows:
        code = normalize_code(str(row.get("stkCode") or ""))
        if not code:
            continue
        price = _num(row.get("price"))
        shares = _int(row.get("holdPos"))
        mv = (price or 0) * (shares or 0)
        market_values.append(mv)
        holdings.append(
            Holding(
                code=code,
                name=str(row.get("stkName") or ""),
                weight=0,
                price=price,
                shares=shares,
                cost=_num(row.get("cbj")),
                pnl_pct=_num(row.get("ykRate") or row.get("prcPcnt")),
                hold_days=_int(row.get("hldDays")),
                market=guess_market(code.split(".")[0]),
            )
        )
    total_mv = sum(market_values)
    denom = nav if nav and nav > 0 else total_mv
    if denom:
        for item, mv in zip(holdings, market_values):
            item.weight = round(mv / denom * 100, 2)
    elif hold_sum and holdings:
        leftover = hold_sum
        for item in holdings[:-1]:
            leftover -= item.weight
        holdings[-1].weight = round(leftover, 2)
    return holdings, hold_sum


def _rebalances(zjzh: str) -> list[RebalanceItem]:
    payload = _get(
        {
            "type": "spo_hldchg_home",
            "zjzh": zjzh,
            "recIdx": 1,
            "recCnt": 20,
        }
    )
    items = []
    for row in payload.get("data") or []:
        items.append(
            RebalanceItem(
                code=normalize_code(str(row.get("stkCode") or "")),
                name=str(row.get("stkName") or ""),
                side=str(row.get("mmbz") or ""),
                prev_weight=_num(row.get("holdPosBef")),
                weight=_num(row.get("holdPosAft")),
                price=_num(row.get("cjjg") or row.get("cjjg2")),
                time=str(row.get("cjsj") or "") or None,
            )
        )
    return items


def get_portfolio(zjzh: str) -> PortfolioDetail:
    zjzh = zjzh.strip()
    basic = _get({"type": "spo_zuhe_detail_basic", "zjzh": zjzh}).get("data") or {}
    if not basic:
        raise LookupError(f"找不到东财组合 {zjzh}")
    survey = {}
    try:
        survey = _get({"type": "spo_syl_survey", "zjzh": zjzh}).get("data") or {}
    except Exception:
        survey = {}
    nav = _num(basic.get("zzc"))
    holdings, hold_sum = _holdings(zjzh, nav)
    position_weight = hold_sum if hold_sum is not None else round(sum(h.weight for h in holdings), 2)
    cash_weight = round(max(0.0, 100 - (position_weight or 0)), 2)
    name = str(basic.get("zuheName") or basic.get("zhmcReal") or zjzh)
    return PortfolioDetail(
        source="eastmoney",
        id=zjzh,
        name=name,
        owner=str(basic.get("username") or ""),
        description=str(basic.get("comment") or basic.get("cmtReal") or ""),
        nav=nav,
        cash_weight=cash_weight,
        position_weight=position_weight,
        daily_return=_num(basic.get("syl_dr") or survey.get("syl_dr")),
        return_5d=_num(basic.get("syl_5r") or survey.get("syl_5r")),
        return_20d=_num(basic.get("syl_20r") or survey.get("syl_20r")),
        return_250d=_num(basic.get("syl_250r") or survey.get("syl_250r")),
        total_return=_num(basic.get("zsyl") or survey.get("zsyl")),
        followers=_int(basic.get("concerned") or basic.get("visitors")),
        win_rate=_num(basic.get("dealRate")),
        start_date=str(basic.get("startDate") or "") or None,
        holdings=holdings,
        rebalances=_rebalances(zjzh),
        source_url=f"https://group.eastmoney.com/other,{zjzh}.html",
    )
