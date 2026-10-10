from __future__ import annotations

import httpx

from app.models import Quote
from app.quotes.codes import normalize_code, to_secid

QUOTE_URL = "https://push2.eastmoney.com/api/qt/ulist.np/get"
HEADERS = {
    "User-Agent": (
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64) "
        "AppleWebKit/537.36 (KHTML, like Gecko) Chrome/122.0.0.0 Safari/537.36"
    ),
    "Referer": "https://quote.eastmoney.com/",
}


class EastmoneyQuoteProvider:
    name = "eastmoney"

    def fetch(self, codes: list[str]) -> dict[str, Quote]:
        secids = []
        mapping: dict[str, str] = {}
        for raw in codes:
            secid = to_secid(raw)
            if not secid:
                continue
            secids.append(secid)
            mapping[secid.split(".", 1)[1]] = normalize_code(raw)
        if not secids:
            return {}
        params = {
            "fltt": "2",
            "secids": ",".join(dict.fromkeys(secids)),
            "fields": "f2,f3,f12,f14",
            "ut": "b2884a393a59ad64002292a3e90d46a5",
        }
        with httpx.Client(timeout=12.0, headers=HEADERS, trust_env=False) as client:
            resp = client.get(QUOTE_URL, params=params)
            resp.raise_for_status()
            payload = resp.json()
        rows = ((payload.get("data") or {}).get("diff")) or []
        quotes: dict[str, Quote] = {}
        for row in rows:
            num = str(row.get("f12") or "")
            code = mapping.get(num) or normalize_code(num)
            price = row.get("f2")
            change = row.get("f3")
            quotes[code] = Quote(
                code=code,
                name=str(row.get("f14") or ""),
                price=None if price in (None, "-", "") else float(price),
                change_pct=None if change in (None, "-", "") else float(change),
                source=self.name,
            )
        return quotes
