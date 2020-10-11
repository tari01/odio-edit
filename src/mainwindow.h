/*
    Copyright (C) 2019-2020, Robert Tari <robert@tari.in>
    Copyright (C) 2002 2003 2004 2005 2006 2011 2012, Magnus Hjorth

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

#ifndef MAINWINDOW_H_INCLUDED
#define MAINWINDOW_H_INCLUDED

#include "chunkview.h"

#define OE_TYPE_MAINWINDOW mainwindow_get_type()
G_DECLARE_FINAL_TYPE(MainWindow, mainwindow, OE, MAINWINDOW, GtkWindow)
#define MAINWINDOW_RECENT_MAX 10
#define MAINWINDOW_VZOOM_MAX 100

struct _MainWindow
{
    GtkWindow parent_instance;
    ChunkView *pChunkView;
    GtkAdjustment *pAdjustmentView;
    GtkAdjustment *pAdjustmentZoomH;
    GtkAdjustment *pAdjustmentZoomV;
    gboolean bSensitive;
    GtkWidget *pToolBar;
    GtkWidget *pScale;
    GList *lNeedChunkItems;
    GList *lNeedSelectionItems;
    GList *lNeedClipboardItems;
    GList *lNeedUndoItems;
    GList *lNeedRedoItems;
    Document *pDocument;
    gboolean bFollowMode;
    GtkWidget *pRecentMenu;
    GtkToolItem *pToolItemRecent;
    gboolean bCloseWhenDone;
    GtkLabel *pLabelCursor;
    GtkLabel *pLabelView;
    GtkLabel *pLabelSel;
    GtkProgressBar* pProgressBar;
    gboolean bStatusBarRolling;
    gboolean bStatusBarWorking;
    gboolean bStatusBarBreak;
    gint64 nStatusBarCursorPos;
    gint64 nStatusBarViewStart;
    gint64 nStatusBarViewEnd;
    gint64 nStatusBarSelStart;
    gint64 nStatusBarSelEnd;
    gint64 nStatusBarTimeLast;
};

extern GList *g_lMainWindows;
extern MainWindow *g_pFocusedWindow;
extern GtkFileFilter *g_pFileFilter;
extern GtkFileFilter *g_pFileFilterWav;
extern guint m_nStatusBarsWorking;

GtkWidget *mainwindow_new();
GtkWidget *mainwindow_NewWithFile(gchar *sFilePath);
gboolean mainwindow_UpdateCaches();
void mainwindow_RepaintViews();
void mainwindow_SetSensitive(MainWindow *pMainWindow, gboolean bSensitive);
gboolean mainwindow_Progress(MainWindow *pMainWindow, gfloat fProgress);
void mainwindow_EndProgress(MainWindow *pMainWindow);
void mainwindow_BeginProgress(MainWindow *pMainWindow, gchar *sDescription);

#endif
