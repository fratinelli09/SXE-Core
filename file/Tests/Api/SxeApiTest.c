#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "sxe.h"

static int g_nFailed = 0;

#define CHECK(cond, what) \
	do { if(!(cond)) { printf("FAIL: %s (line %d)\n", what, __LINE__); g_nFailed++; } else printf("ok:   %s\n", what); } while(0)

static const char* g_pszConfig;
static const char* g_pszWork;

typedef struct Counters
{
	int     nLog;
	int     nProgress;
	int     nCycles;
	int     bProgressOk;
	int64_t nLastStep;
	int     bStopAtFirst;
	sxe_engine* pEngine;
} Counters;

static void OnLog(void* user, sxe_log_level level, const char* message)
{
	Counters* c = (Counters*)user;
	(void)level;
	if(message != NULL && message[0] != '\0')
		c->nLog++;
}

static void OnProgress(void* user, const sxe_progress* p)
{
	Counters* c = (Counters*)user;
	if(p->step <= c->nLastStep || p->drawdown_pct > 0 || p->equity <= 0 || p->time <= 0)
		c->bProgressOk = 0;
	c->nLastStep = p->step;
	c->nProgress++;
	if(c->bStopAtFirst)
		sxe_engine_stop(c->pEngine);
}

static void OnCycle(void* user, const sxe_cycle* cycle)
{
	Counters* c = (Counters*)user;
	if(cycle->closed_by != NULL && cycle->close_time >= cycle->open_time)
		c->nCycles++;
}

static int FileExists(const char* pszPath)
{
	struct stat st;
	return stat(pszPath, &st) == 0;
}

static void SetFolders(sxe_engine* e, const char* pszName)
{
	char buf[1024];
	snprintf(buf, sizeof(buf), "%s/%s/Log", g_pszWork, pszName);
	sxe_engine_set(e, "PATH", "log", buf);
	snprintf(buf, sizeof(buf), "%s/%s/Backup", g_pszWork, pszName);
	sxe_engine_set(e, "PATH", "backup", buf);
	snprintf(buf, sizeof(buf), "%s/%s/Log/last_row.txt", g_pszWork, pszName);
	sxe_engine_set(e, "SIMULATION", "state_file", buf);
	snprintf(buf, sizeof(buf), "%s/%s/Log/Set_Close_log.txt", g_pszWork, pszName);
	sxe_engine_set(e, "SIMULATION", "order_log", buf);
}

static sxe_engine* NewEngine(const char* pszName)
{
	sxe_engine* e = sxe_engine_create();
	if(e == NULL || sxe_engine_load_config(e, g_pszConfig) != SXE_OK)
	{
		printf("FAIL: engine %s not created: %s\n", pszName, e ? sxe_engine_error(e) : "");
		exit(1);
	}
	SetFolders(e, pszName);
	return e;
}

static int SameResult(const sxe_result* a, const sxe_result* b)
{
	return a->final_equity == b->final_equity && a->max_drawdown_pct == b->max_drawdown_pct &&
	       a->cycles == b->cycles && a->wins == b->wins && a->liquidations == b->liquidations &&
	       a->orders == b->orders && a->rejected == b->rejected && a->fees == b->fees &&
	       a->funding == b->funding && a->worst_cycle_drawdown_pct == b->worst_cycle_drawdown_pct &&
	       a->max_leverage == b->max_leverage && a->steps == b->steps && a->open_at_end == b->open_at_end &&
	       strcmp(a->reason, b->reason) == 0;
}

