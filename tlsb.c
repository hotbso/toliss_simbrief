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
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <time.h>
#include <stdarg.h>
#include <dirent.h>
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>

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

#define VERSION "1.00"

static char xpdir[512];
static const char *psep;

#define MENU_CONFIGURE 1
#define MENU_GET_OFP 2
static XPLMMenuID tlsb_menu;

#define MSG_GET_OFP (xpMsg_UserStart + 1)
static XPWidgetID getofp_widget, display_widget, getofp_btn, pilot_id_input,
                  status_line, xfer_load_btn;
static ofp_info_t ofp_info;
static XPLMDataRef no_pax_dr, pax_distrib_dr, aft_cargo_dr, fwd_cargo_dr, write_fob_dr;
static XPLMCommandRef set_weight_cmdr;

static int dr_mapped;
static int error_disabled;

static char pref_path[512];
static int tla3xx_detected;
static char pilot_id[20];

static void
map_datarefs()
{
    if (dr_mapped)
        return;

    dr_mapped = 1;

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
    FILE *f = fopen(pref_path, "w");
    if (NULL == f)
        return;

    fprintf(f, "%s", pilot_id);
    fclose(f);
}


static void
load_pref()
{
    FILE *f  = fopen(pref_path, "r");
    if (NULL == f)
        return;

    fgets(pilot_id, sizeof(pilot_id) -1, f);
    fclose(f);
    log_msg("From pref: pilot_id: %s", pilot_id);
}


static int
widget_cb(XPWidgetMessage msg, XPWidgetID widget_id, intptr_t param1, intptr_t param2)
{
    if ((widget_id == getofp_widget) && (msg == xpMessage_CloseButtonPushed)) {
        XPHideWidget(getofp_widget);
        return 1;
    }

    if (error_disabled)
        return 1;

    if ((widget_id == getofp_btn) && (msg == xpMsg_PushButtonPressed)) {
        XPGetWidgetDescriptor(pilot_id_input, pilot_id, sizeof(pilot_id));

        XPSetWidgetDescriptor(status_line, "Fetching...");
        /* Send message to myself to get a draw cycle (draws button as selected) */
        XPSendMessageToWidget(display_widget, MSG_GET_OFP, xpMode_Direct, 0, 0);
        return 1;
    }

    /* self sent message: fetch OFP (lengty) */
    if ((widget_id == display_widget) && (MSG_GET_OFP == msg)) {
        tlsb_ofp_get_parse(pilot_id, &ofp_info);
        tlsb_dump_ofp_info(&ofp_info);

        if (strcmp(ofp_info.status, "Success")) {
            XPSetWidgetDescriptor(status_line, ofp_info.status);
        } else if (strcmp(ofp_info.aircraft_icao, "A319")) {
            XPSetWidgetDescriptor(status_line, "OFP is not for A319");
            memset(&ofp_info, 0, sizeof(ofp_info));
        } else {
            XPSetWidgetDescriptor(status_line, "");
            ofp_info.valid = 1;
            snprintf(ofp_info.altitude, sizeof(ofp_info.altitude), "%d", atoi(ofp_info.altitude) / 100);
         }
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

        int left, top;
        XPGetWidgetGeometry(display_widget, &left, &top, NULL, NULL);

        int left_col = left + 5;
        int right_col = left + 120;

#define DL(TXT) \
    top -= 15; \
    XPLMDrawString(label_color, left_col, top, TXT, NULL, xplmFont_Proportional)

#define DX(field) \
    XPLMDrawString(xfer_color, right_col, top, ofp_info.field, NULL, xplmFont_Basic)

        //DL("Pax:"); DF(right_col, max_passengers);
        // D(right_col, oew);
        DL("Pax:"); DX(pax_count);
        DL("Cargo:"); DX(cargo);
        DL("Fuel:"); DX(fuel_plan_ramp);
        // D(right_col, payload);

        log_msg("bottom of load position %d", top - 15);

        top -= 30;
#define DF(left, field) \
    XPLMDrawString(bg_color, left, top, ofp_info.field, NULL, xplmFont_Basic)

        // D(aircraft_icao);
        DL("Departure:"); DF(right_col, origin); DF(right_col + 50, origin_rwy);
        DL("Destination"); DF(right_col, destination); DF(right_col + 50, destination_rwy);
        DL("Route:"); DF(right_col, route);
        DL("CI"); DF(right_col, ci);
        DL("CRZ FL:"); DF(right_col, altitude);
		return 1;
	}

    return 0;
}


