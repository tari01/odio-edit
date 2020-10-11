/*
    Copyright (C) 2019-2020, Robert Tari <robert@tari.in>
    Copyright (C) 2005 2006, Magnus Hjorth

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

#ifndef DOCUMENT_H_INCLUDED
#define DOCUMENT_H_INCLUDED

#include "chunk.h"

#define OE_TYPE_DOCUMENT document_get_type()
G_DECLARE_FINAL_TYPE(Document, document, OE, DOCUMENT, GObject)

struct _MainWindow;

struct _Document
{
    GObject parent_instance;
    gchar *sFilePath;
    gchar *sTitleName;
    guint sTitleSerial;
    struct HistoryEntry *pHistoryEntry;
    Chunk *pChunk;
    gint64 nViewStart;
    gint64 nViewEnd;
    gint64 nSelStart;
    gint64 nSelEnd;
    gint64 nCursorPos;
    gint64 nOldSelStart;
    gint64 nOldSelEnd;
    gint64 nOldCursorPos;
    gboolean bFollowMode;
    struct _MainWindow *pMainWindow;
};

struct HistoryEntry;
struct HistoryEntry
{
    Chunk *pChunk;
    gint64 nSelStart;
    gint64 nSelEnd;
    gint64 nViewStart;
    gint64 nViewEnd;
    gint64 nCursorPos;
    struct HistoryEntry *pHistoryEntryNext;
    struct HistoryEntry *pHistoryEntryPrev;
};

extern GList *g_lDocuments;
extern Document *g_pPlayingDocument;

typedef Chunk *(*ChunkFunc)(Chunk *pChunk, struct _MainWindow *pMainWindow);
Document *document_NewWithFile(gchar *sFilePath, struct _MainWindow *pMainWindow);
Document *document_NewWithChunk(Chunk *pChunk, gchar *sSourceName, struct _MainWindow *pMainWindow);
void document_SetMainWindow(Document *pDocument, struct _MainWindow *pMainWindow);
gboolean document_Save(Document *pDocument, gchar *sFilePath);
void document_Play(Document *pDocument, gint64 nStart, gint64 nEnd);
void document_PlaySelection(Document *pDocument);
void document_Stop(Document *pDocument);
void document_Update(Document *pDocument, Chunk *pChunk, gint64 nMoveStart, gint64 nMoveDist);
gboolean document_ApplyChunkFunc(Document *pDocument, ChunkFunc pChunkFunc);
void document_SetFollowMode(Document *pDocument, gboolean bFollowMode);
void document_SetCursor(Document *pDocument, gint64 nCursorPos);
void document_SetView(Document *pDocument, gint64 nViewStart, gint64 nViewEnd);
void document_Scroll(Document *pDocument, gint64 nDistance);
void document_SetSelection(Document *pDocument, gint64 nSelStart, gint64 nSelEnd);
void document_Zoom(Document *pDocument, gfloat fZoom, gboolean bFollowCursor);
gboolean document_CanUndo(Document *pDocument);
void document_Undo(Document *pDocument);
gboolean document_CanRedo(Document *pDocument);
void document_Redo(Document *pDocument);

#endif
