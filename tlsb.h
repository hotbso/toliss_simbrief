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

#include <stdarg.h>

typedef struct _ofp_info
{
    char status[100];
    char errmsg[100];
    char aircraft_icao[10];
    char max_passengers[10];
    char fuel_plan_ramp[10];
    char origin[10];
    char destination[10];
    char ci[10];
    char altitude[10];
} ofp_info_t;

extern int tlsb_ofp_get(const char *userid, char *buffer, int buflen, int *retlen);
extern void log_msg(const char *fmt, ...);
extern int tlsb_ofp_get_parse(const char *pilot_id, ofp_info_t *ofp_info);
extern void tlsb_dump_ofp_info(ofp_info_t *ofp_info);