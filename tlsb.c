/*
MIT License

Copyright (c) 2019, 2021 Holger Teutsch

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
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <time.h>
#include <stdarg.h>
#include <unistd.h>
#include <errno.h>

#include "XPLMPlugin.h"
#include "XPLMPlanes.h"
#include "XPLMDisplay.h"
#include "XPLMGraphics.h"
#include "XPLMDataAccess.h"
#include "XPLMUtilities.h"
#include "XPLMProcessing.h"
#include "XPLMMenus.h"
#include "XPWidgets.h"
#include "XPStandardWidgets.h"

#include "tlsb.h"

#define UNUSED(x) (void)(x)

#define VERSION "1.00b6+"

static char xpdir[512];
static const char *psep;
static char fms_path[512];

char tlsb_tmp_fn[512];

static XPLMMenuID tlsb_menu;

#define MSG_GET_OFP (xpMsg_UserStart + 1)
static XPWidgetID getofp_widget, display_widget, getofp_btn,
                  status_line, xfer_load_btn;
static XPWidgetID conf_widget, pilot_id_input, conf_ok_btn,
                  conf_downl_pdf_btn, conf_downl_pdf_path, conf_downl_pdf_paste_btn, conf_downl_fpl_btn;

typedef struct _widget_ctx
{
    XPWidgetID widget;
    int in_vr;          /* currently in vr */
    int l, t, w, h;     /* last geometry before bringing into vr */
} widget_ctx_t;

static widget_ctx_t getofp_widget_ctx, conf_widget_ctx;

static ofp_info_t ofp_info;

static XPLMDataRef no_pax_dr, pax_distrib_dr, aft_cargo_dr, fwd_cargo_dr,
                   write_fob_dr, vr_enabled_dr;
static XPLMCommandRef set_weight_cmdr;  // ToLiss commands
static XPLMCommandRef open_cmdr;

static int dr_mapped;
static int error_disabled;

static char pref_path[512];
static int tla3xx_detected;
static char pilot_id[20];
static int flag_download_fms, flag_download_pdf;
static char pdf_download_dir[200];
static char acf_file[256];

static char msg_line_1[100];
static char msg_line_2[100];


static void
map_datarefs()
{
    if (dr_mapped)
        return;

    dr_mapped = 1;

    vr_enabled_dr = XPLMFindDataRef("sim/graphics/VR/enabled");

    if (NULL == (no_pax_dr = XPLMFindDataRef("AirbusFBW/NoPax"))) goto err;
    if (NULL == (pax_distrib_dr = XPLMFindDataRef("AirbusFBW/PaxDistrib"))) goto err;
    if (NULL == (aft_cargo_dr = XPLMFindDataRef("AirbusFBW/AftCargo")))goto err;
    if (NULL == (fwd_cargo_dr = XPLMFindDataRef("AirbusFBW/FwdCargo"))) goto err;
    if (NULL == (write_fob_dr = XPLMFindDataRef("AirbusFBW/WriteFOB"))) goto err;
    if (NULL == (set_weight_cmdr = XPLMFindCommand("AirbusFBW/SetWeightAndCG"))) goto err;
    return;

err:
    error_disabled = 1;
    log_msg("Can't map all datarefs, disabled");
}

static void
xfer_load_data()
{
    if (!ofp_info.valid)
        return;

    map_datarefs();
    if (error_disabled)
        return;

    log_msg("Xfer load data to ISCS");
    XPLMSetDatai(no_pax_dr, atoi(ofp_info.pax_count));
    XPLMSetDataf(pax_distrib_dr, 0.5);
    XPLMSetDataf(write_fob_dr, atof(ofp_info.fuel_plan_ramp));
    float cargo = 0.5 * atof(ofp_info.cargo);
    XPLMSetDataf(fwd_cargo_dr, cargo);
    XPLMSetDataf(aft_cargo_dr, cargo);
    XPLMCommandOnce(set_weight_cmdr);
}

static void
save_pref()
{
    FILE *f = fopen(pref_path, "wb");
    if (NULL == f)
        return;

    fputs(pilot_id, f); putc('\n', f);
    putc((flag_download_pdf ? '1' : '0'), f); fputs(pdf_download_dir, f); putc('\n', f);
    putc((flag_download_fms ? '1' : '0'), f); putc('\n', f);
    fclose(f);
}


