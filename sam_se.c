/*

MIT License

Copyright (c) 2024 Holger Teutsch

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
#include <string.h>

#define XPLM200
#include "XPLMPlugin.h"
#include "XPLMDataAccess.h"
#include "XPLMUtilities.h"
#include "XPLMProcessing.h"
#include "XPLMMenus.h"

static char pref_path[512];
static const char *psep;
static XPLMMenuID menu_id;
static int auto_item, season_item[4];
static int auto_season;
static int airport_loaded;

static XPLMDataRef date_day_dr, latitude_dr;
static int cur_day = 999;
static int nh;     // on northern hemisphere
static int cached_day = 999;
static int season; // 0-3
static const char *season_str[] = {"win", "spr", "sum", "fal"};

static void
log_msg(const char *fmt, ...)
{
    char line[1024];

    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line) - 3, fmt, ap);
    strcat(line, "\n");
    XPLMDebugString("sam_se: ");
    XPLMDebugString(line);
    va_end(ap);
}

static void
save_pref()
{
    FILE *f = fopen(pref_path, "w");
    if (NULL == f)
        return;

    /* encode southern hemisphere with negative days */
    int d = nh ? cur_day : -cur_day;
    fprintf(f, "%d,%d,%d", auto_season, d, season);
    fclose(f);
}

static void
load_pref()
{
    FILE *f  = fopen(pref_path, "r");
    if (NULL == f)
        return;

    nh = 1;
    if (3 == fscanf(f, "%i,%i,%i", &auto_season, &cached_day, &season))
        log_msg("From pref: auto_season: %d, cached_day: %d, seasons: %d",
                auto_season, cached_day, season);
    else {
        auto_season = 0;
        log_msg("Error readinf pref");
    }

    fclose(f);

    if (cached_day < 0) {
        nh = 0;
        cached_day = -cached_day;
    }
}

// Accessor for the "sam/season/*" dataref
static int
read_season_acc(void *ref)
{
    int s = (long long)ref;
    int val = (s == season);

    //log_msg("accessor %s called, returns %d", season_str[s], val);
    return val;
}

// set season according to date
static void
set_season_auto(int day)
{
    if (! auto_season)
        return;

    if (nh) {
        if (day <= 80) {
            season = 0;
        } else if (day <= 172) {
            season = 1;
        } else if (day <= 264) {
            season = 2;
        } else if (day <= 355) {
            season = 3;
        } else if (day) {
            season = 0;
        }
    } else {
        if (day <= 80) {
            season = 2;
        } else if (day <= 172) {
            season = 3;
        } else if (day <= 264) {
            season = 0;
        } else if (day <= 355) {
            season = 1;
        } else if (day) {
            season = 2;
        }
    }

    log_msg("nh: %d, day: %d->%d, season: %d", nh, cur_day, day, season);
    cur_day = day;
}

// emuluate a kind of radio buttons
static void
set_menu()
{
    XPLMCheckMenuItem(menu_id, auto_item,
                      auto_season ? xplm_Menu_Checked : xplm_Menu_Unchecked);
    if (auto_season) {
        for (int i = 0; i < 4; i++)
            XPLMCheckMenuItem(menu_id, season_item[i], xplm_Menu_Unchecked);
    } else {
        XPLMCheckMenuItem(menu_id, season_item[season], xplm_Menu_Checked);
        for (int i = 0; i < 4; i++)
            if (i != season)
                XPLMCheckMenuItem(menu_id, season_item[i], xplm_Menu_Unchecked);
    }
}

static void
menu_cb(void *menu_ref, void *item_ref)
{
    int entry = (long long)item_ref;
    if (entry == 4) {
        auto_season = !auto_season;
        set_season_auto(XPLMGetDatai(date_day_dr));
    } else {
        int checked;
        XPLMCheckMenuItemState(menu_id, season_item[entry], &checked);
        if (checked == 0) { // unchecking means auto
            auto_season = 1;
            set_season_auto(XPLMGetDatai(date_day_dr));
        } else {
            auto_season = 0;
            season = entry;
        }
    }

    set_menu();
    save_pref();
}

PLUGIN_API int
XPluginStart(char *out_name, char *out_sig, char *out_desc)
{
    XPLMMenuID menu;
    int sub_menu;

    strcpy(out_name, "sam_se " VERSION);
    strcpy(out_sig, "sam_se.hotbso");
    strcpy(out_desc, "A plugin that emulates SAM Seasons");

    /* Always use Unix-native paths on the Mac! */
    XPLMEnableFeature("XPLM_USE_NATIVE_PATHS", 1);

    psep = XPLMGetDirectorySeparator();

    /* set pref path */
    XPLMGetPrefsPath(pref_path);
    XPLMExtractFileAndPath(pref_path);
    strcat(pref_path, psep);
    strcat(pref_path, "sam_se.prf");

    menu = XPLMFindPluginsMenu();
    sub_menu = XPLMAppendMenuItem(menu, "SAM Seasons Emulator", NULL, 1);
    menu_id = XPLMCreateMenu("SAM Seasons Emulator", menu, sub_menu, menu_cb, NULL);

    auto_item = XPLMAppendMenuItem(menu_id, "Automatic", (void *)4, 0);
    XPLMAppendMenuSeparator(menu_id);
    season_item[0] = XPLMAppendMenuItem(menu_id, "Winter", (void *)0, 0);
    season_item[1] = XPLMAppendMenuItem(menu_id, "Spring", (void *)1, 0);
    season_item[2] = XPLMAppendMenuItem(menu_id, "Summer", (void *)2, 0);
    season_item[3] = XPLMAppendMenuItem(menu_id, "Autumn", (void *)3, 0);

    load_pref();
    set_menu();

    date_day_dr = XPLMFindDataRef("sim/time/local_date_days");
    latitude_dr = XPLMFindDataRef("sim/flightmodel/position/latitude");


    XPLMRegisterDataAccessor("sam/season/winter", xplmType_Int, 0, read_season_acc,
                             NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                             NULL, NULL, NULL, (void *)0, NULL);

    XPLMRegisterDataAccessor("sam/season/spring", xplmType_Int, 0, read_season_acc,
                             NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                             NULL, NULL, NULL, (void *)1, NULL);

    XPLMRegisterDataAccessor("sam/season/summer", xplmType_Int, 0, read_season_acc,
                             NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                             NULL, NULL, NULL, (void *)2, NULL);

    XPLMRegisterDataAccessor("sam/season/autumn", xplmType_Int, 0, read_season_acc,
                             NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                             NULL, NULL, NULL, (void *)3, NULL);

    return 1;
}


PLUGIN_API void
XPluginStop(void)
{
}


PLUGIN_API void
XPluginDisable(void)
{
    save_pref();
}


PLUGIN_API int
XPluginEnable(void)
{
    return 1;
}

PLUGIN_API void
XPluginReceiveMessage(XPLMPluginID in_from, long in_msg, void *in_param)
{
    /* Everything before XPLM_MSG_AIRPORT_LOADED has bogus datarefs.
       Anyway it's too late for the current scenery. */
    if ((in_msg == XPLM_MSG_AIRPORT_LOADED) ||
        (airport_loaded && (in_msg == XPLM_MSG_SCENERY_LOADED))) {
        airport_loaded = 1;
        int day = XPLMGetDatai(date_day_dr);
        nh = (XPLMGetDatad(latitude_dr) >= 0.0);
        set_season_auto(day);
    }
}
