# file - SXE Core sources

Everything that is compiled. Every folder has its own `CMakeLists.txt`;
`Include/` = headers, `Src/` = implementation.
Library names are those of Linux: on Windows `libEngine.so` is `Engine.dll`, `libSXE.so` is
`SXE.dll`, and a strategy `libTemplate.so` is `libTemplate.dll`.

### App/ -> SXE
`main.cpp`: reads `Target/Config/EngineConfig.ini`, prepares and starts the engine, handles
Ctrl+C.

### Api/ -> libSXE.so
C interface of the engine, for other programs and languages.
- `Include/sxe.h`: the only header needed, documented in its comments.
- `Src/sxe.cpp`: the C functions on top of `CEngine` and `CSettings`; only the `sxe_`
  functions are exported.

### Python/ -> sxe.py
`sxe.py`: SXE Core from Python, with `ctypes` over the C interface (installed next to
the library).

### Engine/ -> libEngine.so
The engine.
- `CEngine`: single thread, every step data -> exchange -> `Run()` of the strategy.
- `CStrategyLoader`: opens the strategy library and checks its interface.
- `CCycleArchive`: at the end of a cycle creates `Backup/b<N>`, moves the Log files and
  updates `total_result.txt`.
- `CSimStateFile`: saves and reloads the simulation state (`last_row.txt`) for resume.

### Exchange/ -> libExchange.so
- `IExchange`: the exchange as seen by the engine.
- `CSimExchange`: simulated exchange (market and limit orders, taker and maker fees,
  slippage, margin, tiered maintenance margin, funding every 8 hours, liquidation).

### DataFeed/ -> libDataFeed.so
- `IDataFeed`: where prices come from.
- `CCsvFeed`: candles from CSV, walked as ticks inside the candle.

### Settings/ -> libSettings.so
- `CSettings`: reads and validates `EngineConfig.ini`.
- `CReader`: generic INI reader/writer (also used by strategies). inih parser.

### Common/ -> libCommon.so
`Logger` (`log.txt`, `error.txt`), `ThreadBase`, `TimeUtils` (dates),
`Types.h` (`Candle`, `Account`, `Position`, `OrderResult`, `LimitOrder`, `Fill`,
`ExchangeRules`).

### SDK/ (headers only)
Boundary between engine and strategy: `CStrategyBase` (`GetInfo`, `LoadConfig`, `Init`,
`Run`, `Term`), `InitContext` / `StepContext`, `SXE_EXPORT_STRATEGY` macro.

### Strategies/Template/ -> libTemplate.so
Example strategy (moving-average crossover), configured by `Target/Config/Template.ini`.
Starting point for a new strategy.

### Strategies/\<Name\>/ -> lib\<Name\>.so
Every folder of `Strategies` with a `CMakeLists.txt` is built automatically; if it has a
`Test/` subfolder, its tests are built too.

### Tests/ (CTest tests)
`SxeTest.cmake` runs the tests; `Data/` fixed real data (BTCUSDT 1h and funding 2023-2024);
`Golden/` expected results; `TestStrategy/` test strategy and a library with a wrong
interface. Run them with `./comp_All.sh test`.

## Dependencies

```
Common <- Settings <- SDK <- DataFeed, Exchange <- Engine <- App
```

A strategy only sees SDK (plus Common and Settings), never the engine.