static void
load_pref()
{
    char c;
    FILE *f  = fopen(pref_path, "rb");
    if (NULL == f)
        return;

    fgets(pilot_id, sizeof(pilot_id), f);
    int len = strlen(pilot_id);
    if ('\n' == pilot_id[len - 1]) pilot_id[len - 1] = '\0';

    if (EOF == (c = fgetc(f))) goto out;
    flag_download_pdf = (c == '1' ? 1 : 0);

    if (NULL == fgets(pdf_download_dir, sizeof(pdf_download_dir), f)) goto out;
    len = strlen(pdf_download_dir);
    if ('\n' == pdf_download_dir[len - 1]) pdf_download_dir[len - 1] = '\0';

    if (EOF == (c = fgetc(f))) goto out;
    flag_download_fms = (c == '1' ? 1 : 0);

  out:
    fclose(f);
}

static void
download_pdf()
{
    char URL[300], fn[500];
    FILE *f = NULL;

    snprintf(URL, sizeof(URL), "%s%s", ofp_info.sb_path, ofp_info.sb_pdf_link);
    log_msg("URL '%s'", URL);
    snprintf(fn, sizeof(fn), "%s%ssb_ofp.pdf", pdf_download_dir, psep);

    if (NULL == (f = fopen(fn, "wb"))) {
        log_msg("Can't create file '%s'", fn);
        goto err_out;
    }

    if (0 == tlsb_http_get(URL, f, NULL)) {
        log_msg("Can't download '%s'", URL);
        goto err_out;
    }

    snprintf(msg_line_1, sizeof(msg_line_1), "OFP pdf in '%s'", fn);

  err_out:
    if (f) fclose(f);
}

static void
download_fms()
{
    char URL[300], fn[500];
    FILE *f = NULL;

    snprintf(URL, sizeof(URL), "%s%s", ofp_info.sb_path, ofp_info.sb_fms_link);
    log_msg("URL '%s'", URL);
    snprintf(fn, sizeof(fn), "%s%s%s%s19.fms", fms_path, psep, ofp_info.origin, ofp_info.destination);

    if (NULL == (f = fopen(fn, "wb"))) {
        log_msg("Can't create file '%s'", fn);
        goto err_out;
    }

    if (0 == tlsb_http_get(URL, f, NULL)) {
        log_msg("Can't download '%s'", URL);
        goto err_out;
    }

    snprintf(msg_line_2, sizeof(msg_line_2), "FMS plan: '%s%s19'", ofp_info.origin, ofp_info.destination);

  err_out:
    if (f) fclose(f);
}


static void
show_widget(widget_ctx_t *ctx)
{
    if (XPIsWidgetVisible(ctx->widget))
        return;

    XPShowWidget(ctx->widget);

    int in_vr = (NULL != vr_enabled_dr) && XPLMGetDatai(vr_enabled_dr);
    if (in_vr) {
        log_msg("VR mode detected");
        XPLMWindowID window =  XPGetWidgetUnderlyingWindow(ctx->widget);
        XPLMSetWindowPositioningMode(window, xplm_WindowVR, -1);
        ctx->in_vr = 1;
    } else {
        if (ctx->in_vr) {
            log_msg("widget now out of VR, map at (%d,%d)", ctx->l, ctx->t);
            XPLMWindowID window =  XPGetWidgetUnderlyingWindow(ctx->widget);
            XPLMSetWindowPositioningMode(window, xplm_WindowPositionFree, -1);

            /* A resize is necessary so it shows up on the main screen again */
            XPSetWidgetGeometry(ctx->widget, ctx->l, ctx->t, ctx->l + ctx->w, ctx->t - ctx->h);
            ctx->in_vr = 0;
        }
    }
}


static int
conf_widget_cb(XPWidgetMessage msg, XPWidgetID widget_id, intptr_t param1, intptr_t param2)
{
    if (msg == xpMessage_CloseButtonPushed) {
        XPHideWidget(widget_id);
        return 1;
    }

    if (error_disabled)
        return 1;

    if ((widget_id == conf_ok_btn) && (msg == xpMsg_PushButtonPressed)) {
        XPGetWidgetDescriptor(pilot_id_input, pilot_id, sizeof(pilot_id));
        XPGetWidgetDescriptor(conf_downl_pdf_path, pdf_download_dir, sizeof(pdf_download_dir));
        flag_download_pdf = XPGetWidgetProperty(conf_downl_pdf_btn, xpProperty_ButtonState, NULL);
        flag_download_fms = XPGetWidgetProperty(conf_downl_fpl_btn, xpProperty_ButtonState, NULL);
        save_pref();
        XPHideWidget(conf_widget);
        return 1;
    }

   if ((widget_id == conf_downl_pdf_paste_btn) && (msg == xpMsg_PushButtonPressed)) {
       char tmp[sizeof (pdf_download_dir)];
        if (get_clipboard(tmp, sizeof(pdf_download_dir))) {
            XPSetWidgetDescriptor(conf_downl_pdf_path, tmp);
        }
        return 1;
    }

    return 0;
}

