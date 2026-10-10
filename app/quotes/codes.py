from __future__ import annotations


def normalize_code(raw: str) -> str:
    code = (raw or "").strip().upper().replace(" ", "")
    if not code:
        return ""
    if "." in code:
        num, mkt = code.split(".", 1)
        return f"{num}.{mkt}"
    if code.startswith(("SH", "SZ", "BJ", "HK")) and len(code) >= 7:
        return f"{code[2:]}.{code[:2]}"
    if code.isdigit() and len(code) == 6:
        return f"{code}.{guess_market(code)}"
    return code


def guess_market(code: str) -> str:
    num = code.split(".")[0] if "." in code else code
    if num.startswith(("6", "5", "11", "9")):
        return "SH"
    if num.startswith(("8", "4")):
        return "BJ"
    return "SZ"


def to_secid(code: str) -> str | None:
    norm = normalize_code(code)
    if "." not in norm:
        return None
    num, mkt = norm.split(".", 1)
    if mkt == "SH":
        prefix = "1"
    elif mkt == "BJ":
        prefix = "0"
    else:
        prefix = "0"
    return f"{prefix}.{num}"


def to_tencent_symbol(code: str) -> str | None:
    norm = normalize_code(code)
    if "." not in norm:
        return None
    num, mkt = norm.split(".", 1)
    if mkt == "SH":
        return f"sh{num}"
    if mkt == "SZ":
        return f"sz{num}"
    if mkt == "BJ":
        return f"bj{num}"
    return None


def to_sina_symbol(code: str) -> str | None:
    return to_tencent_symbol(code)


def market_byte(code: str) -> int:
    """通达信市场：0 深圳，1 上海。"""
    mkt = guess_market(normalize_code(code).split(".")[0])
    return 1 if mkt == "SH" else 0
