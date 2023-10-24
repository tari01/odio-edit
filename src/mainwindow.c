/*
    Copyright (C) 2019-2023, Robert Tari <robert@tari.in>
    Copyright (C) 2002 2003 2004 2005 2006 2007 2008 2009 2010 2011 2012 2013 2018, Magnus Hjorth

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
#include <math.h>
#include "mainwindow.h"
#include "message.h"
#include "main.h"
#include "player.h"

G_DEFINE_TYPE(MainWindow, mainwindow, GTK_TYPE_WINDOW)

static MainWindow *mainwindow_SetDocument(MainWindow *pMainWindow, Document *pDocument, gchar *sFilePath);
static void mainwindow_OnViewChanged(Document *pDocument, MainWindow *pMainWindow);
static void mainwindow_OnSelectionChanged(Document *pDocument, MainWindow *pMainWindow);
static void mainwindow_OnCursorChanged(Document *pDocument, gboolean bRolling, MainWindow *pMainWindow);
static void mainwindow_OnStateChanged(Document *pDocument, MainWindow *pMainWindow);
static void mainwindow_UpdateDesc(MainWindow *pMainWindow);
static void mainwindow_OnZoomIn(GtkMenuItem *pMenuItem, gpointer pUserData);
static void mainwindow_OnZoomOut(GtkMenuItem *pMenuItem, gpointer pUserData);
static void mainwindow_OnPlay(GtkMenuItem *pMenuItem, MainWindow *pMainWindow);
static void mainwindow_OnPlaySelection(GtkMenuItem *pMenuItem, MainWindow *pMainWindow);
static void mainwindow_OnStop(GtkMenuItem *pMenuItem, MainWindow *pMainWindow);
static void mainwindow_OnClose(GtkMenuItem *pMenuItem, gpointer pUserData);
static void mainwindow_AddRecentFile(gchar *sFilePath);
static GList *m_lstRecentFilenames = NULL;
static Chunk *m_pClipboard = NULL;
static gboolean m_bZooming = FALSE;
guint m_nStatusBarsWorking = 0;
GList *g_lMainWindows = NULL;
MainWindow *g_pFocusedWindow = NULL;
GtkFileFilter *g_pFileFilter = NULL;
GtkFileFilter *g_pFileFilterWav = NULL;

struct GetFileName
{
    gboolean bSave;
    gboolean bResponded;
    gint nResponse;
};

static void mainwindow_OnGetFileName(GtkDialog *pDialog, gint nResponse, gpointer pUserData)
{
    struct GetFileName *pResponse = (struct GetFileName *)pUserData;

    if (pResponse->bSave && nResponse == GTK_RESPONSE_ACCEPT)
    {
        gchar *strFilename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(pDialog));

        if (strFilename != NULL)
        {
            if (!g_str_has_suffix(strFilename, ".wav"))
            {
                g_free(strFilename);
                message_Warning(_("The file extension should be \".wav\""));

                return;
            }

            g_free(strFilename);
        }
    }

    pResponse->bResponded = TRUE;
    pResponse->nResponse = nResponse;
}

static gchar *mainwindow_GetFileName(gchar *sCurrentName, gchar *sTitle, gboolean bSave)
{
    GtkFileChooserAction pFileChooserAction = bSave ? GTK_FILE_CHOOSER_ACTION_SAVE : GTK_FILE_CHOOSER_ACTION_OPEN;
    GtkWidget *pWidget = gtk_file_chooser_dialog_new(sTitle, GTK_WINDOW(g_pFocusedWindow), pFileChooserAction, bSave ? _("_Save") : _("_Open"), GTK_RESPONSE_ACCEPT, _("_Cancel"), GTK_RESPONSE_CANCEL, NULL);
    GtkFileChooser *pFileChooser = GTK_FILE_CHOOSER(pWidget);

    if (bSave == FALSE)
    {
        g_object_ref_sink(g_pFileFilter);
        gtk_file_chooser_set_filter(pFileChooser, g_pFileFilter);
    }
    else
    {
        g_object_ref_sink(g_pFileFilterWav);
        gtk_file_chooser_set_filter(pFileChooser, g_pFileFilterWav);
        gtk_file_chooser_set_do_overwrite_confirmation(pFileChooser, TRUE);
    }

    gtk_window_set_position(GTK_WINDOW(pWidget), GTK_WIN_POS_CENTER);
    gtk_window_set_modal(GTK_WINDOW(pWidget), TRUE);
    gchar *sFileName = NULL;

    if (sCurrentName != NULL)
    {
        sFileName = g_filename_to_utf8(sCurrentName, -1, NULL, NULL, NULL);
        gtk_file_chooser_set_filename(pFileChooser, sFileName);

        if (bSave)
        {
            gchar *sBaseName = g_path_get_basename(sFileName);

            if (sBaseName)
            {
                if (!checkExtension(g_pFileFilterWav, sBaseName))
                {
                    if (checkExtension(g_pFileFilter, sBaseName))
                    {
                        gchar *pDotPos = g_strrstr(sBaseName, ".");
                        pDotPos[0] = '\0';
                    }

                    gchar *sBaseNameNew = g_strconcat(sBaseName, ".wav", NULL);
                    g_free(sBaseName);
                    sBaseName = g_strdup(sBaseNameNew);
                    g_free(sBaseNameNew);
                }

                gtk_file_chooser_set_current_name(pFileChooser, sBaseName);
                g_free(sBaseName);
            }
        }

        g_free(sFileName);
        sFileName = NULL;
    }

    struct GetFileName cResponse = {bSave, FALSE, 0};
    g_signal_connect(pFileChooser, "response", G_CALLBACK(mainwindow_OnGetFileName), &cResponse);
    gtk_widget_show(pWidget);

    while (!cResponse.bResponded)
    {
        mainLoop();
    }

    if (cResponse.nResponse == GTK_RESPONSE_ACCEPT)
    {
        sFileName = gtk_file_chooser_get_filename(pFileChooser);
    }

    if (cResponse.nResponse != GTK_RESPONSE_DELETE_EVENT)
    {
        gtk_widget_destroy(pWidget);
    }

    return sFileName;
}

void mainwindow_BeginProgress(MainWindow *pMainWindow, gchar *sDescription)
{
    if (sDescription == NULL)
    {
        sDescription = _("Processing data");
    }

    gchar *sText = g_strdup_printf (_("%s... Press ESC to cancel."), sDescription);
    gtk_progress_bar_set_text (GTK_PROGRESS_BAR (pMainWindow->pProgressBar), sText);
    g_free (sText);
    pMainWindow->nStatusBarTimeLast = 0;
    pMainWindow->bStatusBarWorking = TRUE;
    pMainWindow->bStatusBarBreak = FALSE;
    m_nStatusBarsWorking++;
    mainwindow_SetSensitive(pMainWindow, FALSE);
    gtk_window_present(GTK_WINDOW(pMainWindow));
    gtk_grab_add(GTK_WIDGET(pMainWindow));
}

static void mainwindow_ResetStatusBar(MainWindow *pMainWindow)
{
    pMainWindow->nStatusBarTimeLast = 0;
    pMainWindow->bStatusBarBreak = FALSE;
    pMainWindow->bStatusBarWorking = FALSE;
    gtk_progress_bar_set_text(pMainWindow->pProgressBar, _("Ready"));
    gtk_progress_bar_set_fraction(pMainWindow->pProgressBar, 0);
}

static void mainwindow_FixTitle(MainWindow *pMainWindow)
{
    if (pMainWindow->pDocument != NULL)
    {
        gchar lText[32];
        gchar *sTitle = g_strdup_printf("Odio Edit - %s (%s): %d Hz, %s", pMainWindow->pDocument->sTitleName, getTime((pMainWindow->pDocument->pChunk)->pAudioInfo->rate, pMainWindow->pDocument->pChunk->nFrames, lText, TRUE), pMainWindow->pDocument->pChunk->pAudioInfo->rate, pMainWindow->pDocument->pChunk->pAudioInfo->finfo->description);
        gtk_window_set_title(GTK_WINDOW(pMainWindow), sTitle);
        g_free(sTitle);
    }
    else
    {
        gtk_window_set_title(GTK_WINDOW(pMainWindow), "Odio Edit");
    }
}

void mainwindow_EndProgress(MainWindow *pMainWindow)
{
    mainwindow_ResetStatusBar(pMainWindow);
    mainwindow_SetSensitive(pMainWindow, TRUE);
    g_list_foreach(g_lMainWindows, (GFunc)mainwindow_FixTitle, NULL);
    g_list_foreach(g_lMainWindows, (GFunc)mainwindow_ResetStatusBar, pMainWindow);
    g_list_foreach(g_lMainWindows, (GFunc)mainwindow_UpdateDesc, NULL);
    gtk_grab_remove(GTK_WIDGET(pMainWindow));

    if (pMainWindow->bCloseWhenDone == TRUE)
    {
        GdkEventAny cEvent = {GDK_DELETE, gtk_widget_get_window(GTK_WIDGET(pMainWindow)), TRUE};
        gtk_main_do_event((GdkEvent*)&cEvent);
    }

    pMainWindow->bStatusBarWorking = FALSE;
    m_nStatusBarsWorking--;
}

gboolean mainwindow_Progress(MainWindow *pMainWindow, gfloat fProgress)
{
    if (fProgress > 1.0)
    {
        fProgress = 1.0;
    }

    gint64 nTime = g_get_real_time();

    if ((nTime - pMainWindow->nStatusBarTimeLast) >= 40000)
    {
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(pMainWindow->pProgressBar), fProgress);
        pMainWindow->nStatusBarTimeLast = nTime;
    }

    g_bIdleWork = FALSE;

    while (!g_bIdleWork)
    {
        mainLoop();
    }

    return pMainWindow->bStatusBarBreak;
}

static void mainwindow_OnWidgetDestroy(GtkWidget *pWidget, GList **pList)
{
    *pList = g_list_remove(*pList, pWidget);
}

static void mainwindow_AppendWidget(GList **pList, gpointer pWidget)
{
    *pList = g_list_append(*pList, pWidget);
    g_signal_connect(pWidget, "destroy", G_CALLBACK(mainwindow_OnWidgetDestroy), pList);
}

static void mainwindow_OnRecentFile(GtkMenuItem *pMenuItem, MainWindow *pMainWindow)
{
    GList *lstRecentMenuItems = gtk_container_get_children(GTK_CONTAINER(pMainWindow->pRecentMenu));
    GList *pRecentFilename = m_lstRecentFilenames;
    GList *pRecentMenuItem = g_list_first(lstRecentMenuItems);

    while (pRecentMenuItem->data != pMenuItem)
    {
        pRecentMenuItem = pRecentMenuItem->next;
        pRecentFilename = pRecentFilename->next;
    }

    gchar *sFileName = g_strdup((gchar *)pRecentFilename->data);
    Document *pDocument = document_NewWithFile(sFileName, pMainWindow);

    if (pDocument == NULL)
    {
        g_free(sFileName);

        return;
    }

    mainwindow_SetDocument(pMainWindow, pDocument, sFileName);
    g_settings_set_string(g_pGSettings, "last-opened", sFileName);
    mainwindow_AddRecentFile(sFileName);
    g_free(sFileName);
    g_list_free(lstRecentMenuItems);
}

static void mainwindow_UpdateRecentFiles(MainWindow *pMainWindow)
{
    GList *lstRecentMenuItems = gtk_container_get_children(GTK_CONTAINER(pMainWindow->pRecentMenu));
    GList *pRecentMenuItem = g_list_first(lstRecentMenuItems);
    guint nPos = 0;

    for (GList *pRecentFilename = m_lstRecentFilenames; pRecentFilename != NULL; pRecentFilename = pRecentFilename->next)
    {
        gchar *sBaseName = g_path_get_basename((gchar *)pRecentFilename->data);

        if (pRecentMenuItem != NULL && nPos < g_list_length(lstRecentMenuItems) - 2)
        {
            gtk_label_set_text(GTK_LABEL(gtk_bin_get_child(GTK_BIN(pRecentMenuItem->data))), sBaseName);
            pRecentMenuItem = pRecentMenuItem->next;
        }
        else
        {
            GtkWidget *pMenuItem = gtk_menu_item_new_with_label(sBaseName);
            g_signal_connect(pMenuItem, "activate", G_CALLBACK(mainwindow_OnRecentFile), pMainWindow);
            gtk_menu_shell_insert(GTK_MENU_SHELL(pMainWindow->pRecentMenu), pMenuItem, nPos);
            gtk_widget_show_all(pMainWindow->pRecentMenu);
        }

        g_free(sBaseName);
        nPos++;
    }

    if (m_lstRecentFilenames != NULL && gtk_menu_tool_button_get_menu(GTK_MENU_TOOL_BUTTON(pMainWindow->pToolItemRecent)) == NULL)
    {
        GtkWidget *pMenuItem = gtk_separator_menu_item_new();
        gtk_menu_shell_append(GTK_MENU_SHELL(pMainWindow->pRecentMenu), pMenuItem);
        pMenuItem = gtk_menu_item_new_with_label(_("Close"));
        g_signal_connect(pMenuItem, "activate", G_CALLBACK(mainwindow_OnClose), pMainWindow);
        gtk_menu_shell_append(GTK_MENU_SHELL(pMainWindow->pRecentMenu), pMenuItem);
        gtk_menu_tool_button_set_arrow_tooltip_text(GTK_MENU_TOOL_BUTTON(pMainWindow->pToolItemRecent), _("Click here for recent files"));
        gtk_menu_tool_button_set_menu(GTK_MENU_TOOL_BUTTON(pMainWindow->pToolItemRecent), pMainWindow->pRecentMenu);
        gtk_widget_show_all(pMainWindow->pRecentMenu);
    }

    g_list_free(lstRecentMenuItems);
}

static void mainwindow_AddRecentFile(gchar *sFilePath)
{
    GList *pRecentFilename = m_lstRecentFilenames;

    while (pRecentFilename != NULL)
    {
        if (!strcmp((gchar *)pRecentFilename->data, sFilePath))
        {
            m_lstRecentFilenames = g_list_remove_link(m_lstRecentFilenames, pRecentFilename);
            g_free(pRecentFilename->data);
            g_list_free_1(pRecentFilename);

            break;
        }
        else
        {
            pRecentFilename = pRecentFilename->next;
        }
    }

    m_lstRecentFilenames = g_list_prepend(m_lstRecentFilenames, g_strdup(sFilePath));

    if (g_list_length(m_lstRecentFilenames) > MAINWINDOW_RECENT_MAX)
    {
        pRecentFilename = g_list_last(m_lstRecentFilenames);
        m_lstRecentFilenames = g_list_remove_link(m_lstRecentFilenames, pRecentFilename);
        g_free(pRecentFilename->data);
        g_list_free_1(pRecentFilename);
    }

    g_list_foreach(g_lMainWindows, (GFunc)mainwindow_UpdateRecentFiles, NULL);

    guint nFile = 0;
    pRecentFilename = m_lstRecentFilenames;
    gchar **lstFiles = g_malloc0(sizeof(gchar *) * (g_list_length(m_lstRecentFilenames) + 1));

    while (pRecentFilename != NULL)
    {
        lstFiles[nFile++] = pRecentFilename->data;
        pRecentFilename = pRecentFilename->next;
    }

    g_settings_set_strv(g_pGSettings, "recent-files", (const gchar * const *)lstFiles);
    g_free(lstFiles);
}

static void mainwindow_Enable(GList *lListItem, gboolean bSensitive)
{
    while (lListItem != NULL)
    {
        gtk_widget_set_sensitive(GTK_WIDGET(lListItem->data), bSensitive);
        lListItem = lListItem->next;
    }
}

void mainwindow_SetStatusBarInfo(MainWindow *pMainWindow, gint64 nCursorPos, gboolean bRolling, gint64 nViewStart, gint64 nViewEnd, gint64 nSelStart, gint64 nSelEnd, gint64 nSampleRate, gint64 nMaxValue)
{
    if (!pMainWindow->bStatusBarWorking)
    {
        gboolean bCursorDiff = FALSE;

        if (bRolling != pMainWindow->bStatusBarRolling)
        {
            bCursorDiff = TRUE;
        }
        else if (bRolling)
        {
            bCursorDiff = (nCursorPos < pMainWindow->nStatusBarCursorPos) || (nCursorPos > pMainWindow->nStatusBarCursorPos + nSampleRate / 20);
        }
        else
        {
            bCursorDiff = (!bRolling && pMainWindow->nStatusBarCursorPos != nCursorPos);
        }

        gboolean bViewDiff = (pMainWindow->nStatusBarViewStart != nViewStart) || (pMainWindow->nStatusBarViewEnd != nViewEnd);
        gboolean bSelDiff;

        if (nSelStart == nSelEnd)
        {
            bSelDiff = (pMainWindow->nStatusBarSelStart != pMainWindow->nStatusBarSelEnd);
        }
        else
        {
            bSelDiff = (pMainWindow->nStatusBarSelStart != nSelStart) || (pMainWindow->nStatusBarSelEnd != nSelEnd);
        }

        gchar sText[256];

        if (bCursorDiff)
        {
            g_snprintf(sText, 150, "<span font-weight=\"bold\">%s:</span> %s", _("Cursor"), getTime(nSampleRate, nCursorPos, sText + 150, TRUE));
            gtk_label_set_markup(GTK_LABEL(pMainWindow->pLabelCursor), sText);
            pMainWindow->nStatusBarCursorPos = nCursorPos;
            pMainWindow->bStatusBarRolling = bRolling;
        }

        if (bViewDiff)
        {
            g_snprintf(sText, 150, "<span font-weight=\"bold\">%s:</span> %s - %s", _("View"), getTime(nSampleRate, nViewStart, sText + 150, TRUE), getTime(nSampleRate, nViewEnd, sText + 200, TRUE));
            gtk_label_set_markup(GTK_LABEL(pMainWindow->pLabelView), sText);
            pMainWindow->nStatusBarViewStart = nViewStart;
            pMainWindow->nStatusBarViewEnd = nViewEnd;
        }

        if (bSelDiff)
        {
            if (nSelStart != nSelEnd)
            {
                g_snprintf(sText, 150, "<span font-weight=\"bold\">%s:</span> %s + %s", _("Selection"), getTime(nSampleRate, nSelStart, sText + 150, TRUE), getTime(nSampleRate, nSelEnd - nSelStart, sText + 200, TRUE));

            }
            else
            {
                g_snprintf(sText, 150, "<span font-weight=\"bold\">%s:</span> 00:00.000 + 00:00.000", _("Selection"));
            }

            gtk_label_set_markup(GTK_LABEL(pMainWindow->pLabelSel), sText);
            pMainWindow->nStatusBarSelStart = nSelStart;
            pMainWindow->nStatusBarSelEnd = nSelEnd;
        }
    }
}

static void mainwindow_UpdateDesc(MainWindow *pMainWindow)
{
    if (pMainWindow->pDocument != NULL)
    {
        mainwindow_SetStatusBarInfo(pMainWindow, pMainWindow->pDocument->nCursorPos, (g_pPlayingDocument == pMainWindow->pDocument), pMainWindow->pDocument->nViewStart, pMainWindow->pDocument->nViewEnd,pMainWindow->pDocument->nSelStart, pMainWindow->pDocument->nSelEnd, pMainWindow->pDocument->pChunk->pAudioInfo->rate, pMainWindow->pDocument->pChunk->nFrames);
    }
    else
    {
        mainwindow_ResetStatusBar(pMainWindow);
    }
}

static void mainwindow_Open(MainWindow *pMainWindow, gchar *sFilePath)
{
    Document *pDocument = document_NewWithFile(sFilePath, pMainWindow);

    if (pDocument == NULL)
    {
        return;
    }

    mainwindow_SetDocument(pMainWindow, pDocument, sFilePath);
    g_settings_set_string(g_pGSettings, "last-opened", sFilePath);
    mainwindow_AddRecentFile(sFilePath);
}

static gchar *mainwindow_GetSaveFileName(gchar *sOldFileName, gchar *sTitle)
{
    gchar *sLastSavedFile = g_settings_get_string(g_pGSettings, "last-saved");

    if (sLastSavedFile == NULL)
    {
        sLastSavedFile = g_settings_get_string(g_pGSettings, "last-opened");
    }

    gchar *sLastSavedDir = NULL;

    if (sLastSavedFile != NULL)
    {
        sLastSavedDir = g_strdup(sLastSavedFile);
        gchar *sBaseName = strrchr(sLastSavedDir, '/');

        if (sBaseName != NULL)
        {
            sBaseName[1] = 0;
        }
        else
        {
            g_free(sLastSavedDir);
            sLastSavedDir = NULL;
        }
    }

    gchar *sFilePath;

    if (sLastSavedDir == NULL)
    {
        sFilePath = mainwindow_GetFileName(sOldFileName, sTitle, TRUE);
    }
    else
    {
        if (sOldFileName != NULL)
        {
            gchar *sBaseName = strrchr(sOldFileName, '/');

            if (!sBaseName)
            {
                sBaseName = sOldFileName;
            }
            else
            {
                sBaseName = sBaseName + 1;
            }

            gchar *sPath = g_strjoin("/", sLastSavedDir, sBaseName, NULL);
            sFilePath = mainwindow_GetFileName(sPath, sTitle, TRUE);
            g_free(sPath);
        }
        else
        {
            sFilePath = mainwindow_GetFileName(NULL, sTitle, TRUE);
        }
    }

    g_free(sLastSavedFile);
    g_free(sLastSavedDir);

    return sFilePath;
}

static gboolean mainwindow_Save(MainWindow *pMainWindow, gchar *sFilePath)
{
    gboolean bFree = FALSE;
    gboolean bGetSaveFileName = FALSE;

    if (sFilePath == NULL)
    {
        bGetSaveFileName = TRUE;
    }
    else if (!checkExtension(g_pFileFilterWav, sFilePath))
    {
        g_free(sFilePath);
        bGetSaveFileName = TRUE;
    }

    if (bGetSaveFileName)
    {
        sFilePath = mainwindow_GetSaveFileName(pMainWindow->pDocument->sFilePath, _("Save file"));

        if (sFilePath == NULL)
        {
            return TRUE;
        }

        bFree = TRUE;
    }

    g_assert(sFilePath != NULL);

    gboolean bError = document_Save(pMainWindow->pDocument, sFilePath);

    if (bError)
    {
        if (bFree)
        {
            g_free(sFilePath);
            return TRUE;
        }
    }

    g_settings_set_string(g_pGSettings, "last-saved", sFilePath);
    mainwindow_AddRecentFile(sFilePath);

    if (bFree)
    {
        g_free(sFilePath);
    }

    return FALSE;
}

static gboolean mainwindow_ChangeCheck(MainWindow *pMainWindow)
{
    if (pMainWindow->pDocument == NULL || (pMainWindow->pDocument->sFilePath != NULL && !document_CanUndo(pMainWindow->pDocument)))
    {
        return FALSE;
    }

    gchar *sMessage = g_strdup_printf(_("Save changes to %s?"), pMainWindow->pDocument->sTitleName);
    gint nResponse = message_Info(sMessage, GTK_BUTTONS_NONE);
    g_free(sMessage);

    switch (nResponse)
    {
        case GTK_RESPONSE_YES:
        {
            return mainwindow_Save(pMainWindow, pMainWindow->pDocument->sFilePath);
        }
        case GTK_RESPONSE_NO:
        {
            return FALSE;
        }
        case GTK_RESPONSE_CANCEL:
        {
            return TRUE;
        }
    }

    g_assert_not_reached();

    return TRUE;
}

static void mainwindow_OnDestroy(GtkWidget *pWidget)
{
    g_info("mainwindow_OnDestroy");
    MainWindow *pMainWindow = OE_MAINWINDOW(pWidget);
    g_lMainWindows = g_list_remove(g_lMainWindows, pWidget);

    if (pMainWindow->pDocument != NULL)
    {
        g_signal_handlers_disconnect_matched(pMainWindow->pDocument, G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, pWidget);
        g_info("document_unref, mainwindow:mainwindow_OnDestroy %p", pMainWindow->pDocument);
        g_object_unref(pMainWindow->pDocument);
        pMainWindow->pDocument = NULL;
    }

    if (g_list_length(g_lMainWindows) == 0)
    {
        if (m_pClipboard)
        {
            g_info("chunk_unref: %d, mainwindow:mainwindow_OnDestroy %p", chunk_AliveCount(), m_pClipboard);
            g_object_unref(m_pClipboard);
        }

        m_pClipboard = NULL;
        g_bQuitFlag = TRUE;
    }

    GTK_WIDGET_CLASS(mainwindow_parent_class)->destroy(pWidget);
}

static gint mainwindow_OnDeleteEvent(GtkWidget *pWidget, GdkEventAny *pEventAny)
{
    MainWindow *pMainWindow = OE_MAINWINDOW(pWidget);
    GtkWidgetClass *pWidgetClass = GTK_WIDGET_CLASS(mainwindow_parent_class);

    if (mainwindow_ChangeCheck(pMainWindow))
    {
        return TRUE;
    }

    if (pMainWindow->bStatusBarWorking == TRUE)
    {
        pMainWindow->bStatusBarBreak = TRUE;
        pMainWindow->bCloseWhenDone = TRUE;

        return TRUE;
    }

    if (g_pPlayingDocument == pMainWindow->pDocument)
    {
        player_Stop ();
    }

    if (pWidgetClass->delete_event)
    {
        return pWidgetClass->delete_event(pWidget, pEventAny);
    }
    else
    {
        return FALSE;
    }
}

static void mainwindow_OnRealize(GtkWidget *pWidget)
{
    GTK_WIDGET_CLASS(mainwindow_parent_class)->realize(pWidget);
    gtk_window_set_icon_name(GTK_WINDOW(pWidget), "odio-edit");
}

static void mainwindow_Play(MainWindow *pMainWindow, gint64 nStart, gint64 nEnd)
{
    document_Play(pMainWindow->pDocument, nStart, nEnd);
    mainwindow_UpdateDesc(pMainWindow);
}

static gint mainwindow_OnKeyPress(GtkWidget *pWidget, GdkEventKey *pEventKey)
{
    MainWindow *pMainWindow = OE_MAINWINDOW(pWidget);

    if (!pMainWindow->bSensitive)
    {
        if (pEventKey->keyval == GDK_KEY_Escape)
        {
            pMainWindow->bStatusBarBreak = TRUE;
        }

        return TRUE;
    }

    if (pMainWindow->pDocument == NULL)
    {
        return GTK_WIDGET_CLASS(mainwindow_parent_class)->key_press_event(pWidget, pEventKey);
    }

    switch (pEventKey->keyval)
    {
        case GDK_KEY_KP_Add:
        {
            mainwindow_OnZoomIn(NULL, pWidget);
            return TRUE;
        }
        case GDK_KEY_KP_Subtract:
        {
            mainwindow_OnZoomOut(NULL, pWidget);
            return TRUE;
        }
        case GDK_KEY_Home:
        {
            document_SetView(pMainWindow->pDocument, 0, pMainWindow->pDocument->nViewEnd - pMainWindow->pDocument->nViewStart);

            if (g_pPlayingDocument != pMainWindow->pDocument || pMainWindow->pDocument->bFollowMode)
            {
                document_SetCursor(pMainWindow->pDocument,0);
            }

            return TRUE;
        }
        case GDK_KEY_End:
        {
            document_Stop(pMainWindow->pDocument);
            document_Scroll(pMainWindow->pDocument, pMainWindow->pDocument->pChunk->nFrames);

            if (g_pPlayingDocument != pMainWindow->pDocument || pMainWindow->pDocument->bFollowMode)
            {
                document_SetCursor(pMainWindow->pDocument, pMainWindow->pDocument->pChunk->nFrames);
            }

            return TRUE;
        }
    }

    return GTK_WIDGET_CLASS(mainwindow_parent_class)->key_press_event(pWidget, pEventKey);
}

static void mainwindow_OnDragDataReceived(GtkWidget *pWidget, GdkDragContext *pDragContext, gint nX, gint nY, GtkSelectionData *pSelectionData, guint nInfo, guint nTime)
{
    gint nLength = gtk_selection_data_get_length(pSelectionData);

    if (nLength > 0)
    {
        gchar *sData = g_malloc(nLength + 1);
        memcpy(sData, gtk_selection_data_get_data(pSelectionData), nLength);
        sData[nLength] = 0;
        gtk_drag_finish(pDragContext, TRUE, FALSE, nTime);

        for (gchar *sUri = strtok(sData, "\n\r"); sUri != NULL; sUri = strtok(NULL, "\n\r"))
        {
            gchar *sFilePath = g_filename_from_uri(sUri, NULL, NULL);

            if (sFilePath != NULL)
            {
                if (g_file_test(sFilePath, G_FILE_TEST_EXISTS) && checkExtension(g_pFileFilter, sFilePath))
                {
                    mainwindow_Open(OE_MAINWINDOW(pWidget), sFilePath);
                }

                g_free(sFilePath);
            }
        }

        g_free(sData);
    }
    else
    {
        gtk_drag_finish(pDragContext, FALSE, FALSE, nTime);
    }
}

static gboolean mainwindow_OnFocusIn(GtkWidget *pWidget, GdkEventFocus *pEventFocus)
{
    g_pFocusedWindow = OE_MAINWINDOW(pWidget);

    return FALSE;
}

static void mainwindow_class_init(MainWindowClass *cls)
{
    GTK_WIDGET_CLASS(cls)->destroy = mainwindow_OnDestroy;
    GTK_WIDGET_CLASS(cls)->delete_event = mainwindow_OnDeleteEvent;
    GTK_WIDGET_CLASS(cls)->realize = mainwindow_OnRealize;
    GTK_WIDGET_CLASS(cls)->key_press_event = mainwindow_OnKeyPress;
    GTK_WIDGET_CLASS(cls)->drag_data_received = mainwindow_OnDragDataReceived;
    GTK_WIDGET_CLASS(cls)->focus_in_event = mainwindow_OnFocusIn;
}

static MainWindow *mainwindow_SetDocument(MainWindow *pMainWindow, Document *pDocument, gchar *sFilePath)
{
    if (pMainWindow->pDocument != NULL)
    {
        pMainWindow = OE_MAINWINDOW(mainwindow_new());
        gtk_widget_show(GTK_WIDGET(pMainWindow));
    }

    pMainWindow->pDocument = pDocument;
    document_SetFollowMode(pDocument, pMainWindow->bFollowMode);
    g_signal_connect(pDocument, "view_changed", G_CALLBACK(mainwindow_OnViewChanged), pMainWindow);
    g_signal_connect(pDocument, "selection_changed", G_CALLBACK(mainwindow_OnSelectionChanged), pMainWindow);
    g_signal_connect(pDocument, "cursor_changed", G_CALLBACK(mainwindow_OnCursorChanged), pMainWindow);
    g_signal_connect(pDocument, "status_changed", G_CALLBACK(mainwindow_OnStateChanged), pMainWindow);
    chunkview_SetDocument(pMainWindow->pChunkView, pDocument);
    document_SetMainWindow(pDocument, pMainWindow);
    mainwindow_FixTitle(pMainWindow);
    mainwindow_UpdateDesc(pMainWindow);
    mainwindow_Enable(pMainWindow->lNeedChunkItems, TRUE);
    g_lMainWindows = g_list_append(g_lMainWindows, pMainWindow);
    mainwindow_OnViewChanged(pMainWindow->pDocument, pMainWindow);

    return pMainWindow;
}

static MainWindow *mainwindow_SetChunk(MainWindow *pMainWindow, Chunk *pChunk)
{
    Document *pDocument = document_NewWithChunk(pChunk, NULL, pMainWindow);

    return mainwindow_SetDocument(pMainWindow, pDocument, NULL);
}

static void mainwindow_OnOpen(GtkMenuItem *pMenuItem, MainWindow *pMainWindow)
{
    gchar *strFilePath = NULL;

    if (pMainWindow->pDocument != NULL && pMainWindow->pDocument->sFilePath != NULL)
    {
        strFilePath = mainwindow_GetFileName(pMainWindow->pDocument->sFilePath, _("Load file"), FALSE);
    }
    else
    {
        gchar *strFilePathSettings = g_settings_get_string(g_pGSettings, "last-opened");

        if (strFilePathSettings == NULL)
        {
            strFilePathSettings = g_settings_get_string(g_pGSettings, "last-saved");
        }

        strFilePath = mainwindow_GetFileName(strFilePathSettings, _("Load file"), FALSE);
        g_free(strFilePathSettings);
    }

    if (!strFilePath)
    {
        return;
    }

    mainwindow_Open(pMainWindow, strFilePath);
    g_free(strFilePath);
}

static void mainwindow_OnSave(GtkMenuItem *pMenuItem, MainWindow *pMainWindow)
{
    mainwindow_Save(pMainWindow, pMainWindow->pDocument->sFilePath);
}

static void mainwindow_OnSaveAs(GtkMenuItem *pMenuItem, MainWindow *pMainWindow)
{
    mainwindow_Save(pMainWindow, NULL);
}

static void mainwindow_OnSaveSelection(GtkMenuItem *pMenuItem, MainWindow *pMainWindow)
{
    gchar *sFileName = mainwindow_GetSaveFileName(pMainWindow->pDocument->sFilePath, _("Save selection as..."));

    if (!sFileName)
    {
        return;
    }

    Chunk *pChunk = chunk_GetPart(pMainWindow->pDocument->pChunk, pMainWindow->pDocument->nSelStart, pMainWindow->pDocument->nSelEnd - pMainWindow->pDocument->nSelStart);

    if (!chunk_Save(pChunk, sFileName, pMainWindow))
    {
        g_settings_set_string(g_pGSettings, "last-saved", sFileName);
    }

    g_object_unref(pChunk);
    g_free(sFileName);
}

static void mainwindow_OnClose(GtkMenuItem *pMenuItem, gpointer pUserData)
{
    MainWindow *pMainWindow = OE_MAINWINDOW(pUserData);

    if (mainwindow_ChangeCheck(pMainWindow))
    {
        return;
    }

    if (g_pPlayingDocument == pMainWindow->pDocument)
    {
        player_Stop ();
    }

    if (g_list_length(g_lMainWindows) == 1 && pMainWindow->pDocument != NULL)
    {
        chunkview_SetDocument(pMainWindow->pChunkView, NULL);
        g_signal_handlers_disconnect_matched(pMainWindow->pDocument, G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, pMainWindow);
        g_object_unref(pMainWindow->pDocument);
        pMainWindow->pDocument = NULL;
        mainwindow_FixTitle(pMainWindow);
        mainwindow_Enable(pMainWindow->lNeedChunkItems, FALSE);
        mainwindow_Enable(pMainWindow->lNeedUndoItems, FALSE);
        mainwindow_Enable(pMainWindow->lNeedSelectionItems, FALSE);
        g_lMainWindows = g_list_remove(g_lMainWindows, pMainWindow);
    }
    else
    {
        gtk_widget_destroy(GTK_WIDGET(pMainWindow));
    }
}

static void mainwindow_OnUndo(GtkMenuItem *pMenuItem, MainWindow *pMainWindow)
{
    document_Undo(pMainWindow->pDocument);
}

static void mainwindow_OnRedo(GtkMenuItem *pMenuItem, MainWindow *pMainWindow)
{
    document_Redo(pMainWindow->pDocument);
}

static void mainwindow_UpdateClipboardItems(MainWindow *pMainWindow, gpointer pUserData)
{
    mainwindow_Enable(pMainWindow->lNeedClipboardItems, pUserData != NULL);
}

static void mainwindow_OnCut(GtkMenuItem *pMenuItem, MainWindow *pMainWindow)
{
    g_assert(pMainWindow->pDocument->nSelEnd != pMainWindow->pDocument->nSelStart);

    if (m_pClipboard)
    {
        g_info("chunk_unref: %d, mainwindow:mainwindow_OnCut %p", chunk_AliveCount(), m_pClipboard);
        g_object_unref(m_pClipboard);
    }

    Chunk *pChunkPart = chunk_GetPart(pMainWindow->pDocument->pChunk, pMainWindow->pDocument->nSelStart, pMainWindow->pDocument->nSelEnd - pMainWindow->pDocument->nSelStart);
    Chunk *pChunk = chunk_RemovePart(pMainWindow->pDocument->pChunk, pMainWindow->pDocument->nSelStart, pMainWindow->pDocument->nSelEnd - pMainWindow->pDocument->nSelStart);
    m_pClipboard = pChunkPart;

    document_Update(pMainWindow->pDocument, pChunk, pMainWindow->pDocument->nSelStart, -(m_pClipboard->nFrames));
    g_list_foreach(g_lMainWindows, (GFunc)mainwindow_UpdateClipboardItems, (gpointer)pMainWindow);
}

static void mainwindow_OnCrop(GtkMenuItem *pMenuItem, MainWindow *pMainWindow)
{
    document_Update(pMainWindow->pDocument, chunk_GetPart(pMainWindow->pDocument->pChunk, pMainWindow->pDocument->nSelStart, pMainWindow->pDocument->nSelEnd - pMainWindow->pDocument->nSelStart), 0, -(pMainWindow->pDocument->nSelStart));
}

static void mainwindow_OnCopy(GtkMenuItem *pMenuItem, MainWindow *pMainWindow)
{
    g_assert(pMainWindow->pDocument->nSelEnd != pMainWindow->pDocument->nSelStart);

    if (m_pClipboard)
    {
        g_info("chunk_unref: %d, mainwindow:mainwindow_OnCopy %p", chunk_AliveCount(), m_pClipboard);
        g_object_unref(m_pClipboard);
    }

    m_pClipboard = chunk_GetPart(pMainWindow->pDocument->pChunk, pMainWindow->pDocument->nSelStart, pMainWindow->pDocument->nSelEnd - pMainWindow->pDocument->nSelStart);
    g_list_foreach(g_lMainWindows, (GFunc)mainwindow_UpdateClipboardItems, (gpointer)pMainWindow);
}

static void mainwindow_OnPaste(GtkMenuItem *pMenuItem, MainWindow *pMainWindow)
{
    if (pMainWindow->pDocument == NULL)
    {
        mainwindow_SetChunk(pMainWindow, m_pClipboard);
        return;
    }

    if (gst_audio_info_is_equal(pMainWindow->pDocument->pChunk->pAudioInfo, m_pClipboard->pAudioInfo) == FALSE)
    {
        message_Warning(_("You cannot mix different sound formats"));

        return;
    }

    Chunk *pChunk = chunk_Insert(pMainWindow->pDocument->pChunk, m_pClipboard, pMainWindow->pDocument->nCursorPos);
    document_Update(pMainWindow->pDocument, pChunk, pMainWindow->pDocument->nCursorPos, m_pClipboard->nFrames);
    document_SetSelection(pMainWindow->pDocument, pMainWindow->pDocument->nCursorPos - m_pClipboard->nFrames, pMainWindow->pDocument->nCursorPos);
}

static void mainwindow_OnPasteOver(GtkMenuItem *pMenuItem, MainWindow *pMainWindow)
{
    if (pMainWindow->pDocument == NULL)
    {
        mainwindow_SetChunk(pMainWindow, m_pClipboard);

        return;
    }

    if (gst_audio_info_is_equal(pMainWindow->pDocument->pChunk->pAudioInfo, m_pClipboard->pAudioInfo) == FALSE)
    {
        message_Warning(_("You cannot mix different sound formats"));

        return;
    }

    gint64 nFrames = m_pClipboard->nFrames;
    gint64 nFramesOrig = MIN(pMainWindow->pDocument->pChunk->nFrames - pMainWindow->pDocument->nCursorPos, nFrames);
    Chunk *pChunk = chunk_ReplacePart(pMainWindow->pDocument->pChunk, pMainWindow->pDocument->nCursorPos, nFramesOrig, m_pClipboard);

    document_Update(pMainWindow->pDocument, pChunk, 0, 0);
    document_SetSelection(pMainWindow->pDocument, pMainWindow->pDocument->nCursorPos, pMainWindow->pDocument->nCursorPos + nFrames);
}

static void mainwindow_OnMixPaste(GtkMenuItem *pMenuItem, MainWindow *pMainWindow)
{
    if (pMainWindow->pDocument == NULL)
    {
        mainwindow_SetChunk(pMainWindow, m_pClipboard);

        return;
    }

    if (gst_audio_info_is_equal(pMainWindow->pDocument->pChunk->pAudioInfo, m_pClipboard->pAudioInfo) == FALSE)
    {
        message_Warning(_("You cannot mix different sound formats"));

        return;
    }

    Chunk *pChunkPart = chunk_GetPart(pMainWindow->pDocument->pChunk, pMainWindow->pDocument->nCursorPos, m_pClipboard->nFrames);
    gint64 nPartFrames = pChunkPart->nFrames;
    Chunk *pChunkMixed = chunk_Mix(pChunkPart, m_pClipboard, pMainWindow);
    g_object_unref(pChunkPart);

    if (!pChunkMixed)
    {
        return;
    }

    Chunk *pChunk = chunk_ReplacePart(pMainWindow->pDocument->pChunk, pMainWindow->pDocument->nCursorPos, nPartFrames, pChunkMixed);
    g_object_unref(pChunkMixed);

    document_Update(pMainWindow->pDocument, pChunk, 0, 0);
    document_SetSelection(pMainWindow->pDocument, pMainWindow->pDocument->nCursorPos, pMainWindow->pDocument->nCursorPos + m_pClipboard->nFrames);
}

static void mainwindow_OnPasteToNew(GtkMenuItem *pMenuItem, MainWindow *pMainwindow)
{
    g_object_ref(m_pClipboard);
    mainwindow_SetChunk(pMainwindow, m_pClipboard);
}

static void mainwindow_OnDelete(GtkMenuItem *pMenuItem, MainWindow *pMainwindow)
{
    gint32 nSelFrames = pMainwindow->pDocument->nSelEnd - pMainwindow->pDocument->nSelStart;
    Chunk *pChunk = chunk_RemovePart(pMainwindow->pDocument->pChunk, pMainwindow->pDocument->nSelStart, nSelFrames);
    document_Update(pMainwindow->pDocument, pChunk, pMainwindow->pDocument->nSelStart, -nSelFrames);
}

static void mainwindow_OnZoomIn(GtkMenuItem *pMenuItem, gpointer pUserData)
{
    document_Zoom(OE_MAINWINDOW(pUserData)->pDocument, 2.0, TRUE);
}

static void mainwindow_OnZoomOut(GtkMenuItem *pMenuItem, gpointer pUserData)
{
    document_Zoom(OE_MAINWINDOW(pUserData)->pDocument, 0.5, TRUE);
}

static void mainwindow_OnStop(GtkMenuItem *pMenuItem, MainWindow *pMainWindow)
{
    document_Stop(pMainWindow->pDocument);
}

gboolean mainwindow_UpdateCaches()
{
    static guint nLast = 0;
    MainWindow *pMainWindow;
    guint nMainWindows = g_list_length(g_lMainWindows);

    if (nMainWindows == 0)
    {
        return FALSE;
    }

    if (nLast >= nMainWindows)
    {
        nLast = 0;
    }

    guint nCurrent = nLast + 1;

    while (1)
    {
        if (nCurrent >= nMainWindows)
        {
            nCurrent = 0;
        }

        pMainWindow = OE_MAINWINDOW(g_list_nth_data(g_lMainWindows, nCurrent));

        if (chunkview_UpdateCache(pMainWindow->pChunkView))
        {
            nLast = nCurrent;

            return TRUE;
        }

        if (nCurrent == nLast)
        {
            return FALSE;
        }

        nCurrent++;
    }
}

static void mainwindow_OnPlaySelection(GtkMenuItem *pMenuItem, MainWindow *pMainWindow)
{
    document_PlaySelection(pMainWindow->pDocument);
}

static void mainwindow_OnPlayAll(GtkMenuItem *pMenuItem, MainWindow *pMainWindow)
{
    mainwindow_Play(pMainWindow, 0, pMainWindow->pDocument->pChunk->nFrames);
}

static void mainwindow_OnPlay(GtkMenuItem *pMenuItem, MainWindow *pMainWindow)
{
    mainwindow_Play(pMainWindow, pMainWindow->pDocument->nCursorPos, pMainWindow->pDocument->pChunk->nFrames);
}

static Chunk *mainwindow_FadeIn(Chunk *pChunk, MainWindow *pMainWindow)
{
    return chunk_Fade(pChunk, 0.0, 1.0, pMainWindow);
}

static Chunk *mainwindow_FadeOut(Chunk *pChunk, MainWindow *pMainWindow)
{
    return chunk_Fade(pChunk, 1.0, 0.0, pMainWindow);
}

static void mainwindow_OnAboutResponse(GtkDialog *pDialog, gint nResponse, gpointer pUserData)
{
    gboolean *bResponded = (gboolean *)pUserData;
    *bResponded = TRUE;
}

static void mainwindow_OnAbout(GtkMenuItem *pMenuItem, MainWindow *pMainWindow)
{
    static const gchar *sAuthors = "Robert Tari <robert@tari.in>";
    GtkAboutDialog *pAboutDialog = GTK_ABOUT_DIALOG(gtk_about_dialog_new());
    gtk_window_set_transient_for(GTK_WINDOW(pAboutDialog), GTK_WINDOW(g_pFocusedWindow));
    gtk_about_dialog_set_license_type(pAboutDialog, GTK_LICENSE_GPL_3_0);
    gtk_about_dialog_set_program_name(pAboutDialog, "Odio Edit");
    gchar *sYear = g_strndup (APPVERSION, 2);
    gchar *sCopyright = g_strdup_printf (_("Robert Tari 2019-20%s"), sYear);
    g_free (sYear);
    gtk_about_dialog_set_copyright(pAboutDialog, sCopyright);
    g_free (sCopyright);
    gtk_about_dialog_set_comments(pAboutDialog, _("A lightweight audio wave editor"));
    gtk_about_dialog_set_authors(pAboutDialog, &sAuthors);
    gtk_about_dialog_set_translator_credits(pAboutDialog, _("translator-credits"));
    gtk_about_dialog_set_version(pAboutDialog, APPVERSION);
    gtk_about_dialog_set_website(pAboutDialog, "https://tari.in/www/software/odio-edit/");
    gtk_about_dialog_set_website_label(pAboutDialog, "https://tari.in/www/software/odio-edit/");
    gtk_about_dialog_set_logo_icon_name(pAboutDialog, "odio-edit");
    gtk_window_set_position(GTK_WINDOW(pAboutDialog), GTK_WIN_POS_CENTER);
    gtk_window_set_modal(GTK_WINDOW(pAboutDialog), TRUE);

    gboolean bResponded = FALSE;

    g_signal_connect(pAboutDialog, "response", G_CALLBACK(mainwindow_OnAboutResponse), &bResponded);
    gtk_widget_show(GTK_WIDGET(pAboutDialog));

    while (!bResponded)
    {
        mainLoop();
    }

    gtk_widget_destroy(GTK_WIDGET(pAboutDialog));
}

static void mainwindow_OnFadeIn(GtkMenuItem *pMenuItem, MainWindow *pMainWindow)
{
    document_ApplyChunkFunc(pMainWindow->pDocument, mainwindow_FadeIn);
}

static void mainwindow_OnFadeOut(GtkMenuItem *pMenuItem, MainWindow *pMainWindow)
{
    document_ApplyChunkFunc(pMainWindow->pDocument, mainwindow_FadeOut);
}

static gboolean mainwindow_OnSelectAll(GtkAccelGroup *pAccelGroup, GObject *pObject, guint nKeyVal, GdkModifierType nModifierType, gpointer pUserData)
{
    MainWindow *pMainWindow = OE_MAINWINDOW(pUserData);

    if (pMainWindow->pDocument != NULL && pMainWindow->pDocument->pChunk != NULL)
    {
        if (pMainWindow->pDocument->nSelStart != 0 || pMainWindow->pDocument->nSelEnd != pMainWindow->pDocument->pChunk->nFrames)
        {
            document_SetSelection(pMainWindow->pDocument, 0, pMainWindow->pDocument->pChunk->nFrames);
        }
        else
        {
            document_SetSelection(pMainWindow->pDocument, 0, 0);
        }
    }

    return TRUE;
}

static void mainwindow_OnViewChanged(Document *pDocument, MainWindow *pMainWindow)
{
    gtk_adjustment_set_page_size(pMainWindow->pAdjustmentView, pDocument->nViewEnd - pDocument->nViewStart);
    gtk_adjustment_set_upper(pMainWindow->pAdjustmentView, pDocument->pChunk->nFrames);
    gtk_adjustment_set_value(pMainWindow->pAdjustmentView, pDocument->nViewStart);
    gtk_adjustment_set_step_increment(pMainWindow->pAdjustmentView, 16);
    gtk_adjustment_set_page_increment(pMainWindow->pAdjustmentView, gtk_adjustment_get_page_size(pMainWindow->pAdjustmentView) / 2);
    m_bZooming = TRUE;
    GtkAllocation pAllocation;
    gtk_widget_get_allocation(GTK_WIDGET(pMainWindow->pChunkView), &pAllocation);
    gfloat fCurrentSamp = pMainWindow->pDocument->nViewEnd - pMainWindow->pDocument->nViewStart;

    if (fCurrentSamp < GFLOAT(pAllocation.width))
    {
        if (fCurrentSamp < pMainWindow->pDocument->pChunk->nFrames)
        {
            gtk_adjustment_set_value(pMainWindow->pAdjustmentZoomH, 1.0);
            gtk_adjustment_set_page_size(pMainWindow->pAdjustmentZoomH, 0.2);
        }
        else
        {
            gtk_adjustment_set_value(pMainWindow->pAdjustmentZoomH, 0.0);
            gtk_adjustment_set_page_size(pMainWindow->pAdjustmentZoomH, 1.2);
        }
    }
    else
    {
        gtk_adjustment_set_value(pMainWindow->pAdjustmentZoomH, log(fCurrentSamp / GFLOAT(pMainWindow->pDocument->pChunk->nFrames)) / log(GFLOAT(pAllocation.width) / GFLOAT(pMainWindow->pDocument->pChunk->nFrames)));
        gtk_adjustment_set_page_size(pMainWindow->pAdjustmentZoomH, 0.2);
    }

    m_bZooming = FALSE;
    mainwindow_UpdateDesc(pMainWindow);
}

static void mainwindow_OnSelectionChanged(Document *pDocument, MainWindow *pMainWindow)
{
    mainwindow_UpdateDesc(pMainWindow);
    mainwindow_Enable(pMainWindow->lNeedSelectionItems, (pDocument->nSelStart != pDocument->nSelEnd));
}

static void mainwindow_OnCursorChanged(Document *pDocument, gboolean bRolling, MainWindow *pMainWindow)
{
    mainwindow_UpdateDesc(pMainWindow);
}

static void mainwindow_OnStateChanged(Document *pDocument, MainWindow *pMainWindow)
{
    mainwindow_FixTitle(pMainWindow);
    mainwindow_UpdateDesc(pMainWindow);
    mainwindow_Enable(pMainWindow->lNeedUndoItems, document_CanUndo(pDocument));
    mainwindow_Enable(pMainWindow->lNeedRedoItems, document_CanRedo(pDocument));
    mainwindow_Enable(pMainWindow->lNeedSelectionItems,(pDocument->nSelStart != pDocument->nSelEnd));
    mainwindow_OnViewChanged(pDocument, pMainWindow);
}

static void mainwindow_OnViewZoomChanged(GtkAdjustment *pAdjustment, MainWindow *pMainWindow)
{
    gint64 nStart = (gint64)gtk_adjustment_get_value(pAdjustment);
    gint64 nEnd = (gint64)(gtk_adjustment_get_value(pAdjustment) + gtk_adjustment_get_page_size(pAdjustment));

    if (nStart < 0)
    {
        nStart = 0;
    }

    if (nEnd <= nStart)
    {
        nEnd = nStart + 1;
    }
    else if (nEnd > pMainWindow->pDocument->pChunk->nFrames)
    {
        nEnd = pMainWindow->pDocument->pChunk->nFrames;
    }

    document_SetView(pMainWindow->pDocument, nStart, nEnd);
}

static void mainwindow_OnHorizZoomChanged(GtkAdjustment *pAdjustment, MainWindow *pMainWindow)
{
    if (m_bZooming)
    {
        return;
    }

    gfloat fCurrentSamp = pMainWindow->pDocument->nViewEnd - pMainWindow->pDocument->nViewStart;
    GtkAllocation pAllocation;
    gtk_widget_get_allocation(GTK_WIDGET(pMainWindow->pChunkView), &pAllocation);
    gfloat fTargetSamp = GFLOAT(pMainWindow->pDocument->pChunk->nFrames) * pow(GFLOAT(pAllocation.width) / GFLOAT(pMainWindow->pDocument->pChunk->nFrames), gtk_adjustment_get_value(pAdjustment));
    gboolean bFollowCursor = fTargetSamp <= fCurrentSamp;
    document_Zoom(pMainWindow->pDocument, fCurrentSamp / fTargetSamp, bFollowCursor);
}

static void mainwindow_OnVertZoomChanged(GtkAdjustment *pAdjustment, MainWindow *pMainWindow)
{
    chunkview_SetScale(pMainWindow->pChunkView, pow(100.0, gtk_adjustment_get_value(pAdjustment)));
}

static void mainwindow_OnDoubleClick(ChunkView *pChunkView, gint64 *nFrame, MainWindow *pMainWindow)
{
    document_SetSelection(pMainWindow->pDocument, 0, pMainWindow->pDocument->pChunk->nFrames);
}

static gint mainwindow_OnScalePress(GtkWidget *pWidget, GdkEventButton *pEventButton, MainWindow *pMainWindow)
{
    if (pEventButton->button == 3)
    {
        gtk_adjustment_set_value(pMainWindow->pAdjustmentZoomV, 0);

        return TRUE;
    }

    return FALSE;
}

static void mainwindow_init(MainWindow *pMainWindow)
{
    if (m_lstRecentFilenames == NULL)
    {
        g_assert(m_lstRecentFilenames == NULL);

        gchar **lstFiles = g_settings_get_strv(g_pGSettings, "recent-files");

        if (lstFiles != NULL)
        {
            for (guint i = 0; i < g_strv_length(lstFiles); i++)
            {
                m_lstRecentFilenames = g_list_append(m_lstRecentFilenames, lstFiles[i]);
            }
        }

        g_free(lstFiles);
    }

    pMainWindow->bCloseWhenDone = FALSE;
    pMainWindow->bSensitive = TRUE;
    pMainWindow->lNeedChunkItems = NULL;
    pMainWindow->lNeedSelectionItems = NULL;
    pMainWindow->lNeedClipboardItems = NULL;
    pMainWindow->lNeedUndoItems = NULL;
    pMainWindow->pAdjustmentView = GTK_ADJUSTMENT(gtk_adjustment_new(0, 0, 0, 0, 0, 0));
    g_signal_connect(pMainWindow->pAdjustmentView, "value-changed", G_CALLBACK(mainwindow_OnViewZoomChanged), pMainWindow);
    pMainWindow->pAdjustmentZoomH = GTK_ADJUSTMENT( gtk_adjustment_new(0, 0, 1.2, 0.01, 0.1, 0.2));
    g_signal_connect(pMainWindow->pAdjustmentZoomH, "value-changed", G_CALLBACK(mainwindow_OnHorizZoomChanged), pMainWindow);
    pMainWindow->pAdjustmentZoomV = GTK_ADJUSTMENT(gtk_adjustment_new(0, 0, 0.2 + log(MAINWINDOW_VZOOM_MAX) / log(100.0), 0.01, 0.1, 0.2));
    g_signal_connect(pMainWindow->pAdjustmentZoomV, "value-changed", G_CALLBACK(mainwindow_OnVertZoomChanged), pMainWindow);

    GtkAccelGroup *pAccelGroup = gtk_accel_group_new();
    pMainWindow->pToolBar = gtk_toolbar_new();
    GtkWidget *pMenu;
    GtkWidget *pMenuItem;
    GtkToolItem *pToolItem;
    GIcon *pIcon;
    GIcon *pIconThemed;
    GEmblem *pEmblem;
    gtk_window_add_accel_group(GTK_WINDOW(pMainWindow), pAccelGroup);
    gtk_accel_group_connect(pAccelGroup, GDK_KEY_A, GDK_CONTROL_MASK, 0, g_cclosure_new(G_CALLBACK(mainwindow_OnSelectAll), pMainWindow, 0));
    pMainWindow->pRecentMenu = gtk_menu_new();
    pMainWindow->pToolItemRecent = gtk_menu_tool_button_new(gtk_image_new_from_icon_name("document-open", GTK_ICON_SIZE_LARGE_TOOLBAR), _("Open"));
    gtk_widget_add_accelerator(GTK_WIDGET(pMainWindow->pToolItemRecent), "clicked", pAccelGroup, GDK_KEY_O, GDK_CONTROL_MASK, 0);
    gtk_tool_item_set_tooltip_text(pMainWindow->pToolItemRecent, _("Load new file [Ctrl+O]"));
    g_signal_connect(pMainWindow->pToolItemRecent, "clicked", G_CALLBACK(mainwindow_OnOpen), pMainWindow);
    gtk_toolbar_insert(GTK_TOOLBAR(pMainWindow->pToolBar), pMainWindow->pToolItemRecent, -1);

    pMenu = gtk_menu_new();
    pMenuItem = gtk_menu_item_new_with_label(_("Save as..."));
    g_signal_connect(pMenuItem, "activate", G_CALLBACK(mainwindow_OnSaveAs), pMainWindow);
    gtk_menu_shell_append(GTK_MENU_SHELL(pMenu), pMenuItem);
    mainwindow_AppendWidget(&pMainWindow->lNeedChunkItems, pMenuItem);
    pMenuItem = gtk_menu_item_new_with_label(_("Save selection as..."));
    g_signal_connect(pMenuItem, "activate", G_CALLBACK(mainwindow_OnSaveSelection), pMainWindow);
    gtk_widget_add_accelerator(pMenuItem, "activate", pAccelGroup, GDK_KEY_U, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);
    gtk_menu_shell_append(GTK_MENU_SHELL(pMenu), pMenuItem);
    mainwindow_AppendWidget(&pMainWindow->lNeedSelectionItems, pMenuItem);
    gtk_widget_show_all(pMenu);
    pToolItem = gtk_menu_tool_button_new(gtk_image_new_from_icon_name("document-save", GTK_ICON_SIZE_LARGE_TOOLBAR), _("Save"));
    gtk_widget_add_accelerator(GTK_WIDGET(pToolItem), "clicked", pAccelGroup, GDK_KEY_S, GDK_CONTROL_MASK, 0);
    gtk_tool_item_set_tooltip_text(pToolItem, _("Save current file [Ctrl+S]"));
    gtk_menu_tool_button_set_arrow_tooltip_text(GTK_MENU_TOOL_BUTTON(pToolItem), _("Click here for more options"));
    gtk_menu_tool_button_set_menu(GTK_MENU_TOOL_BUTTON(pToolItem), pMenu);
    g_signal_connect(pToolItem, "clicked", G_CALLBACK(mainwindow_OnSave), pMainWindow);
    gtk_toolbar_insert(GTK_TOOLBAR(pMainWindow->pToolBar), pToolItem, -1);
    mainwindow_AppendWidget(&pMainWindow->lNeedChunkItems, pToolItem);

    gtk_toolbar_insert(GTK_TOOLBAR(pMainWindow->pToolBar), gtk_separator_tool_item_new(), -1);

    pToolItem = gtk_tool_button_new(gtk_image_new_from_icon_name("edit-undo", GTK_ICON_SIZE_LARGE_TOOLBAR), _("Undo"));
    gtk_widget_add_accelerator(GTK_WIDGET(pToolItem), "clicked", pAccelGroup, GDK_KEY_Z, GDK_CONTROL_MASK, 0);
    gtk_tool_item_set_tooltip_text(pToolItem, _("Undo last change [Ctrl+Z]"));
    g_signal_connect(pToolItem, "clicked", G_CALLBACK(mainwindow_OnUndo), pMainWindow);
    gtk_toolbar_insert(GTK_TOOLBAR(pMainWindow->pToolBar), pToolItem, -1);
    mainwindow_AppendWidget(&pMainWindow->lNeedUndoItems, pToolItem);

    pToolItem = gtk_tool_button_new(gtk_image_new_from_icon_name("edit-redo", GTK_ICON_SIZE_LARGE_TOOLBAR), _("Redo"));
    gtk_widget_add_accelerator(GTK_WIDGET(pToolItem), "clicked", pAccelGroup, GDK_KEY_Z, GDK_CONTROL_MASK | GDK_SHIFT_MASK, 0);
    gtk_tool_item_set_tooltip_text(pToolItem, _("Redo last undo operation [Ctrl+Shift+Z]"));
    g_signal_connect(pToolItem, "clicked", G_CALLBACK(mainwindow_OnRedo), pMainWindow);
    gtk_toolbar_insert(GTK_TOOLBAR(pMainWindow->pToolBar), pToolItem, -1);
    mainwindow_AppendWidget(&pMainWindow->lNeedRedoItems, pToolItem);

    gtk_toolbar_insert(GTK_TOOLBAR(pMainWindow->pToolBar), gtk_separator_tool_item_new(), -1);

    pToolItem = gtk_tool_button_new(gtk_image_new_from_icon_name("edit-cut", GTK_ICON_SIZE_LARGE_TOOLBAR), _("Cut"));
    gtk_widget_add_accelerator(GTK_WIDGET(pToolItem), "clicked", pAccelGroup, GDK_KEY_X, GDK_CONTROL_MASK, 0);
    gtk_tool_item_set_tooltip_text(pToolItem, _("Remove and copy current selection [Ctrl+X]"));
    g_signal_connect(pToolItem, "clicked", G_CALLBACK(mainwindow_OnCut), pMainWindow);
    gtk_toolbar_insert(GTK_TOOLBAR(pMainWindow->pToolBar), pToolItem, -1);
    mainwindow_AppendWidget(&pMainWindow->lNeedSelectionItems, pToolItem);

    pToolItem = gtk_tool_button_new(gtk_image_new_from_icon_name("edit-copy", GTK_ICON_SIZE_LARGE_TOOLBAR), _("Copy"));
    gtk_widget_add_accelerator(GTK_WIDGET(pToolItem), "clicked", pAccelGroup, GDK_KEY_C, GDK_CONTROL_MASK, 0);
    gtk_tool_item_set_tooltip_text(pToolItem, _("Copy current selection [Ctrl+C]"));
    g_signal_connect(pToolItem, "clicked", G_CALLBACK(mainwindow_OnCopy), pMainWindow);
    gtk_toolbar_insert(GTK_TOOLBAR(pMainWindow->pToolBar), pToolItem, -1);
    mainwindow_AppendWidget(&pMainWindow->lNeedSelectionItems, pToolItem);

    pMenu = gtk_menu_new();
    pMenuItem = gtk_menu_item_new_with_label(_("Paste over"));
    g_signal_connect(pMenuItem, "activate", G_CALLBACK(mainwindow_OnPasteOver), pMainWindow);
    gtk_menu_shell_append(GTK_MENU_SHELL(pMenu), pMenuItem);
    mainwindow_AppendWidget(&pMainWindow->lNeedClipboardItems, pMenuItem);
    pMenuItem = gtk_menu_item_new_with_label(_("Paste mixed"));
    g_signal_connect(pMenuItem, "activate", G_CALLBACK(mainwindow_OnMixPaste), pMainWindow);
    gtk_menu_shell_append(GTK_MENU_SHELL(pMenu), pMenuItem);
    mainwindow_AppendWidget(&pMainWindow->lNeedClipboardItems, pMenuItem);
    pMenuItem = gtk_menu_item_new_with_label(_("Paste to new"));
    g_signal_connect(pMenuItem, "activate", G_CALLBACK(mainwindow_OnPasteToNew), pMainWindow);
    gtk_menu_shell_append(GTK_MENU_SHELL(pMenu), pMenuItem);
    mainwindow_AppendWidget(&pMainWindow->lNeedClipboardItems, pMenuItem);
    gtk_widget_show_all(pMenu);
    pToolItem = gtk_menu_tool_button_new(gtk_image_new_from_icon_name("edit-paste", GTK_ICON_SIZE_LARGE_TOOLBAR), _("Paste"));
    gtk_widget_add_accelerator(GTK_WIDGET(pToolItem), "clicked", pAccelGroup, GDK_KEY_V, GDK_CONTROL_MASK, 0);
    gtk_tool_item_set_tooltip_text(pToolItem, _("Paste at cursor position [Ctrl+V]"));
    gtk_menu_tool_button_set_arrow_tooltip_text(GTK_MENU_TOOL_BUTTON(pToolItem), _("Click here for more options"));
    gtk_menu_tool_button_set_menu(GTK_MENU_TOOL_BUTTON(pToolItem), pMenu);
    g_signal_connect(pToolItem, "clicked", G_CALLBACK(mainwindow_OnPaste), pMainWindow);
    gtk_toolbar_insert(GTK_TOOLBAR(pMainWindow->pToolBar), pToolItem, -1);
    mainwindow_AppendWidget(&pMainWindow->lNeedClipboardItems, pToolItem);

    pIconThemed = g_themed_icon_new_with_default_fallbacks("edit-cut");
    pEmblem = g_emblem_new(pIconThemed);
    g_object_unref(pIconThemed);
    pIconThemed = g_themed_icon_new_with_default_fallbacks("edit-delete");
    pIcon = g_emblemed_icon_new(pIconThemed, pEmblem);
    g_object_unref(pIconThemed);
    g_object_unref(pEmblem);
    pToolItem = gtk_tool_button_new(gtk_image_new_from_gicon(pIcon, GTK_ICON_SIZE_LARGE_TOOLBAR), _("Crop"));
    g_object_unref(pIcon);
    gtk_tool_item_set_tooltip_text(pToolItem, _("Crop to selection"));
    g_signal_connect(pToolItem, "clicked", G_CALLBACK(mainwindow_OnCrop), pMainWindow);
    gtk_toolbar_insert(GTK_TOOLBAR(pMainWindow->pToolBar), pToolItem, -1);
    mainwindow_AppendWidget(&pMainWindow->lNeedSelectionItems, pToolItem);

    pToolItem = gtk_tool_button_new(gtk_image_new_from_icon_name("edit-delete", GTK_ICON_SIZE_LARGE_TOOLBAR), _("Delete"));
    gtk_widget_add_accelerator(GTK_WIDGET(pToolItem), "clicked", pAccelGroup, GDK_KEY_Delete, 0, 0);
    gtk_tool_item_set_tooltip_text(pToolItem, _("Delete selection [Del]"));
    g_signal_connect(pToolItem, "clicked", G_CALLBACK(mainwindow_OnDelete), pMainWindow);
    gtk_toolbar_insert(GTK_TOOLBAR(pMainWindow->pToolBar), pToolItem, -1);
    mainwindow_AppendWidget(&pMainWindow->lNeedSelectionItems, pToolItem);

    gtk_toolbar_insert(GTK_TOOLBAR(pMainWindow->pToolBar), gtk_separator_tool_item_new(), -1);

    pMenu = gtk_menu_new();
    pMenuItem = gtk_menu_item_new_with_label(_("Play all"));
    g_signal_connect(pMenuItem, "activate", G_CALLBACK(mainwindow_OnPlayAll), pMainWindow);
    gtk_menu_shell_append(GTK_MENU_SHELL(pMenu), pMenuItem);
    mainwindow_AppendWidget(&pMainWindow->lNeedChunkItems, pMenuItem);
    pMenuItem = gtk_menu_item_new_with_label(_("Play from cursor position"));
    g_signal_connect(pMenuItem, "activate", G_CALLBACK(mainwindow_OnPlay), pMainWindow);
    gtk_menu_shell_append(GTK_MENU_SHELL(pMenu), pMenuItem);
    mainwindow_AppendWidget(&pMainWindow->lNeedChunkItems, pMenuItem);
    gtk_widget_show_all(pMenu);
    pToolItem = gtk_menu_tool_button_new(gtk_image_new_from_icon_name("media-playback-start", GTK_ICON_SIZE_LARGE_TOOLBAR), _("Play"));
    gtk_tool_item_set_tooltip_text(pToolItem, _("Play selection"));
    gtk_menu_tool_button_set_arrow_tooltip_text(GTK_MENU_TOOL_BUTTON(pToolItem), _("Click here for more options"));
    gtk_menu_tool_button_set_menu(GTK_MENU_TOOL_BUTTON(pToolItem), pMenu);
    g_signal_connect(pToolItem, "clicked", G_CALLBACK(mainwindow_OnPlaySelection), pMainWindow);
    gtk_toolbar_insert(GTK_TOOLBAR(pMainWindow->pToolBar), pToolItem, -1);
    mainwindow_AppendWidget(&pMainWindow->lNeedChunkItems, pToolItem);

    pToolItem = gtk_tool_button_new(gtk_image_new_from_icon_name("media-playback-stop", GTK_ICON_SIZE_LARGE_TOOLBAR), _("Stop"));
    gtk_tool_item_set_tooltip_text(pToolItem, _("Stop playback"));
    g_signal_connect(pToolItem, "clicked", G_CALLBACK(mainwindow_OnStop), pMainWindow);
    gtk_toolbar_insert(GTK_TOOLBAR(pMainWindow->pToolBar), pToolItem, -1);
    mainwindow_AppendWidget(&pMainWindow->lNeedChunkItems, pToolItem);

    gtk_toolbar_insert(GTK_TOOLBAR(pMainWindow->pToolBar), gtk_separator_tool_item_new(), -1);

    pIconThemed = g_themed_icon_new_with_default_fallbacks("go-up");
    pEmblem = g_emblem_new(pIconThemed);
    g_object_unref(pIconThemed);
    pIconThemed = g_themed_icon_new_with_default_fallbacks("audio-volume-high");
    pIcon = g_emblemed_icon_new(pIconThemed, pEmblem);
    g_object_unref(pIconThemed);
    g_object_unref(pEmblem);
    pToolItem = gtk_tool_button_new(gtk_image_new_from_gicon(pIcon, GTK_ICON_SIZE_LARGE_TOOLBAR), _("Fade in"));
    g_object_unref(pIcon);
    gtk_tool_item_set_tooltip_text(pToolItem, _("Fade in selection"));
    g_signal_connect(pToolItem, "clicked", G_CALLBACK(mainwindow_OnFadeIn), pMainWindow);
    gtk_toolbar_insert(GTK_TOOLBAR(pMainWindow->pToolBar), pToolItem, -1);
    mainwindow_AppendWidget(&pMainWindow->lNeedSelectionItems, pToolItem);

    pIconThemed = g_themed_icon_new_with_default_fallbacks("go-down");
    pEmblem = g_emblem_new(pIconThemed);
    g_object_unref(pIconThemed);
    pIconThemed = g_themed_icon_new_with_default_fallbacks("audio-volume-high");
    pIcon = g_emblemed_icon_new(pIconThemed, pEmblem);
    g_object_unref(pIconThemed);
    g_object_unref(pEmblem);
    pToolItem = gtk_tool_button_new(gtk_image_new_from_gicon(pIcon, GTK_ICON_SIZE_LARGE_TOOLBAR), _("Fade out"));
    g_object_unref(pIcon);
    gtk_tool_item_set_tooltip_text(pToolItem, _("Fade out selection"));
    g_signal_connect(pToolItem, "clicked", G_CALLBACK(mainwindow_OnFadeOut), pMainWindow);
    gtk_toolbar_insert(GTK_TOOLBAR(pMainWindow->pToolBar), pToolItem, -1);
    mainwindow_AppendWidget(&pMainWindow->lNeedSelectionItems, pToolItem);

    GtkToolItem *pSeparatorToolItem = gtk_separator_tool_item_new();
    gtk_toolbar_insert(GTK_TOOLBAR(pMainWindow->pToolBar), pSeparatorToolItem, -1);

    pToolItem = gtk_tool_button_new(gtk_image_new_from_icon_name("help-about", GTK_ICON_SIZE_LARGE_TOOLBAR), _("About"));
    gtk_tool_item_set_tooltip_text(pToolItem, _("About Odio Edit"));
    g_signal_connect(pToolItem, "clicked", G_CALLBACK(mainwindow_OnAbout), pMainWindow);
    gtk_toolbar_insert(GTK_TOOLBAR(pMainWindow->pToolBar), pToolItem, -1);

    gtk_toolbar_set_style(GTK_TOOLBAR(pMainWindow->pToolBar), GTK_TOOLBAR_BOTH);
    gtk_style_context_add_class(gtk_widget_get_style_context(pMainWindow->pToolBar), GTK_STYLE_CLASS_PRIMARY_TOOLBAR);
    mainwindow_UpdateRecentFiles(pMainWindow);

    gtk_widget_set_hexpand(pMainWindow->pToolBar, TRUE);
    pMainWindow->pChunkView = chunkview_new();
    gtk_widget_set_hexpand(GTK_WIDGET(pMainWindow->pChunkView), TRUE);
    gtk_widget_set_vexpand(GTK_WIDGET(pMainWindow->pChunkView), TRUE);
    g_signal_connect(pMainWindow->pChunkView, "double-click", G_CALLBACK(mainwindow_OnDoubleClick), pMainWindow);
    pMainWindow->pScale = gtk_scale_new(GTK_ORIENTATION_VERTICAL, pMainWindow->pAdjustmentZoomV);
    gtk_widget_set_vexpand(pMainWindow->pScale, TRUE);
    g_signal_connect(pMainWindow->pScale, "button_press_event", G_CALLBACK(mainwindow_OnScalePress), pMainWindow);
    gtk_scale_set_digits(GTK_SCALE(pMainWindow->pScale), 3);
    gtk_scale_set_draw_value (GTK_SCALE(pMainWindow->pScale), FALSE);
    gtk_widget_set_margin_start(pMainWindow->pScale, 5);
    gtk_widget_set_margin_end(pMainWindow->pScale, 5);
    gtk_widget_set_margin_top(pMainWindow->pScale, 5);
    mainwindow_AppendWidget(&pMainWindow->lNeedChunkItems, pMainWindow->pScale);

    gchar sText[256];
    g_snprintf(sText, 150, "<span font-weight=\"bold\">%s:</span> 00:00.000", _("Cursor"));
    pMainWindow->pLabelCursor = GTK_LABEL(gtk_widget_new(GTK_TYPE_LABEL, "use-markup", TRUE, "label", sText, "margin", 5, NULL));
    g_snprintf(sText, 150, "<span font-weight=\"bold\">%s:</span> 00:00.000 - 00:00.000", _("View"));
    pMainWindow->pLabelView = GTK_LABEL(gtk_widget_new(GTK_TYPE_LABEL, "use-markup", TRUE, "label", sText, "margin", 5, NULL));
    g_snprintf(sText, 150, "<span font-weight=\"bold\">%s:</span> 00:00.000 + 00:00.000", _("Selection"));
    pMainWindow->pLabelSel = GTK_LABEL(gtk_widget_new(GTK_TYPE_LABEL, "use-markup", TRUE, "label", sText, "margin", 5, NULL));
    pMainWindow->pProgressBar = GTK_PROGRESS_BAR(gtk_widget_new(GTK_TYPE_PROGRESS_BAR, "margin", 5, "hexpand", TRUE, NULL));
    gtk_progress_bar_set_show_text(pMainWindow->pProgressBar, TRUE);

    GtkCssProvider *pCssProvider = gtk_css_provider_new();
    GtkStyleContext *pStyleContext = gtk_widget_get_style_context(GTK_WIDGET(pMainWindow->pProgressBar));
    GValue cMinHeight = G_VALUE_INIT;
    GtkStateFlags nStateFlags = gtk_style_context_get_state(pStyleContext);
    gtk_style_context_get_property(pStyleContext, "min-height", nStateFlags, &cMinHeight);
    gint nMinHeight = g_value_get_int(&cMinHeight);
    gchar *sMinheight = "";

    if (nMinHeight == 0)
    {
        nMinHeight = 16;
        sMinheight = "progressbar progress{min-height: 16px} ";
    }

    gchar *sCSS = g_strdup_printf("%sprogressbar text {margin-bottom: -%ipx; font-size: %ipx; padding-top: 2px;}", sMinheight, 10 * nMinHeight, nMinHeight - 4);
    g_value_unset(&cMinHeight);
    gtk_style_context_add_provider(pStyleContext, GTK_STYLE_PROVIDER(pCssProvider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    gtk_css_provider_load_from_data(pCssProvider, sCSS, -1, NULL);
    g_free(sCSS);
    g_object_unref(pCssProvider);

    GtkWidget *pScrollbar = gtk_scrollbar_new(GTK_ORIENTATION_HORIZONTAL, pMainWindow->pAdjustmentView);
    GtkWidget *pGrid = gtk_grid_new();
    gtk_grid_attach(GTK_GRID(pGrid), pMainWindow->pToolBar, 0, 0, 2, 1);
    gtk_grid_attach(GTK_GRID(pGrid), GTK_WIDGET(pMainWindow->pChunkView), 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(pGrid), pScrollbar, 0, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(pGrid), pMainWindow->pScale, 1, 1, 1, 2);
    GtkGrid *pGridStatus = GTK_GRID(gtk_grid_new());
    gtk_grid_attach(pGridStatus, GTK_WIDGET(pMainWindow->pLabelCursor), 0, 0, 1, 1);
    gtk_grid_attach(pGridStatus, GTK_WIDGET(pMainWindow->pLabelView), 1, 0, 1, 1);
    gtk_grid_attach(pGridStatus, GTK_WIDGET(pMainWindow->pLabelSel), 2, 0, 1, 1);
    gtk_grid_attach(pGridStatus, GTK_WIDGET(pMainWindow->pProgressBar), 3, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(pGrid), GTK_WIDGET(pGridStatus), 0, 3, 2, 1);
    gtk_container_add(GTK_CONTAINER(pMainWindow), pGrid);
    gtk_widget_show_all(GTK_WIDGET(pMainWindow));

    mainwindow_Enable(pMainWindow->lNeedChunkItems, FALSE);
    mainwindow_Enable(pMainWindow->lNeedSelectionItems, FALSE);
    mainwindow_Enable(pMainWindow->lNeedClipboardItems, m_pClipboard != NULL);
    mainwindow_Enable(pMainWindow->lNeedUndoItems, FALSE);
    mainwindow_Enable(pMainWindow->lNeedRedoItems, FALSE);
    gtk_widget_set_size_request(GTK_WIDGET(pMainWindow), 640, 480);
    gtk_window_maximize(GTK_WINDOW(pMainWindow));

    GtkTargetEntry gte;
    gte.target = "text/uri-list";
    gte.flags = gte.info = 0;
    gtk_drag_dest_set(GTK_WIDGET(pMainWindow), GTK_DEST_DEFAULT_ALL, &gte, 1, GDK_ACTION_COPY | GDK_ACTION_MOVE);

    mainwindow_ResetStatusBar(pMainWindow);
    mainwindow_SetStatusBarInfo(pMainWindow, 0, FALSE, 0, 0, 0, 0, 0, 0);
    mainwindow_FixTitle(pMainWindow);

    /*
    {N_("/Edit/Silence selection"), NULL, edit_silence, 0, NULL},
    {N_("/Edit/Clear clipboard"), NULL, edit_clearclipboard, 0, NULL},
    {N_("/View/Zoom to _selection"), NULL, view_zoomtoselection, 0, NULL},
    {N_("/View/Zoom _all"), NULL, view_zoomall, 0, NULL},
    {N_("/Cursor/Set selection start"), "<control>Q",edit_selstartcursor, 0, NULL},
    {N_("/Cursor/Set selection end"), "<control>W",edit_selendcursor, 0, NULL},
    {N_("/Cursor/Move to beginning"), "<control>H", cursor_moveto_beginning, 0, NULL},
    {N_("/Cursor/Move to end"), "<control>J", cursor_moveto_end, 0, NULL},
    {N_("/Cursor/Move to selection start"), "<control>K", cursor_moveto_selstart, 0, NULL},
    {N_("/Cursor/Move to selection end"), "<control>L", cursor_moveto_selend, 0, NULL},
    */

    pMainWindow->bFollowMode = TRUE;
}

