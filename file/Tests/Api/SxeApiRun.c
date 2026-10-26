#include <stdio.h>

#include "sxe.h"

int main(int argc, char* argv[])
{
	sxe_engine* engine;
	sxe_status  status;

	if(argc < 2)
	{
		fprintf(stderr, "usage: SxeApiRun <EngineConfig.ini>\n");
		return 1;
	}

	engine = sxe_engine_create();
	if(engine == NULL)
		return 1;

	status = sxe_engine_load_config(engine, argv[1]);
	if(status == SXE_OK)
		status = sxe_engine_run(engine);

	if(status != SXE_OK)
		fprintf(stderr, "SxeApiRun: error %d\n%s\n", (int)status, sxe_engine_error(engine));
	else
		printf("%s: equity %.2f (%+.2f%%), cycles %lld\n", sxe_engine_result(engine)->reason,
		       sxe_engine_result(engine)->final_equity, sxe_engine_result(engine)->return_pct,
		       (long long)sxe_engine_result(engine)->cycles);

	sxe_engine_destroy(engine);
	return status == SXE_OK ? 0 : 1;
}
