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
#include <string.h>
#include "tlsb.h"
#include <errno.h>

#define MIN(x, y) (((x) < (y)) ? (x) : (y))

void
tlsb_dump_ofp_info(ofp_info_t *ofp_info)
{
    if (ofp_info->status) {
#define L(field) log_msg(#field ": %s", ofp_info->field)

        L(aircraft_icao);
        L(origin);
        L(destination);
        L(ci);
        L(max_passengers);
        L(fuel_plan_ramp);
        L(oew);
        L(pax_count);
        L(cargo);
        L(payload);
    } else {
        log_msg(ofp_info->errmsg);
    }
}

/* super simple xml extractor */
static int
get_element_text(char *xml, int start_ofs, int end_ofs, const char *tag, int *text_start, int *text_end)
{
    char stag[50], etag[50];
    sprintf(stag, "<%s>", tag);
    sprintf(etag, "</%s>", tag);

    char *s = strstr(xml + start_ofs, stag);
    if (NULL == s)
        return 0;

    s += strlen(stag);

    /* don't run over end_ofs */
    int c = xml[end_ofs];
    xml[end_ofs] = '\0';
    char *e = strstr(s, etag);
    xml[end_ofs] = c;

    if (NULL == e)
        return 0;

    *text_start = s - xml;
    *text_end = e - xml;
    return 1;
}

#define POSITION(tag) \
get_element_text(ofp, 0, ofp_len, tag, &out_s, &out_e)

#define EXTRACT(tag, field) \
do { \
    int s, e; \
    if (get_element_text(ofp, out_s, out_e, tag, &s, &e)) { \
        strncpy(ofp_info->field, ofp + s, MIN(sizeof(ofp_info->field), e - s)); \
    } \
} while (0)

int
tlsb_ofp_get_parse(const char *pilot_id, ofp_info_t *ofp_info)
{
    char *ofp;
    memset(ofp_info, 0, sizeof(*ofp_info));
    int ofp_len;


#if 1
    char url[80];
    sprintf(url, "/api/xml.fetcher.php?userid=%s", pilot_id);
    // log_msg(url);
    //FILE *f = fopen("tlsb_ofp.xml", "w+");
    FILE *f = tmpfile();
    if (NULL == f) {
        log_msg("Can't open");
        return 0;
    }

    int res = tlsb_http_get(url, f, &ofp_len);
#else
    FILE *f = fopen("tlsb_ofp.xml", "r");
    int res = (f != NULL);
#endif

   if (0 == res) {
        strcpy(ofp_info->status, "Network error");
        fclose(f);
        return 0;
    }

    log_msg("got ofp %d bytes", ofp_len);
    rewind(f);

    if (NULL == (ofp = malloc(ofp_len+1))) {    /* + space for a terminating 0 */
        log_msg("can't malloc OFP xml buffer");
        fclose(f);
        return 0;
    }

    ofp_len = fread(ofp, 1, ofp_len, f);
    fclose(f);
    res = 1;
    ofp[ofp_len] = '\0';

    int out_s, out_e;
    if (POSITION("fetch")) {
        EXTRACT("status", status);
        if (strcmp(ofp_info->status, "Success")) {
            goto out;
        }
    }

    if (POSITION("aircraft")) {
        EXTRACT("icaocode", aircraft_icao);
        EXTRACT("max_passengers", max_passengers);
    }

    if (POSITION("fuel")) {
        EXTRACT("plan_ramp", fuel_plan_ramp);
    }

    if (POSITION("origin")) {
        EXTRACT("icao_code", origin);
        EXTRACT("plan_rwy", origin_rwy);
    }

    if (POSITION("destination")) {
        EXTRACT("icao_code", destination);
        EXTRACT("plan_rwy", destination_rwy);
     }

    if (POSITION("general")) {
        EXTRACT("costindex", ci);
        EXTRACT("initial_altitude", altitude);
        EXTRACT("route", route);
    }

    if (POSITION("weights")) {
        EXTRACT("oew", oew);
        EXTRACT("pax_count", pax_count);
        EXTRACT("cargo", cargo);
        EXTRACT("payload", payload);
    }

out:
    free(ofp);
    return 1;
}
