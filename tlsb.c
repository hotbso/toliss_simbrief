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
static XPWidgetID getofp_widget, getofp_btn;

static char pref_path[512];
static int tla3xx_detected;
static char pilot_id[20];

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
get_ofp()
{
    /* that should be plenty */
    int buflen = 1024 * 1014;
    char *ofp = malloc(buflen);
    if (NULL == ofp) {
        log_msg("Can't malloc");
        return 0;
    }
    
    int ret_len;
    int res = tlsb_ofp_get(pilot_id, ofp, buflen, &ret_len);
    if (res) {
        log_msg("got ofp %d bytes", ret_len);
        ofp[200] = 0;
        log_msg(ofp);
    }

    free(ofp);
    return 1;
}

static int
widget_cb(XPWidgetMessage msg, XPWidgetID widget_id, intptr_t param1, intptr_t param2)
{
    if ((widget_id == getofp_widget) && (msg == xpMessage_CloseButtonPushed)) {
        XPHideWidget(getofp_widget);
        return 1;
    } else if ((widget_id == getofp_btn) && (msg == xpMsg_PushButtonPressed)) {
        get_ofp();
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
            int top = 600;
            int width = 200;
            int height = 100;

            getofp_widget = XPCreateWidget(left, top, left + width, top - height,
                                         0, "Toliss Simbrief", 1, NULL, xpWidgetClass_MainWindow);
            XPSetWidgetProperty(getofp_widget, xpProperty_MainWindowHasCloseBoxes, 1);
            XPAddWidgetCallback(getofp_widget, widget_cb);
            left += 5; top -= 25;
#if 0
            XPCreateWidget(left, top, left + width - 2 * 5, top - 15,
                           1, "Autosave copies to keep", 0, getofp_widget, xpWidgetClass_Caption);
            top -= 20;
            pref_slider = XPCreateWidget(left, top, left + width - 30, top - 25,
                                         1, "tlsb_keep", 0, getofp_widget, xpWidgetClass_ScrollBar);

            char buff[10];
            snprintf(buff, sizeof(buff), "%d", tlsb_keep);
            pref_slider_v = XPCreateWidget(left + width - 25, top, left + width - 2*5 , top - 25,
                                           1, buff, 0, getofp_widget, xpWidgetClass_Caption);

            XPSetWidgetProperty(pref_slider, xpProperty_ScrollBarMin, KEEP_MIN);
            XPSetWidgetProperty(pref_slider, xpProperty_ScrollBarMax, KEEP_MAX);
            XPSetWidgetProperty(pref_slider, xpProperty_ScrollBarPageAmount, 1);
            XPSetWidgetProperty(pref_slider, xpProperty_ScrollBarSliderPosition, tlsb_keep);
#endif
            top -= 30;
            getofp_btn = XPCreateWidget(left, top, left + width - 2*5, top - 20,
                                      1, "OK", 0, getofp_widget, xpWidgetClass_Button);
            XPAddWidgetCallback(getofp_btn, widget_cb);
        }

        XPShowWidget(getofp_widget);
    }
}


PLUGIN_API int
XPluginStart(char *out_name, char *out_sig, char *out_desc)
{
    XPLMMenuID menu;
    int sub_menu;

    /* Always use Unix-native paths on the Mac! */
    XPLMEnableFeature("XPLM_USE_NATIVE_PATHS", 1);

    psep = XPLMGetDirectorySeparator();
    XPLMGetSystemPath(xpdir);

    strcpy(out_name, "toliss_simbrief " VERSION);
    strcpy(out_sig, "tlsb-hotbso");
    strcpy(out_desc, "A plugin that connects simbrief to ToLiss' A319");

    /* load preferences */
    XPLMGetPrefsPath(pref_path);
    XPLMExtractFileAndPath(pref_path);
    strcat(pref_path, psep);
    strcat(pref_path, "toliss_simbrief.prf");
    load_pref();

    menu = XPLMFindPluginsMenu();
    sub_menu = XPLMAppendMenuItem(menu, "Toliss Simbrief", NULL, 1);
    tlsb_menu = XPLMCreateMenu("Toliss Simbrief", menu, sub_menu, menu_cb, NULL);
    XPLMAppendMenuItem(tlsb_menu, "Configure", (void *)MENU_CONFIGURE, 0);
    XPLMAppendMenuItem(tlsb_menu, "Get OFP", (void *)MENU_GET_OFP, 0);
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
                }
            }
        break;
    }
}