GtkWidget *mainwindow_new()
{
    return g_object_new(OE_TYPE_MAINWINDOW, NULL);
}

GtkWidget *mainwindow_NewWithFile(gchar *sFilePath)
{
    MainWindow *pMainWindow = OE_MAINWINDOW(mainwindow_new());
    gchar *sFilePathNew = file_Canonicalize(sFilePath, NULL);
    Document *pDocument = document_NewWithFile(sFilePathNew, pMainWindow);

    if (pDocument != NULL)
    {
        mainwindow_SetDocument(pMainWindow, pDocument, sFilePathNew);
        g_settings_set_string(g_pGSettings, "last-opened", sFilePathNew);
        mainwindow_AddRecentFile(sFilePathNew);
    }

    g_free(sFilePathNew);

    return GTK_WIDGET(pMainWindow);
}

void mainwindow_SetSensitive(MainWindow *pMainWindow, gboolean bSensitive)
{
    gtk_widget_set_sensitive(pMainWindow->pToolBar, bSensitive);
    gtk_widget_set_sensitive(GTK_WIDGET(pMainWindow->pChunkView), bSensitive);
    gtk_widget_set_sensitive(pMainWindow->pScale, bSensitive);
    pMainWindow->bSensitive = bSensitive;
}

void mainwindow_RepaintViews()
{
    for (GList *l = g_lMainWindows; l != NULL; l = l->next)
    {
        chunkview_ForceRepaint(OE_MAINWINDOW(l->data)->pChunkView);
    }
}

