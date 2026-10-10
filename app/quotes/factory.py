from __future__ import annotations

from app.quotes.eastmoney import EastmoneyQuoteProvider
from app.quotes.sina import SinaQuoteProvider
from app.quotes.tencent import TencentQuoteProvider
from app.store import load_settings

PROVIDERS = {
    "tencent": TencentQuoteProvider,
    "sina": SinaQuoteProvider,
    "eastmoney": EastmoneyQuoteProvider,
}


def normalize_quote_source(source: str | None) -> str:
    key = (source or "tencent").strip().lower()
    if key in PROVIDERS:
        return key
    return "tencent"


def get_provider(source: str | None = None):
    settings = load_settings()
    key = normalize_quote_source(source or settings.get("quote_source"))
    return PROVIDERS[key]()


def list_quote_sources() -> list[dict]:
    return [
        {"id": "tencent", "label": "腾讯"},
        {"id": "sina", "label": "新浪"},
        {"id": "eastmoney", "label": "东财"},
    ]
