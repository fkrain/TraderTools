from __future__ import annotations

from typing import Literal
from uuid import uuid4

from pydantic import BaseModel, Field


def new_grid_id() -> str:
    return uuid4().hex[:10]


class GridStrategy(BaseModel):
    id: str = Field(default_factory=new_grid_id)
    code: str
    name: str = ""
    status: Literal["running", "paused", "stopped"] = "paused"
    # 区间上下限，0 表示不限制
    price_ceil: float = 0
    price_floor: float = 0
    # pause | clear_pause | clear_stop
    ceil_action: Literal["pause", "clear_pause", "clear_stop"] = "pause"
    floor_action: Literal["pause", "clear_pause", "clear_stop"] = "clear_stop"
    # 每次买卖
    buy_qty: float = 100
    buy_unit: Literal["shares", "amount"] = "shares"
    sell_qty: float = 0  # 0 = 按上次成交量
    sell_unit: Literal["shares", "amount"] = "shares"
    # 基准
    last_price: float = 0
    last_qty: int = 0
    use_order_price: bool = True  # True=下单价，False=条件价
    use_cost_basis: bool = False  # 用券商成本价作基准
    auto_xrxd: bool = True
    # 涨跌触发（%）或绝对价
    by_price: bool = False
    sell_up_pct: float = 2.0
    buy_down_pct: float = 2.0
    # 拐点
    low_pivot: float = 0
    rebound_pct: float = 0
    high_pivot: float = 0
    pullback_pct: float = 0
    delay_confirm_sec: int = 0
    check_buy_with_down: bool = False
    check_sell_with_up: bool = False
    # 连买 / 倍数
    max_buy_streak: int = 0
    buy_streak: int = 0
    buy_qty_multiple: float = 0  # 0=用每次买入设置
    cross_buy_multiply: bool = False
    cross_sell_multiply: bool = False
    sell_all_after_multi_buy: bool = False
    # 挂单 / 匹配 / 检查
    preorder_sell: bool = False
    preorder_buy: bool = False
    grid_match: bool = False
    check_fill: bool = False
    margin_buy: bool = False
    # 助手 16.1
    session_enabled: bool = True
    session_start: str = "09:25:00"
    session_end: str = "15:00:00"
    buy_price_type: str = "ask1"  # last|market|bid1..bid5|ask1..ask5
    sell_price_type: str = "bid1"
    price_offset_cents: int = 0
    max_position_amount: float = 0
    reserve_base_shares: int = 0
    position_shares: int = 0
    match_buy_prices: list[float] = Field(default_factory=list)
    # 统计（示意，引擎待接入后写入）
    range_pnl: float = 0
    range_save: float = 0
    note: str = ""


class GridStrategyUpdate(BaseModel):
    code: str | None = None
    name: str | None = None
    status: Literal["running", "paused", "stopped"] | None = None
    price_ceil: float | None = None
    price_floor: float | None = None
    ceil_action: Literal["pause", "clear_pause", "clear_stop"] | None = None
    floor_action: Literal["pause", "clear_pause", "clear_stop"] | None = None
    buy_qty: float | None = None
    buy_unit: Literal["shares", "amount"] | None = None
    sell_qty: float | None = None
    sell_unit: Literal["shares", "amount"] | None = None
    last_price: float | None = None
    last_qty: int | None = None
    use_order_price: bool | None = None
    use_cost_basis: bool | None = None
    auto_xrxd: bool | None = None
    by_price: bool | None = None
    sell_up_pct: float | None = None
    buy_down_pct: float | None = None
    low_pivot: float | None = None
    rebound_pct: float | None = None
    high_pivot: float | None = None
    pullback_pct: float | None = None
    delay_confirm_sec: int | None = None
    check_buy_with_down: bool | None = None
    check_sell_with_up: bool | None = None
    max_buy_streak: int | None = None
    buy_streak: int | None = None
    buy_qty_multiple: float | None = None
    cross_buy_multiply: bool | None = None
    cross_sell_multiply: bool | None = None
    sell_all_after_multi_buy: bool | None = None
    preorder_sell: bool | None = None
    preorder_buy: bool | None = None
    grid_match: bool | None = None
    check_fill: bool | None = None
    margin_buy: bool | None = None
    session_enabled: bool | None = None
    session_start: str | None = None
    session_end: str | None = None
    buy_price_type: str | None = None
    sell_price_type: str | None = None
    price_offset_cents: int | None = None
    max_position_amount: float | None = None
    reserve_base_shares: int | None = None
    position_shares: int | None = None
    match_buy_prices: list[float] | None = None
    range_pnl: float | None = None
    range_save: float | None = None
    note: str | None = None
