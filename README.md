# SXE Core - Strategy eXecution Engine

A C++17 engine that runs trading strategies as plug-in libraries, with a realistic simulator
of USDT-margined perpetual futures.

The strategy is compiled as a separate shared library (`.so` on Linux, `.dll` on Windows)
and knows nothing about where its prices come from. The engine loads it at runtime, checks
its interface, feeds it market data step by step and executes its orders on a simulated
exchange that reproduces the rules of a real one: market and limit orders, taker and maker fees, slippage, isolated/cross
margin, tiered maintenance margin, funding every 8 hours and liquidation.

Version 1.3.1 - changes in [CHANGELOG.md](CHANGELOG.md)  
Italian version: [README_IT.md](README_IT.md)


## Highlights

### Strategies as plug-ins
Write a class, add one macro, build a shared library. The engine refuses libraries that
are not strategies or that were built against a different interface version. Strategies can also be
built outside the project tree with a single CMake function.

### Deterministic, single-threaded engine
One step = data -> funding -> exchange -> `Run()` of the strategy.
Same inputs, same result to the cent: verified by golden tests.

### Realistic futures simulation
Modelled on Bitget BTCUSDT perpetual, all values configurable.

- market orders with taker fee and slippage; limit orders (buy, close) with maker fee and no
  slippage, filled at their price when the market goes through them by one tick (or just
  touches them), with the margin of open buy orders reserved
