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
#include <errno.h>
#include <unistd.h>

#include "tlsb.h"

#define MIN(x, y) (((x) < (y)) ? (x) : (y))

#ifdef WIN
#define unlink(f) _unlink(f)
#endif

void
tlsb_dump_ofp_info(ofp_info_t *ofp_info)
{
    if (0 == strcmp(ofp_info->status, "Success")) {
#define L(field) log_msg(#field ": %s", ofp_info->field)
        L(units);
        L(icao_airline);
        L(flight_number);
        L(aircraft_icao);
        L(origin);
        L(destination);
        L(alternate);
        L(ci);
        L(tropopause);
        L(isa_dev);
        L(wind_component);
        L(route);
        L(alt_route);
        L(max_passengers);
        L(fuel_plan_ramp);
        L(oew);
        L(pax_count);
        L(freight);
        L(payload);
        L(est_time_enroute);
        L(sb_path);
        L(sb_pdf_link);
        L(sb_fms_link);
        L(time_generated);
    } else {
        log_msg(ofp_info->status);
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
    char *ofp = NULL;
    FILE *f = NULL;

    memset(ofp_info, 0, sizeof(*ofp_info));
    int ofp_len;

    char url[100];
    sprintf(url, "https://www.simbrief.com/api/xml.fetcher.php?userid=%s", pilot_id);
    // log_msg(url);

    f = fopen(tlsb_tmp_fn, "wb+");
    // is unrealiable on windows FILE *f = tmpfile();

    if (NULL == f) {
        log_msg("Can't create temporary file");
        return 0;
    }

    int res = tlsb_http_get(url, f, &ofp_len, 10);

    if (0 == res) {
        strcpy(ofp_info->status, "Network error");
        goto out;
    }

    log_msg("got ofp %d bytes", ofp_len);
    rewind(f);

    if (NULL == (ofp = malloc(ofp_len+1))) {    /* + space for a terminating 0 */
        log_msg("can't malloc OFP xml buffer");
        res = 0;
        goto out;
    }

    ofp_len = fread(ofp, 1, ofp_len, f);
    res = 1;
    ofp[ofp_len] = '\0';

    int out_s, out_e;
    if (POSITION("fetch")) {
        EXTRACT("status", status);
        if (strcmp(ofp_info->status, "Success")) {
            goto out;
        }
    }

    if (POSITION("params")) {
        EXTRACT("time_generated", time_generated);
        EXTRACT("units", units);
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
        EXTRACT("icao_airline", icao_airline);
        EXTRACT("flight_number", flight_number);
        EXTRACT("costindex", ci);
        EXTRACT("initial_altitude", altitude);
        EXTRACT("avg_tropopause", tropopause);
        EXTRACT("avg_wind_comp", wind_component);
        EXTRACT("avg_temp_dev", isa_dev);
        EXTRACT("route", route);
    }

     if (POSITION("alternate")) {
        EXTRACT("icao_code", alternate);
        EXTRACT("route", alt_route);
    }

   if (POSITION("weights")) {
        EXTRACT("oew", oew);
        EXTRACT("pax_count", pax_count);
        EXTRACT("freight_added", freight);
        EXTRACT("payload", payload);
    }

    if (POSITION("times")) {
        EXTRACT("est_time_enroute", est_time_enroute);
    }

    if (POSITION("fms_downloads")) {
        EXTRACT("directory", sb_path);
    }

    /* beware: these go directly into nested structures that fortunately
       appear only once */
    if (POSITION("pdf")) {
        EXTRACT("link", sb_pdf_link);
    }

    if (POSITION("xpe")) {
        EXTRACT("link", sb_fms_link);
    }

out:
    if (ofp) free(ofp);
    if (f) fclose(f);
    unlink(tlsb_tmp_fn);   /* unchecked */
    return res;
}