int main(int argc, char* argv[])
{
	sxe_engine* full;
	sxe_engine* e;
	sxe_result  fullResult;
	Counters    c;
	char        buf[1024];
	sxe_status  status;

	if(argc < 7)
	{
		fprintf(stderr, "usage: SxeApiTest <EngineConfig.ini> <work folder> <strategy folder> <strategy library> "
		                "<strategy config> <data folder>\n");
		return 1;
	}
	g_pszConfig = argv[1];
	g_pszWork   = argv[2];

	CHECK(sxe_api_version() == SXE_C_API_VERSION, "interface version of the library");
	CHECK(sxe_version() != NULL && strlen(sxe_version()) > 0, "version of SXE Core");

	memset(&c, 0, sizeof(c));
	c.bProgressOk = 1;
	full = NewEngine("full");
	sxe_engine_on_log(full, OnLog, &c);
	sxe_engine_on_progress(full, OnProgress, &c);
	sxe_engine_on_cycle(full, OnCycle, &c);
	CHECK(sxe_engine_result(full)->finished == 0, "result not finished before the run");
	status = sxe_engine_run(full);
	if(status != SXE_OK)
		printf("%s\n", sxe_engine_error(full));
	CHECK(status == SXE_OK, "full run");
	fullResult = *sxe_engine_result(full);
	printf("      %s: equity %.2f (%+.2f%%), cycles %lld, liquidations %lld, steps %lld\n", fullResult.reason,
	       fullResult.final_equity, fullResult.return_pct, (long long)fullResult.cycles,
	       (long long)fullResult.liquidations, (long long)fullResult.steps);
	CHECK(fullResult.finished == 1 && fullResult.error == 0, "result finished without error");
	CHECK(c.nLog > 0, "log callback called");
	CHECK(c.nProgress == (int)(fullResult.steps / 1000) && c.bProgressOk, "progress callback every 1000 steps, values consistent");
	CHECK(c.nCycles == fullResult.cycles + fullResult.liquidations + fullResult.open_at_end,
	      "cycle callback for every closed cycle, liquidation and open cycle at the end");
	snprintf(buf, sizeof(buf), "%s/full/Backup/total_result.txt", g_pszWork);
	CHECK(FileExists(buf), "full run writes total_result.txt");
	CHECK(sxe_engine_set(full, "SIMULATION", "initial_capital", "1") == SXE_ERR_STATE, "set after the run refused");
	CHECK(sxe_engine_run(full) == SXE_ERR_STATE, "second run of the same engine refused");

	e = NewEngine("light");
	sxe_engine_set_option(e, SXE_OPT_ARCHIVE, 0);
	sxe_engine_set_option(e, SXE_OPT_STATE_FILE, 0);
	sxe_engine_set_option(e, SXE_OPT_ORDER_LOG, 0);
	sxe_engine_set_option(e, SXE_OPT_LOG_FILES, 0);
	CHECK(sxe_engine_run(e) == SXE_OK && SameResult(&fullResult, sxe_engine_result(e)), "run without files: same numbers");
	snprintf(buf, sizeof(buf), "%s/light/Backup/total_result.txt", g_pszWork);
	CHECK(!FileExists(buf), "run without files: no total_result.txt");
	snprintf(buf, sizeof(buf), "%s/light/Log/log.txt", g_pszWork);
	CHECK(!FileExists(buf), "run without files: no log.txt");
	sxe_engine_destroy(e);

	e = NewEngine("capital");
	sxe_engine_set_option(e, SXE_OPT_ARCHIVE, 0);
	sxe_engine_set(e, "simulation", "INITIAL_CAPITAL", "10000");
	CHECK(sxe_engine_run(e) == SXE_OK && sxe_engine_result(e)->initial_capital == 10000 &&
	      sxe_engine_result(e)->final_equity != fullResult.final_equity, "key changed after load_config (names not case sensitive)");
	sxe_engine_destroy(e);

	memset(&c, 0, sizeof(c));
	c.bProgressOk  = 1;
	c.bStopAtFirst = 1;
	e = NewEngine("stop");
	c.pEngine = e;
	sxe_engine_on_progress(e, OnProgress, &c);
	status = sxe_engine_run(e);
	CHECK(status == SXE_OK && sxe_engine_result(e)->finished && !sxe_engine_result(e)->error &&
	      strstr(sxe_engine_result(e)->reason, "stop") != NULL && sxe_engine_result(e)->steps < 1100,
	      "stop from a callback: the run closes normally at the next step");
	sxe_engine_destroy(e);

	e = sxe_engine_create();
	sxe_engine_set_option(e, SXE_OPT_LOG_FILES, 0);
	sxe_engine_set(e, "ENGINE", "mode", "SIM");
	sxe_engine_set(e, "ENGINE", "symbol", "BTCUSDT");
	sxe_engine_set(e, "STRATEGY", "folder", argv[3]);
	sxe_engine_set(e, "STRATEGY", "library", argv[4]);
	sxe_engine_set(e, "STRATEGY", "config", argv[5]);
	sxe_engine_set(e, "PATH", "data", argv[6]);
	sxe_engine_set(e, "EXCHANGE", "maintenance_tiers", "1000000000000000:0.004");
	sxe_engine_set(e, "SIMULATION", "data_file", "BTCUSDT_1h_2023_2024.csv");
	sxe_engine_set(e, "SIMULATION", "funding_file", "BTCUSDT_funding_2023_2024.csv");
	sxe_engine_set(e, "SIMULATION", "initial_capital", "5000");
	sxe_engine_set(e, "SIMULATION", "tick_step", "0.002");
	sxe_engine_set(e, "LOG", "console", "0");
	SetFolders(e, "memory");
	status = sxe_engine_run(e);
	if(status != SXE_OK)
		printf("%s\n", sxe_engine_error(e));
	CHECK(status == SXE_OK && SameResult(&fullResult, sxe_engine_result(e)), "configuration only through sxe_engine_set: same numbers");
	sxe_engine_destroy(e);

	CHECK(sxe_engine_set(NULL, "ENGINE", "mode", "SIM") == SXE_ERR_ARGUMENT, "null engine refused");
	CHECK(sxe_engine_result(NULL) == NULL, "result of a null engine is NULL");

	e = sxe_engine_create();
	CHECK(sxe_engine_load_config(e, "/nonexistent/EngineConfig.ini") == SXE_ERR_CONFIG &&
	      strstr(sxe_engine_error(e), "not found") != NULL, "missing configuration file");
	CHECK(sxe_engine_set_option(e, (sxe_option)999, 1) == SXE_ERR_ARGUMENT, "unknown option refused");
	CHECK(sxe_engine_set_option(e, SXE_OPT_ARCHIVE, 2) == SXE_ERR_ARGUMENT, "option value out of range refused");
	CHECK(sxe_engine_set(e, "ENGINE", "symbol", "BTC\nUSDT") == SXE_ERR_ARGUMENT, "value on more lines refused");
	CHECK(sxe_engine_set(e, "ENGINE]", "symbol", "BTCUSDT") == SXE_ERR_ARGUMENT, "section name not valid refused");
	CHECK(sxe_engine_set_base_dir(e, "/nonexistent") == SXE_ERR_ARGUMENT, "missing base folder refused");
	sxe_engine_destroy(e);

	e = NewEngine("long_line");
	{
		static char szLong[5000];
		memset(szLong, 'x', sizeof(szLong) - 1);
		szLong[sizeof(szLong) - 1] = '\0';
		sxe_engine_set(e, "PATH", "data", szLong);
	}
	CHECK(sxe_engine_setup(e) == SXE_ERR_CONFIG && strstr(sxe_engine_error(e), "too long") != NULL,
	      "value longer than an INI line: reported as too long");
	sxe_engine_destroy(e);

	e = NewEngine("bad_value");
	sxe_engine_set(e, "EXCHANGE", "margin_mode", "sideways");
	sxe_engine_set(e, "SIMULATION", "initial_capital", "-5");
	status = sxe_engine_setup(e);
	CHECK(status == SXE_ERR_CONFIG && strstr(sxe_engine_error(e), "margin_mode") != NULL &&
	      strstr(sxe_engine_error(e), "initial_capital") != NULL, "values not valid: every error reported");
	CHECK(sxe_engine_run(e) == SXE_ERR_STATE, "run after a failed setup refused");
	sxe_engine_destroy(e);

	e = NewEngine("missing_strategy");
	sxe_engine_set(e, "STRATEGY", "library", "libMissing.so");
	CHECK(sxe_engine_run(e) == SXE_ERR_CONFIG && strstr(sxe_engine_error(e), "libMissing.so not found") != NULL,
	      "missing strategy reported");
	sxe_engine_destroy(e);

	e = NewEngine("not_strategy");
	sxe_engine_set(e, "STRATEGY", "library", "OldApiStrategy");
	CHECK(sxe_engine_setup(e) == SXE_ERR_SETUP && strstr(sxe_engine_error(e), "interface version") != NULL,
	      "strategy built for another interface version: setup error with the reason");
	sxe_engine_destroy(e);

	sxe_engine_destroy(full);

	printf(g_nFailed == 0 ? "OK: all checks passed\n" : "ERROR: %d checks failed\n", g_nFailed);
	return g_nFailed == 0 ? 0 : 1;
}
