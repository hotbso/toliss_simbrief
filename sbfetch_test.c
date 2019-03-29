#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <malloc.h>
#include <unistd.h>

#include "tlsb.h"

#define XPLMDebugString(x) fputs(x, stdout); fflush(stdout);

void
log_msg(const char *fmt, ...)
{
    char line[1024];

    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line) - 3, fmt, ap);
    strcat(line, "\n");
    XPLMDebugString("tlsb: ");
    XPLMDebugString(line);
    va_end(ap);
}

int main(int argc, char** argv)
{
    if (argc < 2) {
        log_msg("missing argument");
        exit(1);
    }
    
    int buflen = 1024 * 1014;
    char *ofp = malloc(buflen);
    if (NULL == ofp) {
        log_msg("Can't malloc");
        exit(1);
    }
    
    int ret_len;
    int res = tlsb_ofp_get(argv[1], ofp, buflen, &ret_len);
    if (res) {
        write(1, ofp, ret_len);
        exit(0);
    }
}