from __future__ import annotations

import httpx

from app.models import Quote
from app.quotes.codes import normalize_code, to_tencent_symbol

HEADERS = {
    "User-Agent": (
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64) "
        "AppleWebKit/537.36 (KHTML, like Gecko) Chrome/122.0.0.0 Safari/537.36"
    ),
    "Referer": "https://finance.qq.com/",
}


class TencentQuoteProvider:
    name = "tencent"

    def fetch(self, codes: list[str]) -> dict[str, Quote]:
        symbols = []
        mapping: dict[str, str] = {}
        for raw in codes:
            sym = to_tencent_symbol(raw)
            if not sym:
                continue
            symbols.append(sym)
            mapping[sym] = normalize_code(raw)
        if not symbols:
            return {}
        url = "https://qt.gtimg.cn/q=" + ",".join(dict.fromkeys(symbols))
        with httpx.Client(timeout=12.0, headers=HEADERS, trust_env=False) as client:
            resp = client.get(url)
            resp.raise_for_status()
            text = resp.text
        quotes: dict[str, Quote] = {}
        for chunk in text.split(";"):
            chunk = chunk.strip()
            if not chunk or "=" not in chunk:
                continue
            left, right = chunk.split("=", 1)
            sym = left.split("_")[-1].lower()
            payload = right.strip().strip('"')
            if not payload or "~" not in payload:
                continue
            parts = payload.split("~")
            # v_sz000001="1~平安银行~000001~11.52~..."
            if len(parts) < 6:
                continue
            name = parts[1]
            price_s = parts[3]
            prev_s = parts[4]
            try:
                price = float(price_s) if price_s not in ("", "0") else None
            except ValueError:
                price = None
            change_pct = None
            if price is not None:
                try:
                    prev = float(prev_s)
                    if prev:
                        change_pct = round((price - prev) / prev * 100, 2)
                except ValueError:
                    pass
            code = mapping.get(sym) or normalize_code(parts[2] if len(parts) > 2 else sym)
            quotes[code] = Quote(
                code=code,
                name=name,
                price=price,
                change_pct=change_pct,
                bid=_f(parts, 9),
                ask=_f(parts, 19),
                source=self.name,
            )
        return quotes


def _f(parts: list[str], idx: int) -> float | None:
    if idx >= len(parts):
        return None
    try:
        v = float(parts[idx])
        return v if v else None
    except ValueError:
        return None
