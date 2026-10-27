"""sxe - SXE Core from Python

Runs the SXE Core engine through its C interface (libSXE.so / SXE.dll, see sxe.h) with
ctypes: no build step and no dependencies, Python 3.8 or later.

    import sxe

    r = sxe.run("Target/Config/EngineConfig.ini",
                settings={"SIMULATION": {"initial_capital": 10000}},
                files=False)
    print(r.return_pct, r.max_drawdown_pct, len(r.cycle_list))

One Engine = one run, as in the C interface. Several engines can run at the same time in
different threads: run() releases the GIL while the engine works.

The library is searched in this order: sxe.load(path), the SXE_LIBRARY environment
variable, the folder of this file (Target/Bin after the install).

Licence: PolyForm Noncommercial 1.0.0 (see LICENSE): commercial use needs the permission
of the author.
"""
from __future__ import annotations

import ctypes
import enum
import os
import signal
import sys
import threading
from dataclasses import dataclass, field
from datetime import datetime, timezone
from typing import Callable, Dict, List, Mapping, Optional, Union

__all__ = [
    "load", "version", "api_version", "run", "Engine",
    "Result", "Cycle", "Progress", "LogLevel",
    "Error", "ArgumentError", "ConfigError", "StateError", "SetupError", "RunError", "InternalError",
]

C_API_VERSION = 1   # SXE_C_API_VERSION this module is written for

Value = Union[str, int, float, bool]


# ---------- data passed to the caller ----------

class LogLevel(enum.IntEnum):
    INFO = 0
    WARNING = 1
    ERROR = 2


@dataclass(frozen=True)
class Progress:
    time: datetime
    step: int
    cycle: int              # cycle in progress, or the next one if no position is open
    price: float
    equity: float
    drawdown_pct: float     # from the highest equity so far, <= 0


@dataclass(frozen=True)
class Cycle:
    """A closed cycle: the same row as in total_result.txt."""
    cycle: int
    open_time: datetime
    close_time: datetime
    capital_start: float
    capital_end: float
    growth_pct: float
    drawdown_pct: float     # worst equity in the cycle against capital_start, <= 0
    max_leverage: float
    buys: int
    entry_price: float
    avg_price: float
    exit_price: float
    fees: float
    funding: float
    closed_by: str          # "strategy", "liquidation", "open at end of simulation"


@dataclass(frozen=True)
class Result:
    """Summary of the run; percentages: 12.5 = 12.5%."""
    finished: bool
    error: bool
    reason: str
    first_time: Optional[datetime]
    last_time: Optional[datetime]
    initial_capital: float
    final_equity: float
    return_pct: float
    max_drawdown_pct: float
    cycles: int             # positions closed by the strategy
    wins: int
    liquidations: int
    orders: int
    rejected: int
    fees: float
    funding: float
    worst_cycle_drawdown_pct: float
    max_leverage: float
    open_at_end: bool
    underwater: int         # cycles with equity below zero: only with [SIMULATION] liquidation = 0
    steps: int
    seconds: float
    cycle_list: List[Cycle] = field(default_factory=list, repr=False)


# ---------- errors: one class per sxe_status ----------

class Error(Exception):
    """Error returned by the engine; str() = sxe_engine_error(), every error on its own line."""
    status = 0

    def __init__(self, message: str, result: Optional[Result] = None):
        super().__init__(message)
        self.result = result    # set by RunError: the summary up to the error


class ArgumentError(Error):  status = 1
class ConfigError(Error):    status = 2
class StateError(Error):     status = 3
class SetupError(Error):     status = 4
class RunError(Error):       status = 5
class InternalError(Error):  status = 6

_ERRORS = {cls.status: cls for cls in (ArgumentError, ConfigError, StateError, SetupError, RunError, InternalError)}


# ---------- C structures and functions of sxe.h ----------

class _CProgress(ctypes.Structure):
    _fields_ = [("time", ctypes.c_int64), ("step", ctypes.c_int64), ("cycle", ctypes.c_int64),
                ("price", ctypes.c_double), ("equity", ctypes.c_double), ("drawdown_pct", ctypes.c_double)]


