/*
 * sxe.h - C interface of SXE Core
 *
 * Embeds the engine in another program (any language that can call C functions).
 * One engine = one run:
 *
 *     sxe_engine* e = sxe_engine_create();
 *     sxe_engine_load_config(e, "Target/Config/EngineConfig.ini");   (optional)
 *     sxe_engine_set(e, "SIMULATION", "initial_capital", "10000");    (optional)
 *     sxe_engine_on_cycle(e, my_cycle_callback, my_data);             (optional)
 *     sxe_engine_run(e);
 *     sxe_engine_result(e)->return_pct ...
 *     sxe_engine_destroy(e);
 *
 * For another run create a new engine. Several engines can run at the same time,
 * each in its own thread.
 *
 * Threads: an engine is used from one thread at a time; only sxe_engine_stop() may be
 * called from another thread. The callbacks are called in the thread that runs
 * sxe_engine_run(), and must not call sxe_ functions of the same engine except
 * sxe_engine_stop(). Do not destroy an engine while sxe_engine_run() is running.
 *
 * Files: the options below control the files of the engine; the strategy may still write
 * its own files in [PATH] log.
 *
 * Strings: UTF-8. The strings and structures returned by the library belong to it:
 * those passed to a callback are valid until the callback returns, the others until
 * the next call on the same engine or sxe_engine_destroy().
 *
 * Licence: PolyForm Noncommercial 1.0.0 (see LICENSE): commercial use needs the
 * permission of the author.
 */
#ifndef SXE_H
#define SXE_H

#include <stdint.h>

#if defined(_WIN32)
  #if defined(SXE_BUILD)
    #define SXE_API __declspec(dllexport)
  #else
    #define SXE_API __declspec(dllimport)
  #endif
#else
  #define SXE_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* version of this interface; sxe_api_version() gives the one of the loaded library */
#define SXE_C_API_VERSION 1

/* the *_MAX values only make every enum as large as an int: never use them */

/* ---------- result of the calls ---------- */
typedef enum sxe_status
{
    SXE_OK           = 0,
    SXE_ERR_ARGUMENT = 1,   /* null handle, unknown option, value not allowed */
    SXE_ERR_CONFIG   = 2,   /* configuration not valid: sxe_engine_error() lists every error */
    SXE_ERR_STATE    = 3,   /* call not allowed now: e.g. sxe_engine_set() after the setup */
    SXE_ERR_SETUP    = 4,   /* strategy not loaded, data not readable, Init() of the strategy failed */
    SXE_ERR_RUN      = 5,   /* the simulation ended with an error (sxe_result.reason) */
    SXE_ERR_INTERNAL = 6,   /* unexpected error inside the library */
    SXE_STATUS_MAX   = 0x7FFFFFFF
} sxe_status;

typedef enum sxe_log_level
{
    SXE_LOG_INFO    = 0,
    SXE_LOG_WARNING = 1,
    SXE_LOG_ERROR   = 2,
    SXE_LOG_MAX     = 0x7FFFFFFF
} sxe_log_level;

/* options that are not in EngineConfig.ini, [default] */
typedef enum sxe_option
{
    SXE_OPT_ARCHIVE        = 1,   /* [1] b<N> folders, result.txt and total_result.txt in [PATH] backup */
    SXE_OPT_STATE_FILE     = 2,   /* [1] state file, needed to resume a run ([SIMULATION] resume) */
    SXE_OPT_ORDER_LOG      = 3,   /* [1] order log */
    SXE_OPT_LOG_FILES      = 4,   /* [1] log.txt and error.txt in [PATH] log */
    SXE_OPT_CONSOLE        = 5,   /* [0] status line and warnings on the terminal; replaces [LOG] console */
    SXE_OPT_PROGRESS_EVERY = 6,   /* [1000] steps between two progress callbacks, 0 = never */
    SXE_OPT_MAX            = 0x7FFFFFFF
} sxe_option;

typedef struct sxe_engine sxe_engine;

/* ---------- data passed to the caller (read only) ---------- */

/* times: seconds since 1970-01-01 UTC; percentages: 12.5 = 12.5% */
typedef struct sxe_progress
{
    int64_t time;
    int64_t step;
    int64_t cycle;           /* cycle in progress, or the next one if no position is open */
    double  price;
    double  equity;
    double  drawdown_pct;    /* from the highest equity so far, <= 0 */
} sxe_progress;