- quantity step, minimum quantity, minimum notional
- isolated or cross margin
- progressive tiered maintenance margin (each slice of the position pays its tier's rate)
  and a tier-by-tier liquidation price solver
- funding every 8 hours from a historical file, with a fallback rate and a stress multiplier
- liquidation: isolated loses the position margin and continues, cross loses the account
  and stops

### Intra-candle price path
Each candle is walked as a sequence of ticks (close only, by candle direction, low first or
high first) with optional intermediate ticks, so orders and liquidations happen inside the
candle. The strategy only sees closed candles: no look-ahead.

### Cycle archive
A cycle goes from the first buy to the full close. When it closes, the engine creates
`Backup/b<N>` with a `result.txt` summary, moves there all the log files of the cycle and
appends a row to `total_result.txt`.

### Resume
Stop a simulation with Ctrl+C and resume it later: the result is identical to an
uninterrupted run.

### Configurable data folder and date range
Historical data live in any folder (`[PATH] data`). The period to simulate is set by month
(2023-01 -> 2025-10) or by day, with a warning if the data do not cover it.

### Strict configuration
Every key is validated at startup and all errors are reported at once.

### Embeddable
The same engine is also a shared library with a C interface (`libSXE.so` on Linux,
`SXE.dll` on Windows, `sxe.h`), to use it from other programs and languages: see [Using SXE Core as a library](#using-sxe-core-as-a-library).

### Fast
About 700,000 steps per second on a Raspberry Pi 4: two years of 1-hour candles with
intra-candle ticks run in 0.2 seconds.

### Tested
25 CTest tests on fixed real market data (BTCUSDT 2023-2024) and on hand-made candles,
run by GitHub Actions on every push on Linux arm64 (GCC 14) and Windows x64 (MSVC), with
the same golden references on both.


## How it works

```
    candles + funding files
              |
              v
       +-------------+       +---------------------------------------+
       |  Data feed  | ----> |  Engine (single thread), every step:  |
       |  ticks in   |       |    1. next tick                       |
       |  the candle |       |    2. funding every 8 hours           |
       +-------------+       |    3. exchange: equity, liquidation   |
                             |    4. strategy.Run(ctx)               |
                             |    5. archive closed cycles, state    |
                             +---------------------------------------+
                                  |              |              |
                                  v              v              v
                         +--------------+  +-----------+  +---------------+
                         |  Simulated   |  | Strategy  |  |  Target/Log   |
                         |  exchange    |  | plug-in   |  |  Target/Backup|
                         |  fees, margin|  | libX.so   |  |               |
                         |  tiers, liq. |  |           |  |               |
                         +--------------+  +-----------+  +---------------+
```

The strategy implements five methods, called in this order:

| Method | When | What |
|---|---|---|
| `GetInfo()` | once | name, version, description |
| `LoadConfig(path)` | once | reads its own `.ini` |
| `Init(InitContext)` | once | starting account and position, exchange rules, log and backup folders |
| `Run(StepContext&)` | every step | decides, places orders through `ctx.orders`, returns |
| `Term()` | once | final saves and summary |

`Run()` executes one step and returns: the loop belongs to the engine. The strategy never
reads the clock, never touches the network and never talks to the exchange directly. Time,
prices, account and position arrive in the context; orders go through `ctx.orders`,
synchronously:

| Order | Meaning |
|---|---|
| `MarketBuy(usdt, leverage)` | buy at market (taker fee, slippage) |
| `Close()` | close the whole position at market |
| `LimitBuy(price, usdt, leverage)` | buy limit below the market (maker fee when filled) |
| `LimitClose(price)` | close limit of the whole position above the market |
| `Move(id, price)`, `Cancel(id)`, `CancelAll()` | change or remove open limit orders |

Every result says the order type (market or limit), the fee rate and the order id. Open
limit orders are in `ctx.vOpenOrders`, the limit orders filled since the previous step in
`ctx.vFills`. On a liquidation the exchange cancels all open orders.


## Quick start

### Windows, ready to run
Every release has `SXE-Core-windows-x64.zip`: extract it and run `Bin\SXE.exe`. It contains
the engine, the example strategy and configuration, the test data and the C interface
(`SXE.dll`, `SXE.lib`, `Include\sxe.h`). It needs the Microsoft Visual C++ Redistributable
(x64), already present on most systems.

### Build on Linux
Requirements: CMake 3.16 or later, a C++17 compiler (tested with GCC 14 on Raspberry Pi OS
64-bit).

```sh
./comp_All.sh test        # build, install into Target/ and run the tests
Target/Bin/SXE            # run the example strategy on the data shipped with the repository
```

`Target/Bin/SXE path/to/other.ini` uses another configuration. A status line shows date,
progress, price and equity. At the end the engine prints a summary (return, max drawdown,
cycles, liquidations, fees, funding, steps per second) and leaves one `Target/Backup/b<N>`
folder per cycle.

Build options:

```sh
./comp_All.sh             # Release build
./comp_All.sh debug       # Debug build
./comp_All.sh test        # Release build + tests
./comp_All.sh clean       # removes the CMake work folder CMakeWork/
```

### Build on Windows
Requirements: Visual Studio 2022 with "Desktop development with C++". From an
"x64 Native Tools Command Prompt", in the project folder:

```bat
cmake -S . -B CMakeWork -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build CMakeWork
ctest --test-dir CMakeWork
cmake --install CMakeWork
Target\Bin\SXE.exe
```


## Example result

The example strategy (Template, moving average crossover 20/50) on the data shipped with
the repository: BTCUSDT 1h, 2023-2024, initial capital 5000 USDT, isolated margin.
`Target/Bin/SXE` prints:

```text
Simulation finished (end of data) at 2025-01-01 00:00: equity 7183.89 USDT (+43.68% on the
initial capital), drawdown max -19.42%, closed cycles 219 (in profit 81), liquidations 0,
orders 439, rejected 0, fees 832.42 USDT, funding 795.75 USDT, last cycle still open
(archived); 136901 steps in 0.2 s (781833 steps/s)
```

Equity and drawdown at the close of each cycle, from `Target/Backup/total_result.txt`
(the -19.42% above also counts open positions, tick by tick):

![Equity and drawdown of the Template strategy](docs/equity_template.svg)

The chart is drawn by `python3 docs/equity_chart.py Target/Backup/total_result.txt out.svg`
(standard library only), also for your own runs.

The Template shows how to write a strategy; it is not a strategy to trade.


## Using your own data

In `Target/Config/EngineConfig.ini`:

```ini
[PATH]
data = /path/to/your/MarketData          ; folder with the historical data

[SIMULATION]
data_file    = BTCUSDT_1h.csv            ; inside the data folder (or an absolute path)
funding_file = BTCUSDT_funding.csv       ; inside the data folder, empty = fixed funding_rate
start_date   = 2023-01
end_date     = 2025-10
```

Relative paths in `EngineConfig.ini` start from `Target/Bin`, so the engine works from any
folder it is launched from.

File formats:

```
candles   date;time(UTC);open;high;low;close;volume
          2023-01-01;00:00;16541.77;16545.7;16508.39;16529.67;4364.8357

funding   timestamp,funding_rate
          2023-01-01 00:00,0.00010000
```


## Writing a strategy

1. Copy `file/Strategies/Template` (a moving-average crossover) to a new folder and rename
   the class and `NAME` in its `CMakeLists.txt`. Copy `Target/Config/Template.ini` as the
   configuration of the new strategy.

2. Implement the methods of `CStrategyBase` (`file/SDK/StrategyBase.h`).

3. Keep the export macro at the end of the `.cpp`:

   ```cpp
   SXE_EXPORT_STRATEGY(CMyStrategy)
   ```

4. Every folder of `file/Strategies` with a `CMakeLists.txt` is built automatically.
   Or build it in a separate project against an installed SXE Core:

   ```cmake
   cmake_minimum_required(VERSION 3.16)
   project(MyStrategies CXX)
   include(<SXE folder>/cmake/SXEStrategy.cmake)
   sxe_add_strategy(NAME My SOURCES CMyStrategy.cpp CONFIG My.ini)
   ```

   This installs `libMy.so` (Windows: `libMy.dll`) into `Target/Bin/Strategies` and `My.ini`
   into `Target/Config`.

5. Select it in `EngineConfig.ini`:

   ```ini
   [STRATEGY]
   library = My
   config  = ../Config/My.ini
   ```

   A name without extension becomes `libMy.so` on Linux and `libMy.dll` on Windows, so the
   same configuration works on both; a full file name is also accepted.

Files the strategy writes in `InitContext::strLogDir` are archived with the cycle
automatically.

A strategy that saves its own state (for example next to its `.ini`) reloads it only when
`InitContext::bResume` is true, that is when the engine resumes a saved simulation. When it
is false the run starts from scratch and any state left by a previous run must be ignored.


## Using SXE Core as a library

Besides the `SXE` program, the build produces `libSXE.so` (Windows: `SXE.dll`): the same
engine behind a C interface, [`file/Api/Include/sxe.h`](file/Api/Include/sxe.h), that any language able to call
C functions can use (C, C++ with any compiler, Python, C#, Rust, ...).

```c
#include <stdio.h>
#include "sxe.h"

static void on_cycle(void* user, const sxe_cycle* c)
{
    (void)user;
    printf("cycle %lld: %+.2f%% (%s)\n", (long long)c->cycle, c->growth_pct, c->closed_by);
}

int main(void)
{
    sxe_engine* e = sxe_engine_create();
    sxe_engine_load_config(e, "Target/Config/EngineConfig.ini");
    sxe_engine_set(e, "SIMULATION", "initial_capital", "10000");
    sxe_engine_set_option(e, SXE_OPT_ARCHIVE, 0);        /* no b<N> folders */
    sxe_engine_on_cycle(e, on_cycle, NULL);

    if(sxe_engine_run(e) != SXE_OK)
        fprintf(stderr, "%s\n", sxe_engine_error(e));
    else
        printf("return %+.2f%%, max drawdown %.2f%%\n",
               sxe_engine_result(e)->return_pct, sxe_engine_result(e)->max_drawdown_pct);

    sxe_engine_destroy(e);
    return 0;
}
```

```sh
gcc -std=c99 main.c -I file/Api/Include -L Target/Bin -lSXE -Wl,-rpath,$PWD/Target/Bin -o main
```

On Windows (x64 Native Tools Command Prompt):

```bat
cl main.c /I file\Api\Include Target\Bin\SXE.lib
set PATH=%CD%\Target\Bin;%PATH%
main.exe
```

Windows finds `SXE.dll` and the engine DLLs it needs only next to the program or in `PATH`:
copy the program into `Target\Bin` or add that folder to `PATH`, as above.

- One engine = one run: configure, `sxe_engine_run()`, read the result, destroy. Several
  engines can run at the same time, each in its own thread.
- Configuration: the sections and keys of `EngineConfig.ini`, from a file
  (`sxe_engine_load_config`), one by one (`sxe_engine_set`) or both. Same checks as the
  `SXE` program; every error is returned by `sxe_engine_error()`. Relative paths start from
  the folder of `libSXE.so` / `SXE.dll` (`Target/Bin`), like for `SXE`.
- Callbacks for log messages, progress (every N steps) and every closed cycle.
- Options to turn off the archive, the state file, the order log and the log files, for
  programs that only need the numbers.
- `sxe_engine_stop()` from another thread (or from a callback) closes the run normally.

The tests run the golden references through the C interface too, from programs written in C
(`file/Tests/Api`).


## Configuration

`Target/Config/EngineConfig.ini`, every key is commented in the file:

| Section | Content |
|---|---|
| `[ENGINE]` | mode (SIM), symbol |
| `[STRATEGY]` | strategy library and its configuration file |
| `[PATH]` | Log, Backup and historical data folders |
| `[EXCHANGE]` | margin mode, taker and maker fees, slippage, maintenance margin tiers, quantity step, minimum notional |
| `[SIMULATION]` | date range, initial capital, data and funding files, funding fallback and multiplier, intra-candle path, when a limit order is filled, liquidation on/off, resume, state and order log files |
| `[LOG]` | info / warning / error / console output |


## Output

| Path | Content |
|---|---|
| `Target/Log/` | files of the cycle in progress: `log.txt`, `error.txt`, order log (type, price, fee and fee rate of every order), strategy files, simulation state |
| `Target/Backup/b<N>/` | one folder per closed cycle: `result.txt` (open/close, prices, growth, drawdown, max leverage, fees, funding) plus the files moved from Log |
| `Target/Backup/total_result.txt` | one row per cycle, ready for a spreadsheet |


## Tests

`./comp_All.sh test` runs 25 tests on fixed real data (`file/Tests/Data`: BTCUSDT 1h and
funding, 2023-2024) and on a few hand-made candles:

| Test | Checks |
|---|---|
| `loader_not_strategy` | the engine rejects a library that is not a strategy, |
| `loader_old_api` | was built with another interface version, |
| `loader_missing` | or does not exist |
| `config_example` | the example `EngineConfig.ini` is valid |
| `config_long_path`, `config_line_too_long` | a path longer than 200 characters is accepted; a line over the limit (4094 characters) is reported as too long |
| `golden_template` | the Template strategy gives exactly the reference result |
| `golden_isolated` | orders inside the candle, funding, liquidation (isolated margin) |
| `golden_crossed` | the same with cross margin |
| `golden_low_first` | extreme intra-candle path: low first |
| `golden_high_first` | extreme intra-candle path: high first |
| `golden_bitget` | real exchange parameters: tiered maintenance margin, low slippage, funding multiplier |
| `golden_limit` | limit buy and limit close, the close moved when the average price changes |
| `no_liquidation_warning` | with `liquidation = 0` the engine warns when the equity goes below zero: the result is not realistic |
| `resume` | a run stopped mid-candle and resumed gives the same cycles as an uninterrupted one |
| `resume_limit` | the same with open limit orders |
| `limit_through` | hand-made candles: a limit only touched is not filled, one gone through by a tick is filled at its price with the maker fee |
| `limit_touch` | the same with `limit_fill = touch`: filled as soon as it is touched |
| `multirun_golden_template` | several engines in one process: a full run, a run that writes no files and 4 runs in parallel give the same numbers |
| `multirun_golden_isolated` | the same with liquidations |
| `api_golden_template`, `api_golden_isolated`, `api_golden_limit` | the same golden runs through the C interface (`libSXE.so` / `SXE.dll`) from a program written in C |
| `api_checks` | C interface: callbacks, options without files, configuration without files, stop, every error code |
| `benchmark` | steps per second (measurement only) |

Golden tests compare every cycle with the references in `file/Tests/Golden`: any change to
the simulator that alters even one cycle makes them fail. After an intended change the
references are regenerated (and must be reviewed) with:

```sh
SXE_UPDATE_GOLDEN=1 ctest --test-dir CMakeWork -R golden
```


## Project layout

| Folder | Content |
|---|---|
| `file/` | all sources, one folder per component (see [file/README.md](file/README.md)) |
| `file/Api` | C interface: `sxe.h` and `libSXE.so` / `SXE.dll` |
| `file/Engine` | the engine loop, strategy loader, cycle archive, state |
| `file/Exchange` | exchange interface and simulated exchange |
| `file/DataFeed` | data feed interface and CSV feed with intra-candle ticks |
| `file/SDK` | the only headers a strategy needs |
| `file/Settings` | configuration and INI reader |
| `file/Common` | logger, thread, time utilities, shared types |
| `file/Strategies/Template` | example strategy |
| `file/Tests` | tests, fixed data, golden references |
| `cmake/` | `sxe_add_strategy`, also for strategies outside the project |
| `docs/` | example chart and `equity_chart.py`, which draws it from `total_result.txt` |
| `Target/Config` | example configuration: `EngineConfig.ini`, `Template.ini` |
| `Target/Bin`, `Log`, `Backup` | created by the build and by the runs (not in the repository) |


## Limits of the public edition

- Simulation only: `mode = REAL` is reserved for live trading, not part of this edition.
- Long positions with market and limit orders (open/increase, full close), one symbol per run.
- Linux and Windows x64; macOS is not tested.


## Public and private edition

This repository is the public edition of SXE Core, kept at the version described here.
Engine features developed afterwards belong to the author's private edition and are listed
below. For information about the private edition see [Contact](#contact).

### Parameter tuning - available
SXE_Tune, a separate program: it runs many simulations of the same strategy in parallel
with different parameters and ranks them. SXE keeps running on its own. Grid, random and
zoom search (wide first, then narrower around the best); rules between parameters written as
named formulas in `Optimize.ini`, with a summary to check before the start; constraints,
ranking and the best `.ini` ready for SXE. The best results are then checked on periods not
used for the tuning and with the neighbouring parameter values: the robust choice is the one
whose worst case is the highest.

### Graphical interface - planned
Configuration, simulation with charts (price, orders, equity, drawdown, cycles) and
parameter tuning from a window. Separate program: SXE keeps running on its own without it.


## Contact

Francesco Tinelli - [LinkedIn](https://www.linkedin.com/in/francesco-tinelli)

Write to me for questions about SXE Core, for permission to use it commercially
(see [License](#license)) or for information about the private edition.
Bug reports are welcome as GitHub issues.


## License

Copyright (c) 2026 Francesco Tinelli

SXE Core is released under the PolyForm Noncommercial License 1.0.0 (see [LICENSE](LICENSE)):
free to study, use and modify for any noncommercial purpose; commercial use requires the
author's permission.

The INI parser is [inih](https://github.com/benhoyt/inih) by Ben Hoyt, BSD-3-Clause license,
see `file/Settings/LICENSE_inih.txt`.


## Disclaimer

This software is a simulation and research tool. It is not financial advice. Past results,
simulated or real, do not guarantee future results. Trading leveraged derivatives can lose
more than the initial capital.
