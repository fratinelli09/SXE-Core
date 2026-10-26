# SXE Core - changes

Version numbers: architecture.structure.fix

The strategy interface has its own number (`SXE_API_VERSION` in `file/SDK/StrategyBase.h`):
the engine refuses strategies built for a different one.


## 1.3.1 - 2026-10-26

Same results as 1.3.0: the golden references are unchanged.

### Fixes

- INI files: lines up to 4094 characters (inih read at most 198, so a configuration with a
  long path, for example a deep folder or a OneDrive folder on Windows, was refused with
  "Invalid line"). A longer line is now reported as "Line N of <file> is too long", also
  through the C interface.
- The Windows package contains the licence of inih (`LICENSE_inih.txt`), required by its
  BSD licence for binary distributions.

### Tests (25 in total)

- `config_long_path`: a configuration with a path of more than 300 characters runs.
- `config_line_too_long`: a line of more than 4094 characters is reported as too long.
- `api_checks`: the same through `sxe_engine_set()`.


## 1.3.0 - 2026-10-26

The strategy interface does not change (`SXE_API_VERSION` 3), but strategies must be
rebuilt on each system. Same results as 1.2.0: the golden references are unchanged and
are the same on Linux and Windows.

### Windows

- Builds with Visual Studio 2022 (MSVC, x64): the engine, `SXE.exe`, the strategies as
  DLLs and the C interface `SXE.dll` + `SXE.lib`.
- CI: a Windows x64 job builds and runs the 23 tests; every release gets
  `SXE-Core-windows-x64.zip`, ready to run.
- `[STRATEGY] library` accepts a name without extension: `Template` becomes
  `libTemplate.so` on Linux and `libTemplate.dll` on Windows.
- `-ffp-contract=off` with GCC/Clang: no fused multiply-add, the same results on every
  processor.
- `.gitattributes`: text files always with LF line ends, also in a Windows checkout.


## 1.2.0 - 2026-10-26

The strategy interface does not change (`SXE_API_VERSION` 3): strategies built for 1.1.0
work without being rebuilt. Same results as 1.1.0: the golden references are unchanged.

### Engine usable from other programs (first step towards the C library)

- Several engines can run in the same process, each in its own thread: `CEngine` takes
  its own `Logger` (until now there was one per process), `run_here()` runs the loop in
  the calling thread and `GetResult()` returns the summary numbers as a `SimResult`.
- `EngineOptions` turns off the cycle archive, the state file and the order log, for runs
  that only need the numbers.
- `CSettings::SetOutputFolders()` moves the output of a run to other folders.
- New tests `multirun_golden_template` and `multirun_golden_isolated` (19 in total).

### C interface: libSXE.so

- New library `libSXE.so` with the C interface `file/Api/Include/sxe.h`: create an engine,
  configure it from `EngineConfig.ini` and/or key by key, run it, read the result. Callbacks
  for log messages, progress and closed cycles; options to turn off the engine files;
  `sxe_engine_stop()` from any thread. Only the `sxe_` functions are exported.
- `CSettings::LoadText()`: configuration from text instead of a file.
- `Logger`: log files can be turned off, messages can be passed to a callback.
- New tests: `api_golden_template`, `api_golden_isolated`, `api_golden_limit` (golden runs
  through the C interface from a C program) and `api_checks` (23 in total).
- Tests: the comparison of resumed runs skipped the cycle open at the end by its old Italian
  description; messages of the test script in English.

## 1.1.0 - 2026-10-23

### Limit orders

- Next to the market orders the strategy can place limit orders through `ctx.orders`:
  `LimitBuy(price, usdt, leverage)` below the market and `LimitClose(price)` of the whole
  position above it, then `Move(id, price)`, `Cancel(id)` and `CancelAll()`. A limit that
  would be filled at once (buy at or above the market, close at or below) is rejected.
- A limit order is filled at its own price, without slippage, with the new maker fee
  (`[EXCHANGE] fee_maker`, default 0.0002 = 0.02%). Market orders keep the taker fee and the
  slippage.
- `[SIMULATION] limit_fill`: `through` (default) = filled when the price goes past the limit
  by `tick_step`, since a limit that is only touched may stay unfilled behind other orders
  at the same price; `touch` = filled as soon as the price reaches it.
- An open buy limit reserves its margin and fee: the available margin goes down. A buy limit
  that no longer has enough margin when it is reached is cancelled. On a liquidation all
  open orders are cancelled; when the position is closed the close limits are cancelled.
- `StepContext`: `vOpenOrders` (open limit orders) and `vFills` (limit orders filled since
  the previous step). `OrderResult`: order type, fee rate and order id.
- Order log: every order with its type (market/limit), id, limit price and fee rate; new
  rows `LIMIT_BUY`, `LIMIT_CLOSE`, `MOVE`, `CANCEL`, `FILL_BUY`, `FILL_CLOSE`. A cycle closed
  by a limit order is marked "limit" in `total_result.txt`. The final summary splits maker
  and taker fees when limit orders were used.
- The simulation state saves the open limit orders: resume works with orders open. States
  saved by 1.0.x are still read.
- Strategy interface 2 -> 3: strategies must be rebuilt. Strategies that use only market
  orders give exactly the same results as before.

### Tests

- `golden_limit`: limit buy and limit close on real data, the close moved with the average
  price.
- `resume_limit`: stop and resume with open limit orders = uninterrupted run.
- `limit_through`, `limit_touch`: hand-made candles checking when a limit is filled, its
  price and its fee.


## 1.0.1 - 2026-10-22

### Fixes

- A strategy could not tell a resumed simulation from a new one.
  A strategy that saves its own state reloaded the state of a previous run even when the
  simulation started from scratch (`resume = 0`). `InitContext` now has `bResume`: true only
  when the engine has restored a saved simulation state, false otherwise.
  Strategy interface 1 -> 2: strategies must be rebuilt.

- With `liquidation = 0` a simulation could give unrealistic results without any notice.
  A position whose equity goes below zero would already have been liquidated on a real
  exchange. The engine now prints a warning the first time this happens in a cycle and the
  final summary reports how many cycles went below zero.

### Tests

- `no_liquidation_warning`: the warning is printed when liquidation is disabled.


## 1.0.0 - 2026-10-22

First public release.
