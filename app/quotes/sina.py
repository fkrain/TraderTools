from __future__ import annotations

import re

import httpx

from app.models import Quote
from app.quotes.codes import normalize_code, to_sina_symbol

HEADERS = {
    "User-Agent": (
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64) "
        "AppleWebKit/537.36 (KHTML, like Gecko) Chrome/122.0.0.0 Safari/537.36"
    ),
    "Referer": "https://finance.sina.com.cn/",
}


class SinaQuoteProvider:
    name = "sina"

    def fetch(self, codes: list[str]) -> dict[str, Quote]:
        symbols = []
        mapping: dict[str, str] = {}
        for raw in codes:
            sym = to_sina_symbol(raw)
            if not sym:
                continue
            symbols.append(sym)
            mapping[sym] = normalize_code(raw)
        if not symbols:
            return {}
        url = "https://hq.sinajs.cn/list=" + ",".join(dict.fromkeys(symbols))
        with httpx.Client(timeout=12.0, headers=HEADERS, trust_env=False) as client:
            resp = client.get(url)
            resp.raise_for_status()
            text = resp.content.decode("gbk", errors="ignore")
        quotes: dict[str, Quote] = {}
        for line in text.splitlines():
            m = re.match(r'var hq_str_(\w+)="(.*)";?', line.strip())
            if not m:
                continue
            sym, payload = m.group(1), m.group(2)
            if not payload:
                continue
            parts = payload.split(",")
            if len(parts) < 4:
                continue
            name = parts[0]
            try:
                price = float(parts[3]) if parts[3] not in ("", "0.000") else None
            except ValueError:
                price = None
            change_pct = None
            if price is not None and len(parts) > 2:
                try:
                    prev = float(parts[2])
                    if prev:
                        change_pct = round((price - prev) / prev * 100, 2)
                except ValueError:
                    pass
            code = mapping.get(sym) or normalize_code(sym[2:] if len(sym) > 2 else sym)
            quotes[code] = Quote(
                code=code,
                name=name,
                price=price,
                change_pct=change_pct,
                bid=_f(parts, 6) if len(parts) > 6 else None,
                ask=_f(parts, 7) if len(parts) > 7 else None,
                source=self.name,
            )
        return quotes


def _f(parts: list[str], idx: int) -> float | None:
    try:
        v = float(parts[idx])
        return v if v else None
    except (ValueError, IndexError):
        return None