/*static Chunk *interp(Chunk *pChunk, MainWindow *pMainWindow)
{
    return chunk_InterpolateEndpoints(pChunk, pMainWindow);
}*/

/*
static void edit_silence(GtkMenuItem *pMenuItem, gpointer pUserData)
{
    document_ApplyChunkFunc(OE_MAINWINDOW(pUserData)->pDocument, interp);
}
*/

/*static void view_zoomtoselection(GtkMenuItem *pMenuItem, gpointer pUserData)
{
    Document *pDocument = OE_MAINWINDOW(pUserData)->pDocument;
    document_SetView(pDocument, pDocument->nSelStart, pDocument->nSelEnd);
}*/

/*static void view_zoomall(GtkMenuItem *pMenuItem, gpointer pUserData)
{
    Document *pDocument = OE_MAINWINDOW(pUserData)->pDocument;
    document_SetView(pDocument, 0, pDocument->pChunk->nFrames);
}*/

/*static void edit_selstartcursor(GtkMenuItem *pMenuItem, gpointer pUserData)
{
    MainWindow *pMainWindow = OE_MAINWINDOW(pUserData);

    if (pMainWindow->pDocument->nSelStart == pMainWindow->pDocument->nSelEnd)
    {
        document_SetSelection(pMainWindow->pDocument, pMainWindow->pDocument->nCursorPos, pMainWindow->pDocument->pChunk->nFrames);
    }
    else
    {
        document_SetSelection(pMainWindow->pDocument, pMainWindow->pDocument->nCursorPos, pMainWindow->pDocument->nSelEnd);
    }
}*/

