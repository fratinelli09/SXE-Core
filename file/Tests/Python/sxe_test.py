"""Checks of sxe.py: results, callbacks, pandas, options, errors, stop, threads.

usage: sxe_test.py <EngineConfig.ini> <work folder>
"""
import dataclasses
import math
import os
import signal
import sys
import threading
from concurrent.futures import ThreadPoolExecutor
from datetime import datetime, timezone

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "Python"))
import sxe  # noqa: E402

failed = 0


def check(cond: bool, what: str) -> None:
    global failed
    if cond:
        print(f"ok:   {what}")
    else:
        failed += 1
        print(f"FAIL: {what}")


def folders(name: str) -> dict:
    d = os.path.join(WORK, name)
    return {"PATH": {"log": f"{d}/Log", "backup": f"{d}/Backup"},
            "SIMULATION": {"state_file": f"{d}/Log/last_row.txt", "order_log": f"{d}/Log/Set_Close_log.txt"}}


def numbers(r: sxe.Result) -> tuple:
    return (r.final_equity, r.max_drawdown_pct, r.cycles, r.wins, r.liquidations, r.orders, r.rejected,
            r.fees, r.funding, r.worst_cycle_drawdown_pct, r.max_leverage, r.steps, r.open_at_end, r.reason)


def pandas_checks(full: sxe.Result) -> None:
    """pandas.DataFrame(cycle_list), as in the README; pandas is required only with SXE_REQUIRE_PANDAS (CI)."""
    try:
        import pandas
    except ImportError:
        if os.environ.get("SXE_REQUIRE_PANDAS"):
            check(False, "pandas installed (required by SXE_REQUIRE_PANDAS)")
        else:
            print("skip: pandas not installed")
        return
    print(f"      pandas {pandas.__version__}")
    df = pandas.DataFrame(full.cycle_list)
    check(len(df) == len(full.cycle_list) and list(df.columns) == [f.name for f in dataclasses.fields(sxe.Cycle)],
          "pandas: one row per cycle, one column per field")
    check(all(isinstance(df[c].dtype, pandas.DatetimeTZDtype) and str(df[c].dt.tz) == "UTC"
              for c in ("open_time", "close_time")), "pandas: times as datetime64 in UTC")
    check(math.isclose(df.fees.sum(), full.fees, rel_tol=1e-9) and
          math.isclose(df.funding.sum(), full.funding, rel_tol=1e-9) and
          df.capital_end.iloc[-1] == full.final_equity and
          df.drawdown_pct.min() == full.worst_cycle_drawdown_pct, "pandas: totals equal to the result")