static void
menu_cb(void *menu_ref, void *item_ref)
{
    if (item_ref == (void *)MENU_GET_OFP) {
        /* create gui */
        if (NULL == getofp_widget) {
            int left = 200;
            int top = 800;
            int width = 400;
            int height = 300;

            getofp_widget = XPCreateWidget(left, top, left + width, top - height,
                                         0, "Toliss Simbrief Connector", 1, NULL, xpWidgetClass_MainWindow);
            XPSetWidgetProperty(getofp_widget, xpProperty_MainWindowHasCloseBoxes, 1);
            XPAddWidgetCallback(getofp_widget, widget_cb);
            left += 5; top -= 25;
            XPCreateWidget(left, top, left + width - 2 * 5, top - 15,
                           1, "Pilot Id", 0, getofp_widget, xpWidgetClass_Caption);

            int left1 = left + 80;
            pilot_id_input = XPCreateWidget(left1, top, left1 +  50, top - 15,
                                            1, pilot_id, 0, getofp_widget, xpWidgetClass_TextField);
            XPSetWidgetProperty(pilot_id_input, xpProperty_TextFieldType, xpTextEntryField);
            XPSetWidgetProperty(pilot_id_input, xpProperty_MaxCharacters, sizeof(pilot_id) -1);

            left1 += 65;
            int top1 = top + 8;
            getofp_btn = XPCreateWidget(left1, top1, left1 + 60, top1 - 30,
                                      1, "Fetch OFP", 0, getofp_widget, xpWidgetClass_Button);
            XPAddWidgetCallback(getofp_btn, widget_cb);

            top -= 20;
            status_line = XPCreateWidget(left, top, left + width - 10, top - 20,
                                      1, "", 0, getofp_widget, xpWidgetClass_Caption);

            top -= 20;
            log_msg("display_widget position %d", top);
            display_widget = XPCreateCustomWidget(left + 10, top, left + width -20, top - height + 10,
                                                   1, "", 0, getofp_widget, widget_cb);
            top -= 45;
            log_msg("Button position %d", top);
            xfer_load_btn = XPCreateWidget(left + 10, top, left + 160, top - 30,
                                      1, "Xfer Load data to ISCS", 0, getofp_widget, xpWidgetClass_Button);
            XPAddWidgetCallback(xfer_load_btn, widget_cb);
        }

        XPSetWidgetDescriptor(pilot_id_input, pilot_id);
        XPShowWidget(getofp_widget);
    }
}


PLUGIN_API int
XPluginStart(char *out_name, char *out_sig, char *out_desc)
{

    /* Always use Unix-native paths on the Mac! */
    XPLMEnableFeature("XPLM_USE_NATIVE_PATHS", 1);

    psep = XPLMGetDirectorySeparator();
    XPLMGetSystemPath(xpdir);

    strcpy(out_name, "toliss_simbrief " VERSION);
    strcpy(out_sig, "tlsb-hotbso");
    strcpy(out_desc, "A plugin that connects simbrief to the ToLiss A319");

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
                char acf_path[512];
                char acf_file[256];

                XPLMGetNthAircraftModel(XPLM_USER_AIRCRAFT, acf_file, acf_path);
                log_msg(acf_file);

                if (0 == strncmp(acf_file, "a319", 4)) {
                    XPLMGetSystemPath(acf_file);
                    char *s = acf_file + strlen(acf_file);

                    /* check for directory */
                    sprintf(s, "Resources%splugins%sToLissData%sSituations", psep, psep, psep);
                    if (0 != access(acf_file, F_OK))
                        return;

                    log_msg("Detected ToLiss A319");
                    tla3xx_detected = 1;

                    XPLMMenuID menu = XPLMFindPluginsMenu();
                    int sub_menu = XPLMAppendMenuItem(menu, "Toliss Simbrief", NULL, 1);
                    tlsb_menu = XPLMCreateMenu("Toliss Simbrief", menu, sub_menu, menu_cb, NULL);
                    XPLMAppendMenuItem(tlsb_menu, "Configure", (void *)MENU_CONFIGURE, 0);
                    XPLMAppendMenuItem(tlsb_menu, "Get OFP", (void *)MENU_GET_OFP, 0);
               }
            }
        break;
    }
}