/*static void edit_selendcursor(GtkMenuItem *pMenuItem, gpointer pUserData)
{
    MainWindow *pMainWindow = OE_MAINWINDOW(pUserData);

    if (pMainWindow->pDocument->nSelStart == pMainWindow->pDocument->nSelEnd)
    {
        document_SetSelection(pMainWindow->pDocument, 0, pMainWindow->pDocument->nCursorPos + 1);
    }
    else
    {
        document_SetSelection(pMainWindow->pDocument, pMainWindow->pDocument->nSelStart, pMainWindow->pDocument->nCursorPos);
    }
}*/

/*static void edit_clearclipboard(GtkMenuItem *pMenuItem, gpointer pUserData)
{
    g_info("chunk_unref: %d, edit_clearclipboard", chunk_AliveCount());
    g_object_unref(m_pClipboard);
    m_pClipboard = NULL;
    g_list_foreach(g_lMainWindows, (GFunc)mainwindow_UpdateClipboardItems, NULL);
}*/

/*static void cursor_moveto_beginning(GtkMenuItem *pMenuItem, gpointer pUserData)
{
    document_SetCursor(OE_MAINWINDOW(pUserData)->pDocument, 0);
}*/

/*static void cursor_moveto_end(GtkMenuItem *pMenuItem, gpointer pUserData)
{
    MainWindow *pMainWindow = OE_MAINWINDOW(pUserData);
    document_SetCursor(pMainWindow->pDocument, pMainWindow->pDocument->pChunk->nFrames);
}*/

/*static void cursor_moveto_selstart(GtkMenuItem *pMenuItem, gpointer pUserData)
{
    MainWindow *pMainWindow = OE_MAINWINDOW(pUserData);
    document_SetCursor(pMainWindow->pDocument, pMainWindow->pDocument->nSelStart);
}*/

/*static void cursor_moveto_selend(GtkMenuItem *pMenuItem, gpointer pUserData)
{
    MainWindow *pMainWindow = OE_MAINWINDOW(pUserData);
    document_SetCursor(pMainWindow->pDocument, pMainWindow->pDocument->nSelEnd);
}*/
