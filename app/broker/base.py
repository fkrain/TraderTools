from __future__ import annotations

from dataclasses import asdict, dataclass, field
from datetime import datetime
from typing import Literal, Protocol
from uuid import uuid4

Side = Literal["buy", "sell"]
PriceType = Literal["limit", "opponent", "market"]
OrderStatus = Literal["pending", "submitted", "filled", "rejected", "cancelled", "unimplemented"]


def new_client_order_id() -> str:
    return uuid4().hex[:12]


def now_str() -> str:
    return datetime.now().strftime("%Y-%m-%d %H:%M:%S")


@dataclass
class OrderIntent:
    code: str
    side: Side
    qty: int
    price_type: PriceType = "limit"
    price: float | None = None
    account_id: str = "default"
    client_order_id: str = field(default_factory=new_client_order_id)
    name: str = ""
    reason: str = ""

    def to_dict(self) -> dict:
        return asdict(self)


@dataclass
class OrderResult:
    client_order_id: str
    status: OrderStatus
    order_id: str = ""
    message: str = ""
    filled_qty: int = 0
    filled_price: float | None = None

    def to_dict(self) -> dict:
        return asdict(self)


@dataclass
class BrokerStatus:
    channel: str
    connected: bool
    label: str
    detail: str = ""

    def to_dict(self) -> dict:
        return asdict(self)


class IBroker(Protocol):
    channel: str

    def connect(self) -> BrokerStatus:
        ...

    def disconnect(self) -> None:
        ...

    def status(self) -> BrokerStatus:
        ...

    def place(self, intent: OrderIntent) -> OrderResult:
        ...

    def cancel(self, order_id: str) -> OrderResult:
        ...

    def query_positions(self) -> list[dict]:
        ...

    def query_orders(self) -> list[dict]:
        ...

    def query_account(self) -> dict:
        ...
