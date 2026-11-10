/* Callbacks borrow diagnostic messages; copies must survive later diagnostics. */
#include <stdio.h>
#include <string.h>
#include "smi.h"
#include "error.h"

static int calls;
static int failed;
static char copied[64];

static void collect(char *path, int line, int severity, char *message, char *tag)
{
    if (severity != 1 || strcmp(tag, "internal-other") ||
        strcmp(message, "retained diagnostic") || (path ? line != 17 : line != 0))
        failed = 1;
    snprintf(copied, sizeof(copied), "%s", message);
    ++calls;
}

int main(void)
{
    Parser parser = {0};
    char path[] = "diagnostic-fixture";
    parser.path = path;
    parser.flags = SMI_FLAG_ERRORS | SMI_FLAG_RECURSIVE;
    if (smiInit("diagnostic-lifetime") < 0) return 1;
    smiSetErrorLevel(9);
    smiSetErrorHandler(collect);
    for (int i = 0; i < 10000; ++i) {
        smiPrintError(NULL, ERR_OTHER_ERROR, "retained diagnostic");
        smiPrintErrorAtLine(&parser, ERR_OTHER_ERROR, 17, "retained diagnostic");
    }
    smiExit();
    if (failed || calls != 20000 || strcmp(copied, "retained diagnostic")) {
        fprintf(stderr, "Diagnostic callbacks changed or copies did not survive\n");
        return 1;
    }
    return 0;
}