// return success == 1
static int
fetch_ofp(void)
{
    msg_line_1[0] = '\0';
    msg_line_2[0] = '\0';

    ofp_info.valid = 0;

    tlsb_ofp_get_parse(pilot_id, &ofp_info);
    tlsb_dump_ofp_info(&ofp_info);

    if (strcmp(ofp_info.status, "Success")) {
        XPSetWidgetDescriptor(status_line, ofp_info.status);
        return 0; // error
    }

    if ((0 == strcmp(ofp_info.aircraft_icao, acf_file))
        || ((0 == strcmp(ofp_info.aircraft_icao, "A21N")) && (0 == strcmp(acf_file, "A321")))) {
        time_t tg = atol(ofp_info.time_generated);
        struct tm tm;
    #ifdef WINDOWS
        gmtime_s(&tm, &tg);
    #else
        gmtime_r(&tg, &tm);
    #endif
        char line[100];
        /* strftime does not work for whatever reasons */
        sprintf(line, "OFP generated at %4d-%02d-%02d %02d:%02d:%02d UTC",
                       tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                       tm.tm_hour, tm.tm_min, tm.tm_sec);

        XPSetWidgetDescriptor(status_line, line);
        ofp_info.valid = 1;
        snprintf(ofp_info.altitude, sizeof(ofp_info.altitude), "%d", atoi(ofp_info.altitude) / 100);

        if (flag_download_pdf)
            download_pdf();

        if (flag_download_fms)
            download_fms();
        return 1;
        }

    char line[100];
    sprintf(line, "OFP is not for %s", acf_file);
    XPSetWidgetDescriptor(status_line, line);
    memset(&ofp_info, 0, sizeof(ofp_info));
    return 0;
}