class _CCycle(ctypes.Structure):
    _fields_ = [("cycle", ctypes.c_int64), ("open_time", ctypes.c_int64), ("close_time", ctypes.c_int64),
                ("capital_start", ctypes.c_double), ("capital_end", ctypes.c_double),
                ("growth_pct", ctypes.c_double), ("drawdown_pct", ctypes.c_double),
                ("max_leverage", ctypes.c_double), ("buys", ctypes.c_int64),
                ("entry_price", ctypes.c_double), ("avg_price", ctypes.c_double),
                ("exit_price", ctypes.c_double), ("fees", ctypes.c_double), ("funding", ctypes.c_double),
                ("closed_by", ctypes.c_char_p)]


class _CResult(ctypes.Structure):
    _fields_ = [("finished", ctypes.c_int), ("error", ctypes.c_int), ("reason", ctypes.c_char_p),
                ("first_time", ctypes.c_int64), ("last_time", ctypes.c_int64),
                ("initial_capital", ctypes.c_double), ("final_equity", ctypes.c_double),
                ("return_pct", ctypes.c_double), ("max_drawdown_pct", ctypes.c_double),
                ("cycles", ctypes.c_int64), ("wins", ctypes.c_int64), ("liquidations", ctypes.c_int64),
                ("orders", ctypes.c_int64), ("rejected", ctypes.c_int64),
                ("fees", ctypes.c_double), ("funding", ctypes.c_double),
                ("worst_cycle_drawdown_pct", ctypes.c_double), ("max_leverage", ctypes.c_double),
                ("open_at_end", ctypes.c_int), ("underwater", ctypes.c_int64),
                ("steps", ctypes.c_int64), ("seconds", ctypes.c_double)]


_LOG_FN      = ctypes.CFUNCTYPE(None, ctypes.c_void_p, ctypes.c_int, ctypes.c_char_p)
_PROGRESS_FN = ctypes.CFUNCTYPE(None, ctypes.c_void_p, ctypes.POINTER(_CProgress))
_CYCLE_FN    = ctypes.CFUNCTYPE(None, ctypes.c_void_p, ctypes.POINTER(_CCycle))

_OPTIONS = {"archive": 1, "state_file": 2, "order_log": 3, "log_files": 4, "console": 5, "progress_every": 6}

_lib = None
_lib_lock = threading.Lock()


def _default_library() -> str:
    path = os.environ.get("SXE_LIBRARY")
    if path:
        return path
    here = os.path.dirname(os.path.abspath(__file__))
    if sys.platform == "win32":
        name = "SXE.dll"
    elif sys.platform == "darwin":
        name = "libSXE.dylib"
    else:
        name = "libSXE.so"
    return os.path.join(here, name)


