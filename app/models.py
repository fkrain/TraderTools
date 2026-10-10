from __future__ import annotations

from typing import Literal

from pydantic import BaseModel, Field


Source = Literal["eastmoney", "xueqiu"]


class Holding(BaseModel):
    code: str
    name: str
    weight: float
    price: float | None = None
    prev_weight: float | None = None
    shares: int | None = None
    cost: float | None = None
    pnl_pct: float | None = None
    hold_days: int | None = None
    market: str | None = None


class RebalanceItem(BaseModel):
    code: str
    name: str
    side: str
    prev_weight: float | None = None
    weight: float | None = None
    price: float | None = None
    time: str | None = None


class PortfolioCard(BaseModel):
    source: Source
    id: str
    name: str
    owner: str = ""
    daily_return: float | None = None
    return_5d: float | None = None
    return_20d: float | None = None
    return_250d: float | None = None
    total_return: float | None = None
    followers: int | None = None
    last_action: str | None = None
    last_stock: str | None = None
    rank: int | None = None
    extra: dict = Field(default_factory=dict)


class PortfolioDetail(BaseModel):
    source: Source
    id: str
    name: str
    owner: str = ""
    description: str = ""
    nav: float | None = None
    cash_weight: float | None = None
    position_weight: float | None = None
    daily_return: float | None = None
    return_5d: float | None = None
    return_20d: float | None = None
    return_250d: float | None = None
    total_return: float | None = None
    followers: int | None = None
    win_rate: float | None = None
    start_date: str | None = None
    holdings: list[Holding] = Field(default_factory=list)
    rebalances: list[RebalanceItem] = Field(default_factory=list)
    source_url: str = ""
    warning: str | None = None


class Quote(BaseModel):
    code: str
    name: str = ""
    price: float | None = None
    change_pct: float | None = None
    bid: float | None = None
    ask: float | None = None
    source: str | None = None


class FollowStartRequest(BaseModel):
    source: Source
    portfolio_id: str
    capital: float = 1_000_000
    follow_ratio: float = 1.0


class SettingsUpdate(BaseModel):
    xueqiu_cookie: str | None = None
    quote_source: str | None = None
    broker_channel: str | None = None
    orders_dir: str | None = None
    mouse_window_title: str | None = None
    easyths_host: str | None = None
    easyths_port: int | None = None
    easyths_api_key: str | None = None
    easyths_account: str | None = None