static int
getofp_widget_cb(XPWidgetMessage msg, XPWidgetID widget_id, intptr_t param1, intptr_t param2)
{
    if (msg == xpMessage_CloseButtonPushed) {
        XPHideWidget(widget_id);
        return 1;
    }

    if (error_disabled)
        return 1;

    if ((widget_id == getofp_btn) && (msg == xpMsg_PushButtonPressed)) {
        XPSetWidgetDescriptor(status_line, "Fetching...");
        /* Send message to myself to get a draw cycle (draws button as selected) */
        XPSendMessageToWidget(status_line, MSG_GET_OFP, xpMode_UpChain, 0, 0);
        return 1;
    }

    /* self sent message: fetch OFP (lengthy) */
    if ((widget_id == getofp_widget) && (MSG_GET_OFP == msg)) {
        (void)fetch_ofp();
        return 1;
    }

    if ((widget_id == xfer_load_btn) && (msg == xpMsg_PushButtonPressed)) {
        xfer_load_data();
        return 1;
    }

    /* draw the embedded custom widget */
    if ((widget_id == display_widget) && (xpMsg_Draw == msg)) {
        static float label_color[] = { 0.0, 0.0, 0.0 };
        static float xfer_color[] = { 0.0, 0.5, 0.0 };
        static float bg_color[] = { 0.0, 0.3, 0.3 };

        int left, top, right, bottom;
        XPGetWidgetGeometry(display_widget, &left, &top, &right, &bottom);
        // log_msg("display_widget start %d %d %d %d", left, top, right, bottom);

        int left_col = left + 5;
        int right_col = left + 80;
        int y = top - 5;

#define DL(TXT) \
    y -= 15; \
    XPLMDrawString(label_color, left_col, y, TXT, NULL, xplmFont_Proportional)

#define DX(field) \
    XPLMDrawString(xfer_color, right_col, y, ofp_info.field, NULL, xplmFont_Basic)

        //DL("Pax:"); DF(right_col, max_passengers);
        // D(right_col, oew);
        DL("Pax:"); DX(pax_count);
        DL("Cargo:"); DX(cargo);
        DL("Fuel:"); DX(fuel_plan_ramp);
        // D(right_col, payload);

        y -= 30;
#define DF(left, field) \
    XPLMDrawString(bg_color, left, y, ofp_info.field, NULL, xplmFont_Basic)

        // D(aircraft_icao);
        DL("Departure:"); DF(right_col, origin); DF(right_col + 50, origin_rwy);
        DL("Destination"); DF(right_col, destination); DF(right_col + 50, destination_rwy);
        DL("Route:");

        /* break route to this # of chars */
#define ROUTE_BRK 50
        char *rptr = ofp_info.route;
        while (1) {
            int len = strlen(rptr);
            if (len <= ROUTE_BRK)
                break;

            /* find last blank < line length */
            char c = rptr[ROUTE_BRK];
            rptr[ROUTE_BRK] = '\0';
            char *cptr = strrchr(rptr, ' ');
            rptr[ROUTE_BRK] = c;

            if (NULL == cptr) {
                log_msg("Can't format route!");
                break;
            }

            /* write that fragment */
            c = *cptr;
            *cptr = '\0';
            XPLMDrawString(bg_color, right_col, y, rptr, NULL, xplmFont_Basic);
            y -= 15;
            *cptr = c;
            rptr = cptr + 1;    /* behind the blank */
        }

        XPLMDrawString(bg_color, right_col, y, rptr, NULL, xplmFont_Basic);


        DL("Trip time");
        if (ofp_info.est_time_enroute[0]) {
            char ttstr[10];
            int ttmin = (atoi(ofp_info.est_time_enroute) + 30) / 60;
            sprintf(ttstr, "%02d%02d", ttmin / 60, ttmin % 60);
            XPLMDrawString(bg_color, right_col, y, ttstr, NULL, xplmFont_Basic);
        }

        DL("CI"); DF(right_col, ci);
        DL("CRZ FL:"); DF(right_col, altitude);

        if (msg_line_1[0]) {
            y -= 15;
            XPLMDrawString(bg_color, left_col, y, msg_line_1, NULL, xplmFont_Proportional);
        }

        if (msg_line_2[0]) {
            y -= 15;
            XPLMDrawString(bg_color, left_col, y, msg_line_2, NULL, xplmFont_Proportional);
        }

        /* adjust height of window */
        y -= 10;

        int pleft, ptop, pright, pbottom;
        XPGetWidgetGeometry(getofp_widget, &pleft, &ptop, &pright, &pbottom);

        if (y != pbottom) {
            XPSetWidgetGeometry(getofp_widget, pleft, ptop, pright, y);
            getofp_widget_ctx.h = ptop - y;

            /* widgets are internally managed relative to the left lower corner.
               So if we resize a container we must shift all childs accordingly. */
            int delta = y - pbottom;
            int nchild = XPCountChildWidgets(getofp_widget);
            for (int i = 0; i < nchild; i++) {
                int cleft, ctop, cright, cbottom;
                XPWidgetID cw = XPGetNthChildWidget(getofp_widget, i);
                XPGetWidgetGeometry(cw, &cleft, &ctop, &cright, &cbottom);
                XPSetWidgetGeometry(cw, cleft, ctop - delta, cright, cbottom - delta);
            }
        }

		return 1;
	}

    return 0;
}

static void
create_widget()
{
    if (getofp_widget)
        return;

    int left = 200;
    int top = 800;
    int width = 450;
    int height = 300;

    getofp_widget_ctx.l = left;
    getofp_widget_ctx.t = top;
    getofp_widget_ctx.w = width;
    getofp_widget_ctx.h = height;

    getofp_widget = XPCreateWidget(left, top, left + width, top - height,
                                 0, "Toliss Simbrief Connector", 1, NULL, xpWidgetClass_MainWindow);
    getofp_widget_ctx.widget = getofp_widget;

    XPSetWidgetProperty(getofp_widget, xpProperty_MainWindowHasCloseBoxes, 1);
    XPAddWidgetCallback(getofp_widget, getofp_widget_cb);
    left += 5; top -= 25;

    int left1 = left + 10;
    getofp_btn = XPCreateWidget(left1, top, left1 + 60, top - 30,
                              1, "Fetch OFP", 0, getofp_widget, xpWidgetClass_Button);
    XPAddWidgetCallback(getofp_btn, getofp_widget_cb);

    top -= 25;
    status_line = XPCreateWidget(left1, top, left + width - 10, top - 20,
                              1, "", 0, getofp_widget, xpWidgetClass_Caption);

    top -= 20;
    display_widget = XPCreateCustomWidget(left + 10, top, left + width -20, top - height + 10,
                                           1, "", 0, getofp_widget, getofp_widget_cb);
    top -= 50;
    xfer_load_btn = XPCreateWidget(left + 10, top, left + 160, top - 30,
                              1, "Xfer Load data to ISCS", 0, getofp_widget, xpWidgetClass_Button);
    XPAddWidgetCallback(xfer_load_btn, getofp_widget_cb);
}