def main() -> int:
    check(sxe.api_version() == sxe.C_API_VERSION, "interface version of the library")
    check(len(sxe.version()) > 0, "version of SXE Core")

    # full run with every callback
    logs, progress, cycles = [], [], []
    full = sxe.run(CONFIG, folders("full"), on_log=lambda level, msg: logs.append((level, msg)),
                   on_progress=progress.append, on_cycle=cycles.append)
    print(f"      {full.reason}: equity {full.final_equity:.2f} ({full.return_pct:+.2f}%), cycles {full.cycles}, "
          f"liquidations {full.liquidations}, steps {full.steps}")
    check(full.finished and not full.error, "result finished without error")
    check(len(logs) > 0 and all(isinstance(level, sxe.LogLevel) for level, _ in logs), "log callback with LogLevel")
    check(len(progress) == full.steps // 1000 and
          all(b.step > a.step for a, b in zip(progress, progress[1:])) and
          all(p.drawdown_pct <= 0 and p.equity > 0 for p in progress), "progress every 1000 steps, values consistent")
    check(len(cycles) == full.cycles + full.liquidations + int(full.open_at_end) and cycles == full.cycle_list,
          "cycle callback and cycle_list: every closed cycle, liquidation and open cycle at the end")
    check(isinstance(full.first_time, datetime) and full.first_time.tzinfo == timezone.utc and
          all(c.close_time >= c.open_time for c in full.cycle_list), "times as datetime in UTC")
    check(sum(c.closed_by == "liquidation" for c in full.cycle_list) == full.liquidations, "closed_by as text")
    check(os.path.isfile(os.path.join(WORK, "full", "Backup", "total_result.txt")), "full run writes total_result.txt")
    pandas_checks(full)

    # without files: same numbers, no files
    light = sxe.run(CONFIG, folders("light"), files=False)
    check(numbers(light) == numbers(full), "files=False: same numbers")
    check(not os.path.exists(os.path.join(WORK, "light", "Backup", "total_result.txt")) and
          not os.path.exists(os.path.join(WORK, "light", "Log", "log.txt")), "files=False: no files")

    # keys and conversions of the values
    s = folders("capital")
    s["SIMULATION"]["initial_capital"] = 10000
    s["EXCHANGE"] = {"slippage": 0.0002}
    r = sxe.run(CONFIG, s, files=False)
    check(r.initial_capital == 10000 and r.final_equity != full.final_equity, "settings: numbers converted to text")

    # errors as exceptions
    s = folders("bad_value")
    s["EXCHANGE"] = {"margin_mode": "sideways"}
    s["SIMULATION"]["initial_capital"] = -5
    try:
        sxe.run(CONFIG, s)
        check(False, "values not valid: ConfigError")
    except sxe.ConfigError as e:
        check("margin_mode" in str(e) and "initial_capital" in str(e) and e.status == 2,
              "values not valid: ConfigError with every error")
    try:
        sxe.run("/nonexistent/EngineConfig.ini")
        check(False, "missing configuration: ConfigError")
    except sxe.ConfigError as e:
        check("not found" in str(e), "missing configuration: ConfigError")
    with sxe.Engine() as e:
        e.load_config(CONFIG)
        e.update(folders("twice"))
        e.files(False)
        e.run()
        try:
            e.run()
            check(False, "second run refused: StateError")
        except sxe.StateError:
            check(True, "second run refused: StateError")

    # an exception in a callback stops the run and comes out of run()
    class Boom(Exception):
        pass

    def boom(p):
        raise Boom("from the callback")

    try:
        sxe.run(CONFIG, folders("boom"), files=False, on_progress=boom)
        check(False, "exception in a callback raised by run()")
    except Boom:
        check(True, "exception in a callback raised by run()")

    # stop from another thread
    with sxe.Engine() as e:
        e.load_config(CONFIG)
        e.update(folders("stop"))
        e.files(False)
        started = threading.Event()
        e.on_progress(lambda p: started.set())
        threading.Thread(target=lambda: (started.wait(), e.stop()), daemon=True).start()
        r = e.run()
        check(r.finished and not r.error and "stop" in r.reason and r.steps < full.steps,
              "stop() from another thread: the run closes normally")

    # Ctrl+C during run(): the run closes normally, then KeyboardInterrupt
    with sxe.Engine() as e:
        e.load_config(CONFIG)
        e.update(folders("ctrl_c"))
        e.files(False)
        e.on_progress(lambda p: signal.raise_signal(signal.SIGINT) if p.step == 1000 else None)
        try:
            e.run()
            check(False, "Ctrl+C: KeyboardInterrupt after the run closed")
        except KeyboardInterrupt:
            r = e.result()
            check(r.finished and not r.error and "stop" in r.reason and r.steps < 1100,
                  "Ctrl+C: KeyboardInterrupt after the run closed")
    check(signal.getsignal(signal.SIGINT) is signal.default_int_handler, "Ctrl+C: Python handler restored")

    # several engines in parallel threads: same numbers as one at a time
    def one(i: int) -> tuple:
        return numbers(sxe.run(CONFIG, folders(f"thread{i}"), files=False))

    with ThreadPoolExecutor(4) as pool:
        results = list(pool.map(one, range(4)))
    check(all(r == numbers(full) for r in results), "4 engines in parallel threads: same numbers")

    print("OK: all checks passed" if failed == 0 else f"ERROR: {failed} checks failed")
    return 0 if failed == 0 else 1


if __name__ == "__main__":
    if len(sys.argv) < 3:
        print(__doc__, file=sys.stderr)
        sys.exit(1)
    CONFIG, WORK = sys.argv[1], sys.argv[2]
    sys.exit(main())
