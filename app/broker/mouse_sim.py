from __future__ import annotations

import json
import shutil
import socket
from pathlib import Path

from app.broker.base import BrokerStatus, OrderIntent, OrderResult, now_str
from app.store import ensure_orders_dirs, load_settings


class MouseSimBroker:
    """委托写入 outbox，由 EasyTHS 桥在同花顺窗口里模拟鼠键成交。"""

    channel = "mouse_sim"

    def _root(self) -> Path:
        settings = load_settings()
        return ensure_orders_dirs(settings.get("orders_dir"))

    def _endpoint(self) -> tuple[str, int]:
        settings = load_settings()
        host = (settings.get("easyths_host") or "127.0.0.1").strip() or "127.0.0.1"
        port = int(settings.get("easyths_port") or 7648)
        return host, port

    def _probe(self) -> bool:
        host, port = self._endpoint()
        try:
            with socket.create_connection((host, port), timeout=0.4):
                return True
        except OSError:
            return False

    def connect(self) -> BrokerStatus:
        self._root()
        return self.status()

    def disconnect(self) -> None:
        return None

    def status(self) -> BrokerStatus:
        root = self._root()
        pending = len(list((root / "outbox").glob("*.json")))
        host, port = self._endpoint()
        up = self._probe()
        return BrokerStatus(
            channel=self.channel,
            connected=up,
            label="模拟鼠键",
            detail=f"{host}:{port} {'up' if up else 'down'} · outbox {pending} · {root}",
        )

    def place(self, intent: OrderIntent) -> OrderResult:
        root = self._root()
        ts = now_str().replace("-", "").replace(":", "").replace(" ", "")
        side = intent.side.upper()
        code = intent.code.replace(".", "")
        name = f"{ts}_{intent.client_order_id}_{side}_{code}.json"
        path = root / "outbox" / name
        payload = {
            "v": 1,
            **intent.to_dict(),
            "created_at": now_str(),
            "channel": self.channel,
        }
        tmp = path.with_suffix(".tmp")
        tmp.write_text(json.dumps(payload, ensure_ascii=False, indent=2), encoding="utf-8")
        tmp.replace(path)
        return OrderResult(
            client_order_id=intent.client_order_id,
            status="submitted",
            order_id=name,
            message=f"已写入模拟鼠键队列 {path.name}",
        )

    def cancel(self, order_id: str) -> OrderResult:
        root = self._root()
        for folder in ("outbox", "sent"):
            path = root / folder / order_id
            if path.exists():
                dest = root / "reject" / path.name
                shutil.move(str(path), str(dest))
                return OrderResult(
                    client_order_id=order_id,
                    status="cancelled",
                    order_id=order_id,
                    message="已移入 reject",
                )
        return OrderResult(client_order_id=order_id, status="rejected", message="找不到委托文件")

    def query_positions(self) -> list[dict]:
        return []

    def query_orders(self) -> list[dict]:
        root = self._root()
        orders = []
        for folder in ("outbox", "sent", "ack", "done", "reject"):
            for path in (root / folder).glob("*.json"):
                try:
                    data = json.loads(path.read_text(encoding="utf-8"))
                except Exception:
                    data = {"file": path.name}
                data["folder"] = folder
                orders.append(data)
        return orders

    def query_account(self) -> dict:
        host, port = self._endpoint()
        return {"channel": self.channel, "orders_dir": str(self._root()), "easyths": f"{host}:{port}"}
