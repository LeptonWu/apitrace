#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>

#include "patch-symbolic.h"

int main(int argc, char *argv[])
{
	if (argc != 3) {
		fprintf(stderr, "Usage: %s <lib> <dir>\n", argv[0]);
		return -1;
	}
	char buf[4096];
	const char *ret = patch_symbolic(argv[1], argv[2], buf, sizeof(buf));
	if (ret == argv[1])
		fprintf(stderr, "Already linked with --BSymbolic\n");
	else
		printf("Modified to %s\n", ret);
	return 0;
}
