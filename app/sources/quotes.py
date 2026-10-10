from __future__ import annotations

# 兼容旧 import：app.sources.quotes
from app.quotes import fetch_quotes, guess_market, normalize_code, to_secid  # noqa: F401
from app.quotes.codes import to_sina_symbol, to_tencent_symbol  # noqa: F401