static void
menu_cb(void *menu_ref, void *item_ref)
{
    /* create gui */
    if (item_ref == &getofp_widget) {
        create_widget();
        show_widget(&getofp_widget_ctx);
        return;
    }

    if (item_ref == &conf_widget) {
        if (NULL == conf_widget) {
            int left = 250;
            int top = 780;
            int width = 500;
            int height = 180;

            conf_widget_ctx.l = left;
            conf_widget_ctx.t = top;
            conf_widget_ctx.w = width;
            conf_widget_ctx.h = height;

            conf_widget = XPCreateWidget(left, top, left + width, top - height,
                                         0, "Toliss Simbrief Connector / Configuration", 1, NULL, xpWidgetClass_MainWindow);
            conf_widget_ctx.widget = conf_widget;

            XPSetWidgetProperty(conf_widget, xpProperty_MainWindowHasCloseBoxes, 1);
            XPAddWidgetCallback(conf_widget, conf_widget_cb);
            left += 5; top -= 25;
            XPCreateWidget(left, top, left + width - 2 * 5, top - 15,
                           1, "Pilot Id", 0, conf_widget, xpWidgetClass_Caption);

            int left1 = left + 60;
            pilot_id_input = XPCreateWidget(left1, top, left1 +  50, top - 15,
                                            1, pilot_id, 0, conf_widget, xpWidgetClass_TextField);
            XPSetWidgetProperty(pilot_id_input, xpProperty_TextFieldType, xpTextEntryField);
            XPSetWidgetProperty(pilot_id_input, xpProperty_MaxCharacters, sizeof(pilot_id) -1);

            top -= 20;
            XPCreateWidget(left, top, left + width - 10, top - 20,
                                      1, "Download OFP pdf file to directory", 0, conf_widget, xpWidgetClass_Caption);
            top -= 20;
            conf_downl_pdf_btn = XPCreateWidget(left, top, left + 20, top - 20,
                                      1, "", 0, conf_widget, xpWidgetClass_Button);
            XPSetWidgetProperty(conf_downl_pdf_btn, xpProperty_ButtonType, xpRadioButton);
            XPSetWidgetProperty(conf_downl_pdf_btn, xpProperty_ButtonBehavior, xpButtonBehaviorCheckBox);

            int left2 = left + width - 70;
            conf_downl_pdf_path = XPCreateWidget(left1, top, left2, top - 15,
                                            1, "", 0, conf_widget, xpWidgetClass_TextField);
            XPSetWidgetProperty(conf_downl_pdf_path, xpProperty_TextFieldType, xpTextEntryField);
            XPSetWidgetProperty(conf_downl_pdf_path, xpProperty_MaxCharacters, sizeof(pdf_download_dir) -1);

            conf_downl_pdf_paste_btn = XPCreateWidget(left2 + 20 , top, left2 + 60, top - 20,
                                      1, "Paste", 0, conf_widget, xpWidgetClass_Button);
            XPAddWidgetCallback(conf_downl_pdf_paste_btn, conf_widget_cb);

            top -= 20;
            XPCreateWidget(left, top, left + width - 10, top - 20,
                                      1, "Download Flightplan", 0, conf_widget, xpWidgetClass_Caption);
            top -= 20;
            conf_downl_fpl_btn = XPCreateWidget(left, top, left + 20, top - 20,
                                      1, "", 0, conf_widget, xpWidgetClass_Button);
            XPSetWidgetProperty(conf_downl_fpl_btn, xpProperty_ButtonType, xpRadioButton);
            XPSetWidgetProperty(conf_downl_fpl_btn, xpProperty_ButtonBehavior, xpButtonBehaviorCheckBox);

            top -= 30;
            conf_ok_btn = XPCreateWidget(left + 10, top, left + 140, top - 30,
                                      1, "OK", 0, conf_widget, xpWidgetClass_Button);
            XPAddWidgetCallback(conf_ok_btn, conf_widget_cb);
        }

        XPSetWidgetDescriptor(pilot_id_input, pilot_id);
        XPSetWidgetDescriptor(conf_downl_pdf_path, pdf_download_dir);
        XPSetWidgetProperty(conf_downl_pdf_btn, xpProperty_ButtonState, flag_download_pdf);
        XPSetWidgetProperty(conf_downl_fpl_btn, xpProperty_ButtonState, flag_download_fms);
        show_widget(&conf_widget_ctx);
        return;
    }
}

