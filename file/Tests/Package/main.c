/* A program outside SXE Core, built with find_package(SXE): prints the version and runs an
   engine without configuration (the error proves that the engine libraries are loaded). */
#include <stdio.h>
#include "sxe.h"

int main(void)
{
	sxe_engine* e = sxe_engine_create();
	int         status;

	if(e == NULL)
		return 1;
	printf("SXE Core %s, C interface %d\n", sxe_version(), sxe_api_version());
	status = sxe_engine_run(e);
	printf("run without configuration: status %d (%s)\n", status, sxe_engine_error(e));
	sxe_engine_destroy(e);
	return status == SXE_ERR_CONFIG ? 0 : 1;
}