def load(path: Optional[str] = None) -> None:
    """Loads the library (only the first call counts); without path: SXE_LIBRARY or next to sxe.py."""
    global _lib
    with _lib_lock:
        if _lib is not None:
            return
        path = os.path.abspath(path or _default_library())
        if not os.path.isfile(path):
            raise FileNotFoundError(f"SXE library not found: {path} (sxe.load(path) or SXE_LIBRARY)")
        if sys.platform == "win32":
            # the engine DLLs are next to SXE.dll
            os.add_dll_directory(os.path.dirname(path))
        lib = ctypes.CDLL(path)

        lib.sxe_api_version.restype = ctypes.c_int
        lib.sxe_api_version.argtypes = []
        lib.sxe_version.restype = ctypes.c_char_p
        lib.sxe_version.argtypes = []
        lib.sxe_engine_create.restype = ctypes.c_void_p
        lib.sxe_engine_create.argtypes = []
        lib.sxe_engine_destroy.restype = None
        lib.sxe_engine_destroy.argtypes = [ctypes.c_void_p]
        for name, args in (("sxe_engine_set_base_dir", [ctypes.c_char_p]),
                           ("sxe_engine_load_config", [ctypes.c_char_p]),
                           ("sxe_engine_set", [ctypes.c_char_p, ctypes.c_char_p, ctypes.c_char_p]),
                           ("sxe_engine_set_option", [ctypes.c_int, ctypes.c_int64]),
                           ("sxe_engine_on_log", [_LOG_FN, ctypes.c_void_p]),
                           ("sxe_engine_on_progress", [_PROGRESS_FN, ctypes.c_void_p]),
                           ("sxe_engine_on_cycle", [_CYCLE_FN, ctypes.c_void_p]),
                           ("sxe_engine_setup", []),
                           ("sxe_engine_run", [])):
            fn = getattr(lib, name)
            fn.restype = ctypes.c_int
            fn.argtypes = [ctypes.c_void_p] + args
        lib.sxe_engine_stop.restype = None
        lib.sxe_engine_stop.argtypes = [ctypes.c_void_p]
        lib.sxe_engine_result.restype = ctypes.POINTER(_CResult)
        lib.sxe_engine_result.argtypes = [ctypes.c_void_p]
        lib.sxe_engine_error.restype = ctypes.c_char_p
        lib.sxe_engine_error.argtypes = [ctypes.c_void_p]

        found = lib.sxe_api_version()
        if found != C_API_VERSION:
            raise ImportError(f"{path}: C interface version {found}, sxe.py needs version {C_API_VERSION}")
        _lib = lib


def _get_lib():
    if _lib is None:
        load()
    return _lib


def version() -> str:
    """Version of SXE Core, e.g. "1.4.0"."""
    return _text(_get_lib().sxe_version())


def api_version() -> int:
    """SXE_C_API_VERSION the library was built with."""
    return _get_lib().sxe_api_version()


# ---------- conversions ----------

def _bytes(s: str) -> bytes:
    return s.encode("utf-8")


def _text(b: Optional[bytes]) -> str:
    return b.decode("utf-8", errors="replace") if b else ""


def _time(t: int) -> datetime:
    return datetime.fromtimestamp(t, timezone.utc)


def _value(value: Value) -> str:
    if isinstance(value, bool):
        return "1" if value else "0"
    if isinstance(value, float):
        return repr(value)
    return str(value)


def _cycle(c: _CCycle) -> Cycle:
    return Cycle(c.cycle, _time(c.open_time), _time(c.close_time), c.capital_start, c.capital_end,
                 c.growth_pct, c.drawdown_pct, c.max_leverage, c.buys, c.entry_price, c.avg_price,
                 c.exit_price, c.fees, c.funding, _text(c.closed_by))


def _result(r: _CResult, cycles: List[Cycle]) -> Result:
    return Result(bool(r.finished), bool(r.error), _text(r.reason),
                  _time(r.first_time) if r.first_time else None, _time(r.last_time) if r.last_time else None,
                  r.initial_capital, r.final_equity, r.return_pct, r.max_drawdown_pct,
                  r.cycles, r.wins, r.liquidations, r.orders, r.rejected, r.fees, r.funding,
                  r.worst_cycle_drawdown_pct, r.max_leverage, bool(r.open_at_end), r.underwater,
                  r.steps, r.seconds, list(cycles))


# ---------- engine ----------

