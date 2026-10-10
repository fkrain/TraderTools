from __future__ import annotations

import os
import subprocess
import threading
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
DATA_DIR = ROOT / "data"
NATIVE_EXE_CANDIDATES = [
    ROOT / "native" / "build" / "Release" / "grid_engine.exe",
    ROOT / "native" / "build" / "Debug" / "grid_engine.exe",
    ROOT / "bin" / "grid_engine.exe",
]


class NativeGridEngine:
    """Manage the C++ grid_engine.exe subprocess."""

    def __init__(self) -> None:
        self._proc: subprocess.Popen | None = None
        self._lock = threading.Lock()
        self._log_tail: list[str] = []
        self._started_at: float | None = None
        self._dry_run = False
        self._poll_ms = 3000
        self._reader: threading.Thread | None = None

    def resolve_exe(self) -> Path | None:
        for p in NATIVE_EXE_CANDIDATES:
            if p.is_file():
                return p
        env = os.environ.get("GRID_ENGINE_EXE")
        if env:
            ep = Path(env)
            if ep.is_file():
                return ep
        return None

    def status(self) -> dict:
        with self._lock:
            running = self._proc is not None and self._proc.poll() is None
            pid = self._proc.pid if running and self._proc else None
            code = None if running else (self._proc.returncode if self._proc else None)
            return {
                "running": running,
                "pid": pid,
                "exit_code": code,
                "exe": str(self.resolve_exe() or ""),
                "exe_found": self.resolve_exe() is not None,
                "dry_run": self._dry_run,
                "poll_ms": self._poll_ms,
                "started_at": self._started_at,
                "log_tail": list(self._log_tail[-40:]),
            }

    def _append_log(self, line: str) -> None:
        line = line.rstrip("\r\n")
        if not line:
            return
        with self._lock:
            self._log_tail.append(line)
            if len(self._log_tail) > 200:
                self._log_tail = self._log_tail[-200:]

    def _read_stdout(self, proc: subprocess.Popen) -> None:
        assert proc.stdout is not None
        try:
            for raw in proc.stdout:
                self._append_log(raw)
        except Exception as ex:  # noqa: BLE001
            self._append_log(f"[reader] {ex}")

    def start(self, *, dry_run: bool = False, poll_ms: int = 3000) -> dict:
        with self._lock:
            if self._proc is not None and self._proc.poll() is None:
                return self.status()

        exe = self.resolve_exe()
        if not exe:
            raise FileNotFoundError(
                "找不到 grid_engine.exe，请先编译 native/build/Release"
            )

        DATA_DIR.mkdir(parents=True, exist_ok=True)
        args = [str(exe), "--data", str(DATA_DIR), "--poll-ms", str(max(500, int(poll_ms)))]
        if dry_run:
            args.append("--dry-run")

        creationflags = 0
        if os.name == "nt":
            creationflags = getattr(subprocess, "CREATE_NO_WINDOW", 0)

        proc = subprocess.Popen(
            args,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            encoding="utf-8",
            errors="replace",
            cwd=str(ROOT),
            creationflags=creationflags,
        )
        reader = threading.Thread(target=self._read_stdout, args=(proc,), daemon=True)
        reader.start()

        with self._lock:
            self._proc = proc
            self._reader = reader
            self._dry_run = dry_run
            self._poll_ms = poll_ms
            self._started_at = time.time()
            self._log_tail.append(f"[start] {' '.join(args)}")

        time.sleep(0.3)
        if proc.poll() is not None:
            raise RuntimeError(f"引擎启动后立即退出, code={proc.returncode}")
        return self.status()

    def stop(self) -> dict:
        with self._lock:
            proc = self._proc
        if proc is None:
            return self.status()
        if proc.poll() is None:
            proc.terminate()
            try:
                proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait(timeout=3)
        self._append_log(f"[stop] exit={proc.returncode}")
        with self._lock:
            self._proc = None
        return self.status()


native_engine = NativeGridEngine()
