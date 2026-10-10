from __future__ import annotations

from typing import Protocol

from app.models import Quote


class IQuoteProvider(Protocol):
    name: str

    def fetch(self, codes: list[str]) -> dict[str, Quote]:
        ...