class Engine:
    """One run of the engine: configure, run(), read the result.

    Use it from one thread at a time; only stop() may be called from another thread.
    The callbacks are called in the thread of run(). An exception raised in a callback
    stops the engine at the next step and is raised again by run().
    """

    def __init__(self):
        self._lib = _get_lib()
        self._handle = self._lib.sxe_engine_create()
        if not self._handle:
            raise MemoryError("sxe_engine_create failed")
        self._on_log: Optional[Callable[[LogLevel, str], None]] = None
        self._on_progress: Optional[Callable[[Progress], None]] = None
        self._on_cycle: Optional[Callable[[Cycle], None]] = None
        self._progress_every = 1000
        self._cycles: List[Cycle] = []
        self._exception: Optional[BaseException] = None
        # the C callbacks must stay alive as long as the engine
        self._c_log = _LOG_FN(self._log_entry)
        self._c_progress = _PROGRESS_FN(self._progress_entry)
        self._c_cycle = _CYCLE_FN(self._cycle_entry)
        self._lib.sxe_engine_on_cycle(self._handle, self._c_cycle, None)
        # always a progress callback: it is where Ctrl+C reaches Python during run()
        self._lib.sxe_engine_on_progress(self._handle, self._c_progress, None)

    # --- lifetime ---
    def close(self) -> None:
        """Destroys the engine (also done by 'with' and by the garbage collector)."""
        if getattr(self, "_handle", None):
            self._lib.sxe_engine_destroy(self._handle)
            self._handle = None

    def __enter__(self) -> "Engine":
        return self

    def __exit__(self, *exc) -> None:
        self.close()

    def __del__(self):
        try:
            self.close()
        except Exception:
            pass

    def _check(self, status: int) -> None:
        if status != 0:
            cls = _ERRORS.get(status, Error)
            raise cls(_text(self._lib.sxe_engine_error(self._handle)) or f"error {status}")

    def _h(self):
        if not self._handle:
            raise StateError("the engine has been closed")
        return self._handle

    # --- configuration, before the setup ---
    def set_base_dir(self, folder: Union[str, os.PathLike]) -> None:
        """Folder of the relative paths (default: the folder of the library, Target/Bin)."""
        self._check(self._lib.sxe_engine_set_base_dir(self._h(), _bytes(os.fspath(folder))))

    def load_config(self, ini_path: Union[str, os.PathLike]) -> None:
        """Adds the keys of an INI file (EngineConfig.ini); keys already set are replaced."""
        self._check(self._lib.sxe_engine_load_config(self._h(), _bytes(os.fspath(ini_path))))

    def set(self, section: str, key: str, value: Value) -> None:
        """Sets one key of EngineConfig.ini; numbers and bools are converted to text."""
        self._check(self._lib.sxe_engine_set(self._h(), _bytes(section), _bytes(key), _bytes(_value(value))))

    def update(self, settings: Mapping[str, Mapping[str, Value]]) -> None:
        """Sets several keys: {"SECTION": {"key": value, ...}, ...}."""
        for section, keys in settings.items():
            for key, value in keys.items():
                self.set(section, key, value)

    def options(self, *, archive: Optional[bool] = None, state_file: Optional[bool] = None,
                order_log: Optional[bool] = None, log_files: Optional[bool] = None,
                console: Optional[bool] = None, progress_every: Optional[int] = None) -> None:
        """Options that are not in EngineConfig.ini (see sxe_option in sxe.h); None = unchanged."""
        values = {"archive": archive, "state_file": state_file, "order_log": order_log,
                  "log_files": log_files, "console": console}
        for name, value in values.items():
            if value is not None:
                self._check(self._lib.sxe_engine_set_option(self._h(), _OPTIONS[name], 1 if value else 0))
        if progress_every is not None:
            if progress_every < 0:
                raise ArgumentError("progress interval must be >= 0")
            self._progress_every = progress_every
            # 0 = no progress for the caller, but the callback stays for Ctrl+C
            self._check(self._lib.sxe_engine_set_option(self._h(), _OPTIONS["progress_every"],
                                                        progress_every or 1000))

    def files(self, enabled: bool) -> None:
        """Turns the archive, state file, order log and log files on or off together."""
        self.options(archive=enabled, state_file=enabled, order_log=enabled, log_files=enabled)

    def on_log(self, fn: Optional[Callable[[LogLevel, str], None]]) -> None:
        """fn(level, message) for every log message; None removes it."""
        self._on_log = fn
        self._check(self._lib.sxe_engine_on_log(self._h(), self._c_log if fn else _LOG_FN(), None))

    def on_progress(self, fn: Optional[Callable[[Progress], None]]) -> None:
        """fn(progress) every progress_every steps (default 1000); None removes it."""
        self._on_progress = fn

    def on_cycle(self, fn: Optional[Callable[[Cycle], None]]) -> None:
        """fn(cycle) for every closed cycle; None removes it. Result.cycle_list has them all anyway."""
        self._on_cycle = fn

    # --- run ---
    def setup(self) -> None:
        """Checks the configuration, creates the output folders, loads strategy and data."""
        self._check(self._lib.sxe_engine_setup(self._h()))

    def run(self) -> Result:
        """Runs the whole simulation in this thread and returns the result.

        In the main thread Ctrl+C stops the engine at the next step, closes the run like the SXE
        program and then raises KeyboardInterrupt (the result stays in result()).
        """
        handle = self._h()
        previous = None
        if (threading.current_thread() is threading.main_thread() and
                signal.getsignal(signal.SIGINT) is signal.default_int_handler):
            # Python runs the handler at the next callback; KeyboardInterrupt raised there
            # would be lost in the C code
            def on_ctrl_c(signum, frame):
                if self._exception is None:
                    self._exception = KeyboardInterrupt()
                self._lib.sxe_engine_stop(handle)
            previous = signal.signal(signal.SIGINT, on_ctrl_c)
        try:
            status = self._lib.sxe_engine_run(handle)     # the GIL is released meanwhile
        finally:
            if previous is not None:
                signal.signal(signal.SIGINT, previous)
        exception, self._exception = self._exception, None
        if exception is not None:
            raise exception
        if status == RunError.status:
            raise RunError(_text(self._lib.sxe_engine_error(handle)), self.result())
        self._check(status)
        return self.result()

    def stop(self) -> None:
        """From any thread: the engine stops at the next step and closes the run normally."""
        if self._handle:
            self._lib.sxe_engine_stop(self._handle)

    def result(self) -> Result:
        """Result so far (finished = False until the run is over)."""
        return _result(self._lib.sxe_engine_result(self._h()).contents, self._cycles)

    # --- C callbacks: exceptions must not cross the C code ---
    def _fail(self, exception: BaseException) -> None:
        if self._exception is None:
            self._exception = exception
        self._lib.sxe_engine_stop(self._handle)

    def _log_entry(self, user, level, message) -> None:
        try:
            if self._on_log is not None and self._exception is None:
                self._on_log(LogLevel(level), _text(message))
        except BaseException as e:
            self._fail(e)

    def _progress_entry(self, user, p) -> None:
        try:
            if self._on_progress is not None and self._progress_every > 0 and self._exception is None:
                c = p.contents
                self._on_progress(Progress(_time(c.time), c.step, c.cycle, c.price, c.equity, c.drawdown_pct))
        except BaseException as e:
            self._fail(e)

    def _cycle_entry(self, user, c) -> None:
        try:
            cycle = _cycle(c.contents)
            self._cycles.append(cycle)
            if self._on_cycle is not None and self._exception is None:
                self._on_cycle(cycle)
        except BaseException as e:
            self._fail(e)


def run(config: Optional[Union[str, os.PathLike]] = None,
        settings: Optional[Mapping[str, Mapping[str, Value]]] = None, *,
        files: bool = True, base_dir: Optional[Union[str, os.PathLike]] = None,
        on_log: Optional[Callable[[LogLevel, str], None]] = None,
        on_progress: Optional[Callable[[Progress], None]] = None,
        on_cycle: Optional[Callable[[Cycle], None]] = None) -> Result:
    """One run: configuration file and/or keys, then the result.

    files=False turns off the archive, the state file, the order log and the log files.
    """
    with Engine() as engine:
        if base_dir is not None:
            engine.set_base_dir(base_dir)
        if config is not None:
            engine.load_config(config)
        if settings:
            engine.update(settings)
        if not files:
            engine.files(False)
        engine.on_log(on_log)
        engine.on_progress(on_progress)
        engine.on_cycle(on_cycle)
        return engine.run()
