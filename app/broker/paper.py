from __future__ import annotations

from app.broker.base import BrokerStatus, OrderIntent, OrderResult, now_str


class PaperBroker:
    """本地模拟成交：立即按意图价全部成交。"""

    channel = "paper"

    def __init__(self) -> None:
        self._connected = True
        self._orders: list[dict] = []

    def connect(self) -> BrokerStatus:
        self._connected = True
        return self.status()

    def disconnect(self) -> None:
        self._connected = False

    def status(self) -> BrokerStatus:
        return BrokerStatus(
            channel=self.channel,
            connected=self._connected,
            label="仅模拟账户",
            detail="内存成交，不发真实委托",
        )

    def place(self, intent: OrderIntent) -> OrderResult:
        px = intent.price or 0
        self._orders.append(
            {
                "time": now_str(),
                "client_order_id": intent.client_order_id,
                "code": intent.code,
                "side": intent.side,
                "qty": intent.qty,
                "price": px,
                "status": "filled",
            }
        )
        return OrderResult(
            client_order_id=intent.client_order_id,
            status="filled",
            order_id=intent.client_order_id,
            message="模拟成交",
            filled_qty=intent.qty,
            filled_price=px,
        )

    def cancel(self, order_id: str) -> OrderResult:
        return OrderResult(client_order_id=order_id, status="cancelled", message="模拟撤单")

    def query_positions(self) -> list[dict]:
        return []

    def query_orders(self) -> list[dict]:
        return list(self._orders)

    def query_account(self) -> dict:
        return {"cash": None, "channel": self.channel}