/* a closed cycle: the same row as in total_result.txt */
typedef struct sxe_cycle
{
    int64_t     cycle;
    int64_t     open_time;
    int64_t     close_time;
    double      capital_start;
    double      capital_end;
    double      growth_pct;
    double      drawdown_pct;   /* worst equity in the cycle against capital_start, <= 0 */
    double      max_leverage;
    int64_t     buys;
    double      entry_price;
    double      avg_price;
    double      exit_price;
    double      fees;
    double      funding;
    const char* closed_by;      /* "strategy", "liquidation", "open at end of simulation" */
} sxe_cycle;

/* summary of the run, complete when finished = 1 */
typedef struct sxe_result
{
    int         finished;
    int         error;
    const char* reason;             /* why the simulation ended */
    int64_t     first_time;         /* first and last candle of the period */
    int64_t     last_time;
    double      initial_capital;
    double      final_equity;
    double      return_pct;
    double      max_drawdown_pct;   /* <= 0 */
    int64_t     cycles;             /* positions closed by the strategy */
    int64_t     wins;
    int64_t     liquidations;
    int64_t     orders;
    int64_t     rejected;
    double      fees;
    double      funding;
    double      worst_cycle_drawdown_pct;
    double      max_leverage;
    int         open_at_end;        /* position still open at the end */
    int64_t     underwater;         /* cycles with equity below zero: only with [SIMULATION] liquidation = 0 */
    int64_t     steps;
    double      seconds;
} sxe_result;

typedef void (*sxe_log_fn)(void* user, sxe_log_level level, const char* message);
typedef void (*sxe_progress_fn)(void* user, const sxe_progress* progress);
typedef void (*sxe_cycle_fn)(void* user, const sxe_cycle* cycle);

/* ---------- library ---------- */
SXE_API int         sxe_api_version(void);   /* SXE_C_API_VERSION the library was built with */
SXE_API const char* sxe_version(void);       /* version of SXE Core, e.g. "1.2.0" */

/* ---------- lifetime ---------- */
SXE_API sxe_engine* sxe_engine_create(void); /* NULL if out of memory */
SXE_API void        sxe_engine_destroy(sxe_engine* engine);

/* ---------- configuration, before the setup ----------
 * Sections and keys are those of EngineConfig.ini, with the same checks; names are not
 * case sensitive. Relative paths start from the base folder: by default the folder of
 * libSXE (Target/Bin, as for the SXE program).
 */
SXE_API sxe_status sxe_engine_set_base_dir(sxe_engine* engine, const char* dir);
/* adds the keys of an INI file; keys already set are replaced */
SXE_API sxe_status sxe_engine_load_config(sxe_engine* engine, const char* ini_path);
/* sets one key; value as it would be written in the INI file, on one line */
SXE_API sxe_status sxe_engine_set(sxe_engine* engine, const char* section, const char* key, const char* value);
SXE_API sxe_status sxe_engine_set_option(sxe_engine* engine, sxe_option option, int64_t value);

/* NULL fn removes the callback */
SXE_API sxe_status sxe_engine_on_log(sxe_engine* engine, sxe_log_fn fn, void* user);
SXE_API sxe_status sxe_engine_on_progress(sxe_engine* engine, sxe_progress_fn fn, void* user);
SXE_API sxe_status sxe_engine_on_cycle(sxe_engine* engine, sxe_cycle_fn fn, void* user);

/* ---------- run ---------- */
/* checks the configuration, creates the output folders, loads strategy and data */
SXE_API sxe_status        sxe_engine_setup(sxe_engine* engine);
/* runs the whole simulation in the calling thread; calls sxe_engine_setup() if not done */
SXE_API sxe_status        sxe_engine_run(sxe_engine* engine);
/* from any thread: the engine stops at the next step and closes the run normally */
SXE_API void              sxe_engine_stop(sxe_engine* engine);
/* never NULL for a valid engine; finished = 0 until the run is over */
SXE_API const sxe_result* sxe_engine_result(const sxe_engine* engine);
/* errors of the last failed call, one per line; "" if none */
SXE_API const char*       sxe_engine_error(const sxe_engine* engine);

#ifdef __cplusplus
}
#endif

#endif