// call back for open cmd
static int
open_cmd_cb(XPLMCommandRef cmdr, XPLMCommandPhase phase, void *ref)
{
    UNUSED(ref);
    if (xplm_CommandBegin != phase)
        return 0;

    log_msg("open cmd called");
    create_widget();
    fetch_ofp();
    show_widget(&getofp_widget_ctx);
    return 0;
}

PLUGIN_API int
XPluginStart(char *out_name, char *out_sig, char *out_desc)
{
    /* Always use Unix-native paths on the Mac! */
    XPLMEnableFeature("XPLM_USE_NATIVE_PATHS", 1);
    XPLMEnableFeature("XPLM_USE_NATIVE_WIDGET_WINDOWS", 1);

    strcpy(out_name, "toliss_simbrief " VERSION);
    strcpy(out_sig, "tlsb-hotbso");
    strcpy(out_desc, "A plugin that connects simbrief to the ToLiss A319/A321");

    psep = XPLMGetDirectorySeparator();
    XPLMGetSystemPath(xpdir);
    snprintf(fms_path, sizeof(fms_path), "%s%sOutput%sFMS plans%s",
             xpdir, psep, psep, psep);

    snprintf(tlsb_tmp_fn, sizeof(tlsb_tmp_fn), "%s%sOutput%stlsb_download.tmp",
             xpdir, psep, psep);

    /* load preferences */
    XPLMGetPrefsPath(pref_path);
    XPLMExtractFileAndPath(pref_path);
    strcat(pref_path, psep);
    strcat(pref_path, "toliss_simbrief.prf");
    load_pref();
    return 1;
}


PLUGIN_API void
XPluginStop(void)
{
    save_pref();
}


PLUGIN_API void
XPluginDisable(void)
{
}


PLUGIN_API int
XPluginEnable(void)
{
    return 1;
}


PLUGIN_API void
XPluginReceiveMessage(XPLMPluginID in_from, long in_msg, void *in_param)
{
    UNUSED(in_from);

    switch (in_msg) {
        case XPLM_MSG_PLANE_LOADED:
            if (in_param == 0) {
                char path[512];

                XPLMGetNthAircraftModel(XPLM_USER_AIRCRAFT, acf_file, path);
                log_msg(acf_file);

                acf_file[4] = '\0';     /* be safe */
                for (int i = 0; i < 4; i++)
                    acf_file[i] = toupper(acf_file[i]);

                if ((0 == strcmp(acf_file, "A319")) || (0 == strcmp(acf_file, "A321"))) {
                    XPLMGetSystemPath(path);
                    char *s = path + strlen(path);

                    /* check for directory */
                    sprintf(s, "Resources%splugins%sToLissData%sSituations", psep, psep, psep);
                    if (0 != access(path, F_OK))
                        return;

                    log_msg("Detected ToLiss A319/A321");
                    tla3xx_detected = 1;

                    XPLMMenuID menu = XPLMFindPluginsMenu();
                    int sub_menu = XPLMAppendMenuItem(menu, "Simbrief Connector", NULL, 1);
                    tlsb_menu = XPLMCreateMenu("Simbrief Connector", menu, sub_menu, menu_cb, NULL);
                    XPLMAppendMenuItem(tlsb_menu, "Configure", &conf_widget, 0);
                    XPLMAppendMenuItem(tlsb_menu, "Fetch OFP", &getofp_widget, 0);

                    open_cmdr = XPLMCreateCommand("tlsb/open", "Open widget and fetch ofp data");
                    XPLMRegisterCommandHandler(open_cmdr, open_cmd_cb, 0, NULL);
               }
            }
        break;
    }
}
