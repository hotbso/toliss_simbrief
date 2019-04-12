/*
MIT License

Copyright (c) 2019 Holger Teutsch

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <malloc.h>
#include <unistd.h>
#include <time.h>
#include "tlsb.h"

char tlsb_tmp_fn[] = "xml.tmp";

int
main(int argc, char** argv)
{
    if (argc < 2) {
        log_msg("missing argument");
        exit(1);
    }

    ofp_info_t ofp_info;
    tlsb_ofp_get_parse(argv[1], &ofp_info);
    tlsb_dump_ofp_info(&ofp_info);
    time_t tg = atol(ofp_info.time_generated);
    log_msg("tg %u", tg);
    struct tm tm;
    gmtime_s(&tm, &tg);
    char line[100];    
    sprintf(line, "OFP generated at %4d-%02d-%02d %02d:%02d:%02d UTC",
                   tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                   tm.tm_hour, tm.tm_min, tm.tm_sec);
    log_msg("'%s'", line);
    
exit(0);
}