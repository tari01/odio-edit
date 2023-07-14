/*
    Copyright (C) 2019-2023, Robert Tari <robert@tari.in>
    Copyright (C) 2002 2003 2004 2005 2007 2008 2010 2011 2012, Magnus Hjorth

    This file is part of Odio Edit.

    Odio Edit is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    Odio Edit is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with odio-edit; if not, write to the Free Software
    Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
*/

#include <glib/gi18n.h>
#include <locale.h>
#include "mainwindow.h"
#include "player.h"
#include "main.h"
#include <sys/stat.h>

gboolean g_bQuitFlag;
gboolean g_bIdleWork;
GSettings *g_pGSettings;
GdkRGBA g_lColours[LAST_COLOR];

gchar *COLOURS[] =
{
    "#000000", //TEXT
    "rgba(255, 255, 255, 0.8)", //SCALE
    "#1b2329", //BACKGROUND
    "#80ab01", //WAVE1
    "#c7cdd1", //WAVE2
    "#f5f5f5", //CURSOR
    "rgba(255, 255, 255, 0.8)", //STRIPE
    "#435761", //SELECTION
    "rgba(0, 0, 0, 0.8)", //BARS
    NULL
};

gchar *PATTERNS[] =
{
    "*.[Ww][Aa][Vv]",
    "*.[Ff][Ll][Aa][Cc]",
    "*.[wW][vV]",
    "*.[aA][pP][eE]",
    "*.[mM]4[aA]",
    "*.[mM][pP]3",
    "*.[oO][gG][gG]",
    "*.[oO][gG][aA]",
    "*.[mM][kK][vV]",
    "*.[wW][eE][bB][mM]",
    "*.[dD][sS][fF]",
    "*.[dD][fF][fF]",
    NULL
};

void mainLoop()
{
    if (gtk_events_pending())
    {
        gtk_main_iteration();
        
        return;
    }

    if (!g_bIdleWork)
    {
        g_bIdleWork = TRUE;

        return;
    }
    
    if (m_nStatusBarsWorking)
    {
        return;
    }

    if (mainwindow_UpdateCaches())
    {
        return;
    }

    gtk_main_iteration();
}

    static void onColorSchemeChanged (GSettings *pSettings, const gchar *sKey, gpointer pData)
    {
        gchar *sColorScheme = g_settings_get_string (pSettings, sKey);
        gboolean bDark = g_str_equal (sColorScheme, "prefer-dark");
        g_free (sColorScheme);
        GtkSettings *pGtkSettings = gtk_settings_get_default ();
        
        if (pGtkSettings)
        {
            g_object_set (pGtkSettings, "gtk-application-prefer-dark-theme", bDark, NULL);
        }
    }
    
gint main(gint argc, gchar **argv)
{
    setlocale(LC_ALL, "");
    setlocale(LC_NUMERIC, "POSIX");
    gtk_disable_setlocale();
    bindtextdomain("odio-edit", "/usr/share/locale");
    textdomain("odio-edit");
    bind_textdomain_codeset("odio-edit", "UTF-8");
    gtk_init(&argc, &argv);
    gst_init(&argc, &argv);
    GSettings *pGnomeSettings = g_settings_new ("org.gnome.desktop.interface");
    g_signal_connect (pGnomeSettings, "changed::color-scheme", G_CALLBACK (onColorSchemeChanged), NULL);
    onColorSchemeChanged (pGnomeSettings, "color-scheme", NULL);
    mkdir("/tmp/odio-edit", 0755);
    g_log_set_handler(G_LOG_DOMAIN, G_LOG_LEVEL_MASK, g_log_default_handler, NULL);
    g_pFileFilterWav = gtk_file_filter_new();
    gtk_file_filter_add_pattern(g_pFileFilterWav, "*.[Ww][Aa][Vv]");
    g_pFileFilter = gtk_file_filter_new();

    for (gint nPattern = 0; nPattern < g_strv_length(PATTERNS); nPattern++)
    {
        gtk_file_filter_add_pattern(g_pFileFilter, PATTERNS[nPattern]);
    }
    
    g_pGSettings = g_settings_new("in.tari.odio-edit");

    for (gint nColour = 0; nColour < g_strv_length(COLOURS); nColour++)
    {
        gdk_rgba_parse(&g_lColours[nColour], COLOURS[nColour]);
    }

    mainwindow_RepaintViews();
    gint nFiles = 0;

    for (guint i = 1; i < argc; i++)
    {
        if (g_file_test(argv[i], G_FILE_TEST_EXISTS))
        {
            if (checkExtension(g_pFileFilter, argv[i]))
            {
                gtk_widget_show(mainwindow_NewWithFile(argv[i]));
                nFiles++;
            }
        }
    }

    if (nFiles == 0)
    {
        gtk_widget_show(mainwindow_new());
    }

    while (!g_bQuitFlag)
    {
        mainLoop();
    }

    player_Stop(NULL);

    if (g_pPlayingDocument != NULL)
    {
        g_object_unref(g_pPlayingDocument);
        g_pPlayingDocument = NULL;
    }

    g_list_free(g_lMainWindows);
    g_list_free(g_lDocuments);
    
    if (pGnomeSettings)
    {
        g_clear_object (&pGnomeSettings);
    }

    g_info("chunk_AliveCount: %d", chunk_AliveCount());
    g_info("datasource_Count: %d", datasource_Count());
    
    g_assert (chunk_AliveCount() == 0 && datasource_Count() == 0);

    return 0;
}

gboolean checkExtension(GtkFileFilter *pFileFilter, gchar *sFilePath)
{
    GtkFileFilterInfo cFileFilterInfo;
    cFileFilterInfo.display_name = sFilePath;
    cFileFilterInfo.contains = GTK_FILE_FILTER_DISPLAY_NAME;

    return gtk_file_filter_filter(pFileFilter, &cFileFilterInfo);
}

gchar *getTime(guint32 nSampleRate, gint64 nFrames, gchar *sTime, gboolean bFull)
{
    gfloat fSecs = GFLOAT(nFrames) / GFLOAT(nSampleRate);
    guint fMins = (guint)(fSecs / 60.0);
    fMins = fMins % 60;
    guint nMiliSecs = ((guint) (fSecs  * 1000.0));
    nMiliSecs %= 60000;

    if (bFull == TRUE)
    {
        g_snprintf(sTime, 50, "%02d:%02d.%03d", fMins, nMiliSecs / 1000, nMiliSecs % 1000);
    }
    else
    {
        g_snprintf(sTime, 50, "%02d:%02d", fMins, nMiliSecs / 1000);
    }

    return sTime;
}
