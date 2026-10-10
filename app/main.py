from __future__ import annotations

from pathlib import Path

from fastapi import FastAPI, HTTPException, Query
from fastapi.responses import FileResponse
from fastapi.staticfiles import StaticFiles

from app.broker import get_broker, list_broker_channels, reset_broker
from app.follow import engine
from app.grid import store as grid_store
from app.grid.models import GridStrategy, GridStrategyUpdate
from app.grid.native_engine import native_engine
from app.models import FollowStartRequest, SettingsUpdate
from app.quotes.factory import list_quote_sources
from app.sources import eastmoney, quotes, xueqiu
from app.store import ensure_orders_dirs, public_settings, save_settings

ROOT = Path(__file__).resolve().parents[1]
WEB = ROOT / "web"

app = FastAPI(title="跟单台", version="0.2.0")
app.mount("/static", StaticFiles(directory=WEB), name="static")


@app.on_event("startup")
def _startup():
    ensure_orders_dirs()


@app.get("/")
def index():
    return FileResponse(WEB / "index.html")


@app.get("/api/health")
def health():
    return {"ok": True}


@app.get("/api/settings")
def get_settings():
    data = public_settings()
    data["quote_sources"] = list_quote_sources()
    data["broker_channels"] = list_broker_channels()
    return data


@app.put("/api/settings")
def put_settings(body: SettingsUpdate):
    patch = body.model_dump(exclude_unset=True)
    if patch.get("easyths_api_key") == "":
        patch.pop("easyths_api_key", None)
    save_settings(patch)
    reset_broker()
    return get_settings()


@app.get("/api/broker/status")
def broker_status():
    try:
        st = get_broker().status()
        return st.to_dict()
    except Exception as exc:
        return {
            "channel": "unknown",
            "connected": False,
            "label": "交易",
            "detail": str(exc),
        }


@app.post("/api/broker/connect")
def broker_connect():
    try:
        st = get_broker(force_new=True).connect()
        return st.to_dict()
    except Exception as exc:
        raise HTTPException(status_code=502, detail=str(exc)) from exc


@app.get("/api/portfolios")
def list_portfolios(
    source: str = Query(..., pattern="^(eastmoney|xueqiu)$"),
    rank: str = "daily",
    page: int = 1,
    size: int = 30,
    q: str | None = None,
):
    try:
        if source == "eastmoney":
            items, total = eastmoney.list_portfolios(rank=rank, page=page, size=size)
        else:
            items, total = xueqiu.list_portfolios(page=page, size=size, rank=rank)
    except xueqiu.XueqiuAuthError as exc:
        raise HTTPException(status_code=401, detail=str(exc)) from exc
    except Exception as exc:
        raise HTTPException(status_code=502, detail=str(exc)) from exc

    if q:
        key = q.strip().lower()
        items = [
            item
            for item in items
            if key in item.id.lower()
            or key in item.name.lower()
            or key in (item.owner or "").lower()
        ]
    return {"source": source, "total": total, "page": page, "items": items}


@app.get("/api/portfolios/{source}/{portfolio_id}")
def portfolio_detail(source: str, portfolio_id: str):
    try:
        if source == "eastmoney":
            detail = eastmoney.get_portfolio(portfolio_id)
        elif source == "xueqiu":
            detail = xueqiu.get_portfolio(portfolio_id)
        else:
            raise HTTPException(status_code=400, detail="未知数据源")
    except xueqiu.XueqiuAuthError as exc:
        raise HTTPException(status_code=401, detail=str(exc)) from exc
    except LookupError as exc:
        raise HTTPException(status_code=404, detail=str(exc)) from exc
    except Exception as exc:
        raise HTTPException(status_code=502, detail=str(exc)) from exc
    return detail


@app.get("/api/quotes")
def get_quotes(codes: str):
    items = [c.strip() for c in codes.split(",") if c.strip()]
    try:
        return {"items": list(quotes.fetch_quotes(items).values())}
    except Exception as exc:
        raise HTTPException(status_code=502, detail=str(exc)) from exc


@app.get("/api/follow")
def follow_status():
    state = engine.snapshot()
    return {"active": bool(state), "account": state}


@app.post("/api/follow")
def follow_start(body: FollowStartRequest):
    try:
        state = engine.start(body)
    except (ValueError, LookupError) as exc:
        raise HTTPException(status_code=400, detail=str(exc)) from exc
    except xueqiu.XueqiuAuthError as exc:
        raise HTTPException(status_code=401, detail=str(exc)) from exc
    except Exception as exc:
        raise HTTPException(status_code=502, detail=str(exc)) from exc
    return {"active": True, "account": state}


@app.post("/api/follow/sync")
def follow_sync():
    try:
        state = engine.sync()
    except LookupError as exc:
        raise HTTPException(status_code=404, detail=str(exc)) from exc
    except ValueError as exc:
        raise HTTPException(status_code=400, detail=str(exc)) from exc
    except xueqiu.XueqiuAuthError as exc:
        raise HTTPException(status_code=401, detail=str(exc)) from exc
    except Exception as exc:
        raise HTTPException(status_code=502, detail=str(exc)) from exc
    return {"active": True, "account": state}


@app.post("/api/follow/pause")
def follow_pause():
    try:
        state = engine.pause()
    except LookupError as exc:
        raise HTTPException(status_code=404, detail=str(exc)) from exc
    return {"active": True, "account": state}


@app.post("/api/follow/resume")
def follow_resume():
    try:
        state = engine.resume()
    except LookupError as exc:
        raise HTTPException(status_code=404, detail=str(exc)) from exc
    return {"active": True, "account": state}


@app.delete("/api/follow")
def follow_stop():
    engine.stop()
    return {"active": False}


@app.get("/api/grid")
def grid_list():
    items = grid_store.list_strategies()
    return {"items": items, "summary": grid_store.summary()}


@app.get("/api/grid/engine")
def grid_engine_status():
    return native_engine.status()


@app.post("/api/grid/engine/start")
def grid_engine_start(dry_run: bool = Query(False), poll_ms: int = Query(3000)):
    try:
        return native_engine.start(dry_run=dry_run, poll_ms=poll_ms)
    except FileNotFoundError as ex:
        raise HTTPException(status_code=404, detail=str(ex)) from ex
    except RuntimeError as ex:
        raise HTTPException(status_code=500, detail=str(ex)) from ex


@app.post("/api/grid/engine/stop")
def grid_engine_stop():
    return native_engine.stop()


@app.post("/api/grid")
def grid_create(body: GridStrategy):
    if not body.code.strip():
        raise HTTPException(status_code=400, detail="请填写股票代码")
    item = grid_store.add_strategy(body)
    return item


@app.put("/api/grid/{sid}")
def grid_update(sid: str, body: GridStrategyUpdate):
    item = grid_store.update_strategy(sid, body.model_dump(exclude_unset=True))
    if not item:
        raise HTTPException(status_code=404, detail="找不到区间策略")
    return item


@app.delete("/api/grid/{sid}")
def grid_delete(sid: str):
    if not grid_store.delete_strategy(sid):
        raise HTTPException(status_code=404, detail="找不到区间策略")
    return {"ok": True}


@app.post("/api/grid/{sid}/toggle")
def grid_toggle(sid: str):
    item = grid_store.get_strategy(sid)
    if not item:
        raise HTTPException(status_code=404, detail="找不到区间策略")
    nxt = "paused" if item.status == "running" else "running"
    updated = grid_store.update_strategy(sid, {"status": nxt})
    return updated
