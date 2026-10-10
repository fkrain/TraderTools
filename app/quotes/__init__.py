from __future__ import annotations

from app.models import Quote
from app.quotes.codes import guess_market, normalize_code, to_secid, to_tencent_symbol, to_sina_symbol

__all__ = [
    "Quote",
    "guess_market",
    "normalize_code",
    "to_secid",
    "to_tencent_symbol",
    "to_sina_symbol",
    "fetch_quotes",
    "get_provider",
]


def fetch_quotes(codes: list[str]) -> dict[str, Quote]:
    from app.quotes.factory import get_provider

    return get_provider().fetch(codes)


def get_provider():
    from app.quotes.factory import get_provider as _get

    return _get()
