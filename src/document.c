/*
    Copyright (C) 2019-2021, Robert Tari <robert@tari.in>
    Copyright (C) 2005 2006 2007 2010 2011 2012, Magnus Hjorth

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
#include "document.h"
#include "player.h"
#include "main.h"

G_DEFINE_TYPE(Document, document, G_TYPE_OBJECT)

enum {VIEW_CHANGED_SIGNAL, SELECTION_CHANGED_SIGNAL, CURSOR_CHANGED_SIGNAL, STATE_CHANGED_SIGNAL, LAST_SIGNAL};

static void document_SetCursorMain(Document *pDocument, gint64 nCursorPos, gboolean bPlaySlave, gboolean bRunning);
static guint m_lDocumentSignals[LAST_SIGNAL] = {0};
static guint m_nUntitledCount = 0;

GList *g_lDocuments = NULL;
Document *g_pPlayingDocument = NULL;

static void document_ClearHistory(struct HistoryEntry *pHistoryEntry)
{
    if (pHistoryEntry != NULL)
    {
        while (pHistoryEntry->pHistoryEntryNext != NULL)
        {
            pHistoryEntry = pHistoryEntry->pHistoryEntryNext;
        }

        while (pHistoryEntry != NULL)
        {
            if (pHistoryEntry->pChunk != NULL)
            {
                g_info("chunk_unref: %d, document:document_ClearHistory %p", chunk_AliveCount(), pHistoryEntry->pChunk);
                g_object_unref(pHistoryEntry->pChunk);
                pHistoryEntry->pChunk = NULL;
            }

            struct HistoryEntry *pHistoryEntryNew = pHistoryEntry->pHistoryEntryPrev;
            g_free(pHistoryEntry);
            pHistoryEntry = pHistoryEntryNew;
        }
    }
}

static void document_OnDispose(GObject *pObject)
{
    g_info("document_destroy");
    Document *pDocument = OE_DOCUMENT(pObject);

    if (pDocument->sFilePath != NULL)
    {
        g_free(pDocument->sFilePath);
        pDocument->sFilePath = NULL;
    }
    
    if (pDocument->sTitleName != NULL)
    {
        g_free(pDocument->sTitleName);
        pDocument->sTitleName = NULL;
    }
    
    if (pDocument->pChunk != NULL)
    {
        g_info("chunk_unref: %d, document:document_OnDispose %p", chunk_AliveCount(), pDocument->pChunk);
        g_object_unref(pDocument->pChunk);
        pDocument->pChunk = NULL;
    }
    
    document_ClearHistory(pDocument->pHistoryEntry);
    pDocument->pHistoryEntry = NULL;
    g_lDocuments = g_list_remove(g_lDocuments, pObject);
    G_OBJECT_CLASS(document_parent_class)->dispose(pObject);
}

static void document_class_init(DocumentClass *cls)
{
    G_OBJECT_CLASS(cls)->dispose = document_OnDispose;
    m_lDocumentSignals[VIEW_CHANGED_SIGNAL] = g_signal_new("view_changed", G_TYPE_FROM_CLASS(cls), G_SIGNAL_RUN_FIRST, 0, NULL, NULL, NULL, G_TYPE_NONE, 0);
    m_lDocumentSignals[SELECTION_CHANGED_SIGNAL] = g_signal_new("selection_changed", G_TYPE_FROM_CLASS(cls), G_SIGNAL_RUN_FIRST, 0, NULL, NULL, NULL, G_TYPE_NONE, 0);
    m_lDocumentSignals[CURSOR_CHANGED_SIGNAL] = g_signal_new("cursor_changed", G_TYPE_FROM_CLASS(cls), G_SIGNAL_RUN_FIRST, 0, NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_BOOLEAN);
    m_lDocumentSignals[STATE_CHANGED_SIGNAL] = g_signal_new("status_changed", G_TYPE_FROM_CLASS(cls), G_SIGNAL_RUN_FIRST, 0, NULL, NULL, NULL, G_TYPE_NONE, 0);
}

static void document_init(Document *pDocument)
{
    pDocument->sFilePath = NULL;
    pDocument->sTitleName = NULL;
    pDocument->sTitleSerial = 0;
    pDocument->pHistoryEntry = NULL;
    pDocument->pChunk = NULL;
    pDocument->nViewStart = 0;
    pDocument->nViewEnd = 0;
    pDocument->nSelStart = 0;
    pDocument->nSelEnd = 0;
    pDocument->nCursorPos = 0;
}

static Document *document_new()
{
    g_info("document_new");
    return g_object_new(OE_TYPE_DOCUMENT, NULL);
}

Document *document_NewWithFile(gchar *sFilePath, struct _MainWindow *pMainWindow)
{
    Chunk *pChunk = chunk_Load(sFilePath, pMainWindow);

    if (pChunk == NULL)
    {
        return NULL;
    }

    Document *pDocument = document_NewWithChunk(pChunk, sFilePath, pMainWindow);
    
    return pDocument;
}

static void document_SetFilename(Document *pDocument, gchar *sFilePath)
{
    if ((sFilePath == pDocument->sFilePath && sFilePath != NULL) || (pDocument->sFilePath != NULL && sFilePath != NULL && !strcmp(pDocument->sFilePath, sFilePath)))
    {
        return;
    }
    
    g_free(pDocument->sTitleName);
    g_free(pDocument->sFilePath);
    pDocument->sFilePath = g_strdup(sFilePath);

    if (sFilePath == NULL)
    {
        pDocument->sTitleName = g_strdup_printf(_("Untitled %d"), ++m_nUntitledCount);
    }
    else
    {
        guint sTitleSerial = 0;
        
        for (GList *l = g_lDocuments; l != NULL; l = l->next)
        {
            Document *pDocumentIter = OE_DOCUMENT(l->data);

            if (pDocumentIter == pDocument)
            {
                continue;
            }
            
            if (pDocument->sFilePath == NULL || pDocumentIter->sFilePath == NULL)
            {
                continue;
            }
            
            gchar *sBaseNameDoc = g_path_get_basename(pDocument->sFilePath);
            gchar *sBaseNameIter = g_path_get_basename(pDocumentIter->sFilePath);
            guint nDiff = strcmp(sBaseNameDoc, sBaseNameIter);
            g_free(sBaseNameDoc);
            g_free(sBaseNameIter);            
            
            if (nDiff)
            {
                continue;
            }

            if (pDocumentIter->sTitleSerial > sTitleSerial)
            {
                sTitleSerial = pDocumentIter->sTitleSerial;
            }
        }
        
        pDocument->sTitleSerial = sTitleSerial + 1;
        gchar *sBaseName = g_path_get_basename(pDocument->sFilePath);
        
        if (pDocument->sTitleSerial > 1)
        {
            pDocument->sTitleName = g_strdup_printf("%s %d", sBaseName, pDocument->sTitleSerial);
            
        }
        else
        {
            pDocument->sTitleName = g_strdup(sBaseName);
        }
        
        g_free(sBaseName);
    }
}

Document *document_NewWithChunk(Chunk *pChunk, gchar *sSourceName, struct _MainWindow *pMainWindow)
{
    Document *pDocument = document_new();
    document_SetFilename(pDocument, sSourceName);
    pDocument->pChunk = pChunk;
    pDocument->nViewStart = 0;
    pDocument->nViewEnd = pChunk->nFrames;
    pDocument->pMainWindow = pMainWindow;
    g_lDocuments = g_list_append(g_lDocuments, pDocument);
    
    return pDocument;
}

gboolean document_Save(Document *pDocument, gchar *sFilePath)
{
    gboolean bError = chunk_Save(pDocument->pChunk, sFilePath, pDocument->pMainWindow);

    if (!bError)
    {
        document_ClearHistory(pDocument->pHistoryEntry);
        pDocument->pHistoryEntry = NULL;
        document_SetFilename(pDocument, sFilePath);
        g_signal_emit(pDocument, m_lDocumentSignals[STATE_CHANGED_SIGNAL], 0);
    }

    return bError;
}

static void document_OnCursorChanged(gint nPos, gboolean bIsRunning)
{
    Document *pDocument = g_pPlayingDocument;

    if (g_pPlayingDocument != NULL)
    {
        if (!bIsRunning)
        {
            g_pPlayingDocument = NULL;
        }
        
        if (nPos == -1)
        {
            nPos = pDocument->nSelStart;
        }
        
        document_SetCursorMain(pDocument, nPos, TRUE, bIsRunning);

        if (!bIsRunning)
        {
            g_info("document_unref, document:document_OnCursorChanged %p", pDocument);
            g_object_unref(pDocument);
        }
    }
}

void document_Play(Document *pDocument, gint64 nStart, gint64 nEnd)
{
    g_assert(pDocument != NULL);

    gint64 nPos = player_GetPos();
    
    if (player_Playing() && g_pPlayingDocument == pDocument && nStart == pDocument->nCursorPos && pDocument->nCursorPos < nPos)
    {
        player_ChangeRange(nPos, nEnd);
    }
    else
    {
        player_Stop();
        
        g_assert(g_pPlayingDocument == NULL);
        
        g_pPlayingDocument = pDocument;
        g_info("document_ref, document:document_Play %p", pDocument);
        g_object_ref(pDocument);

        if (player_Play(pDocument->pChunk, nStart, nEnd, document_OnCursorChanged))
        {
            g_pPlayingDocument = NULL;
            g_info("document_unref, document:document_Play %p", pDocument);
            g_object_unref(pDocument);
        }
    }
}

void document_PlaySelection(Document *pDocument)
{
    g_assert(pDocument != NULL);

    if (pDocument->nSelStart != pDocument->nSelEnd)
    {
        document_Play(pDocument, pDocument->nSelStart, pDocument->nSelEnd);
    }
    else
    {
        document_Play(pDocument, 0, pDocument->pChunk->nFrames);
    }
}

void document_Stop(Document *pDocument)
{
    g_assert(pDocument != NULL);

    if (pDocument != g_pPlayingDocument)
    {
        return;
    }
    
    player_Stop();
    
    g_assert(g_pPlayingDocument == NULL);
}

static void document_FixHistory(Document *pDocument)
{
    struct HistoryEntry *pHistoryEntry;

    if (pDocument->pHistoryEntry == NULL || pDocument->pChunk != pDocument->pHistoryEntry->pChunk || pDocument->nSelStart != pDocument->pHistoryEntry->nSelStart || pDocument->nSelEnd != pDocument->pHistoryEntry->nSelEnd)
    {
        pHistoryEntry = g_malloc0(sizeof(*pHistoryEntry));
        pHistoryEntry->pChunk = pDocument->pChunk;
        g_info("chunk_ref %d, document:document_FixHistory %p", chunk_AliveCount(), pDocument->pChunk);
        g_object_ref(pDocument->pChunk);
        pHistoryEntry->nSelStart = pDocument->nSelStart;
        pHistoryEntry->nSelEnd = pDocument->nSelEnd;
        pHistoryEntry->nViewStart = pDocument->nViewStart;
        pHistoryEntry->nViewEnd = pDocument->nViewEnd;
        pHistoryEntry->nCursorPos = pDocument->nCursorPos;

        if (pDocument->pHistoryEntry == NULL)
        {
            pHistoryEntry->pHistoryEntryPrev = pHistoryEntry->pHistoryEntryNext = NULL;
            pDocument->pHistoryEntry = pHistoryEntry;
        }
        else
        {
            pHistoryEntry->pHistoryEntryPrev = pDocument->pHistoryEntry;
            pHistoryEntry->pHistoryEntryNext = pDocument->pHistoryEntry->pHistoryEntryNext;

            if (pHistoryEntry->pHistoryEntryNext != NULL)
            {
                pHistoryEntry->pHistoryEntryNext->pHistoryEntryPrev = pHistoryEntry;
            }
            
            pDocument->pHistoryEntry->pHistoryEntryNext = pHistoryEntry;
            pDocument->pHistoryEntry = pHistoryEntry;
        }
    }
}

void document_Update(Document *pDocument, Chunk *pChunk, gint64 nMoveStart, gint64 nMoveDist)
{
    if (pChunk == NULL)
    {
        return;
    }

    document_FixHistory(pDocument);

    if (pDocument->pHistoryEntry->pHistoryEntryNext != NULL)
    {
        pDocument->pHistoryEntry->pHistoryEntryNext->pHistoryEntryPrev = NULL;
        document_ClearHistory(pDocument->pHistoryEntry->pHistoryEntryNext);
        pDocument->pHistoryEntry->pHistoryEntryNext = NULL;
    }

    if ((pDocument->pChunk->pAudioInfo->channels != pChunk->pAudioInfo->channels || pDocument->pChunk->pAudioInfo->rate != pChunk->pAudioInfo->rate) && g_pPlayingDocument == pDocument)
    {
        player_Stop();
    }

    g_info("chunk_unref: %d, document:document_Update %p", chunk_AliveCount(), pDocument->pChunk);
    g_object_unref(pDocument->pChunk);
    pDocument->pChunk = pChunk;

    if ((pDocument->nViewEnd - pDocument->nViewStart) >= pChunk->nFrames)
    {
        pDocument->nViewStart = 0;
        pDocument->nViewEnd = pChunk->nFrames;
    }
    else if (pDocument->nViewEnd > pChunk->nFrames)
    {
        pDocument->nViewStart -= (pDocument->nViewEnd - pChunk->nFrames);
        pDocument->nViewEnd = pChunk->nFrames;
    }

    if (pDocument->nSelStart >= nMoveStart)
    {
        pDocument->nSelStart += nMoveDist;

        if (pDocument->nSelStart < nMoveStart)
        {
            pDocument->nSelStart = nMoveStart;
        }
        
        pDocument->nSelEnd += nMoveDist;

        if (pDocument->nSelEnd < nMoveStart)
        {
            pDocument->nSelEnd = nMoveStart;
        }
    }
    
    if (pDocument->nSelStart >= pChunk->nFrames)
    {
        pDocument->nSelStart = pDocument->nSelEnd = 0;
    }
    else if (pDocument->nSelEnd >= pChunk->nFrames)
    {
        pDocument->nSelEnd = pChunk->nFrames;
    }

    if (g_pPlayingDocument == pDocument && player_Playing())
    {
        player_Switch(pChunk, nMoveStart, nMoveDist);
    }
    else
    {
        if (pDocument->nCursorPos >= nMoveStart)
        {
            pDocument->nCursorPos += nMoveDist;

            if (pDocument->nCursorPos < nMoveStart)
            {
                pDocument->nCursorPos = nMoveStart;
            }
        }
        
        if (pDocument->nCursorPos > pChunk->nFrames)
        {
            pDocument->nCursorPos = pChunk->nFrames;
        }
    }

    document_FixHistory(pDocument);
    g_signal_emit(pDocument, m_lDocumentSignals[STATE_CHANGED_SIGNAL], 0);
}

gboolean document_ApplyChunkFunc(Document *pDocument, ChunkFunc pChunkFunc)
{
    if ((pDocument->nSelStart == pDocument->nSelEnd) || (pDocument->nSelStart == 0 && pDocument->nSelEnd >= pDocument->pChunk->nFrames))
    {
        Chunk *pChunk = pChunkFunc(pDocument->pChunk, pDocument->pMainWindow);

        if (pChunk)
        {
            document_Update(pDocument, pChunk, 0, 0);
            
            return FALSE;
        }
        else
        {
            return TRUE;
        }
    }
    else
    {
        Chunk *pChunkPart = chunk_GetPart(pDocument->pChunk, pDocument->nSelStart, pDocument->nSelEnd - pDocument->nSelStart);        
        gint64 nPartFrames = pChunkPart->nFrames;
        Chunk *pChunkApplied = pChunkFunc(pChunkPart, pDocument->pMainWindow);
        g_object_unref(pChunkPart);
                
        if (pChunkApplied)
        {
            gint64 nAppliedFrames = pChunkApplied->nFrames;
            Chunk *pChunk = chunk_ReplacePart(pDocument->pChunk, pDocument->nSelStart, nPartFrames, pChunkApplied);
            g_object_unref(pChunkApplied);
    
            if (nAppliedFrames > nPartFrames)
            {
                document_Update(pDocument, pChunk, pDocument->nSelStart + nPartFrames, nAppliedFrames - nPartFrames);
            }
            else
            {
                document_Update(pDocument, pChunk, pDocument->nSelStart + nAppliedFrames, nAppliedFrames - nPartFrames);
            }
            
            return FALSE;
        }
        else
        {
            return TRUE;
        }
    }
}

static void document_SetCursorMain(Document *pDocument, gint64 nCursorPos, gboolean bPlaySlave, gboolean bRunning)
{
    g_assert(nCursorPos >= 0 && nCursorPos <= pDocument->pChunk->nFrames);

    if (pDocument->nCursorPos == nCursorPos)
    {
        return;
    }

    if (!bPlaySlave && g_pPlayingDocument == pDocument && player_Playing())
    {
        player_SetPos(nCursorPos);
        
        return;
    }

    if (pDocument->bFollowMode && bPlaySlave)
    {
        gint64 nViewStart;
        gint64 nViewEnd;
        gint64 nDist = pDocument->nViewEnd - pDocument->nViewStart;

        if (pDocument->nCursorPos < pDocument->nViewEnd && nCursorPos > pDocument->nViewEnd && nCursorPos < pDocument->nViewEnd + nDist)
        {
            nViewStart = pDocument->nViewEnd;
            nViewEnd = pDocument->nViewEnd + nDist;
        }
        else if (nCursorPos >= pDocument->nViewStart && nCursorPos < pDocument->nViewEnd)
        {
            nViewStart = pDocument->nViewStart;
            nViewEnd = pDocument->nViewEnd;
        }
        else
        {
            nViewStart = nCursorPos - nDist / 2;
            nViewEnd = nViewStart + nDist;
        }

        if (nViewStart < 0)
        {
            nViewEnd -= nViewStart;
            nViewStart = 0;
        }
        else if (nViewEnd > pDocument->pChunk->nFrames)
        {
            nViewEnd = pDocument->pChunk->nFrames;
            nViewStart = nViewEnd - nDist;
        }
        
        document_SetView(pDocument, nViewStart, nViewEnd);
    }

    pDocument->nOldCursorPos = pDocument->nCursorPos;
    pDocument->nCursorPos = nCursorPos;
    g_signal_emit(pDocument, m_lDocumentSignals[CURSOR_CHANGED_SIGNAL], 0, bRunning);
}

void document_SetCursor(Document *pDocument, gint64 nCursorPos)
{
    document_SetCursorMain(pDocument, nCursorPos, FALSE, (g_pPlayingDocument == pDocument && player_Playing()));
}

void document_SetFollowMode(Document *pDocument, gboolean bFollowMode)
{
    pDocument->bFollowMode = bFollowMode;
}

void document_SetView(Document *pDocument, gint64 nViewStart, gint64 nViewEnd)
{
    gint64 nViewEndNew;

    if (nViewStart > nViewEnd)
    {
        nViewEndNew = nViewStart;
        nViewStart = nViewEnd;
        nViewEnd = nViewEndNew;
    }
    
    g_assert(nViewStart >= 0 && (nViewStart < pDocument->pChunk->nFrames || pDocument->pChunk->nFrames == 0) && nViewEnd >= 0 && nViewEnd <= pDocument->pChunk->nFrames);

    if (pDocument->nViewStart != nViewStart || pDocument->nViewEnd != nViewEnd)
    {
        pDocument->nViewStart = nViewStart;
        pDocument->nViewEnd = nViewEnd;
        g_signal_emit(pDocument, m_lDocumentSignals[VIEW_CHANGED_SIGNAL], 0);
    }
}

void document_Scroll(Document *pDocument, gint64 nDistance)
{
    if (pDocument->nViewStart + nDistance < 0)
    {
        nDistance = -pDocument->nViewStart;
    }
    else if (pDocument->nViewEnd + nDistance > pDocument->pChunk->nFrames)
    {
        nDistance = pDocument->pChunk->nFrames - pDocument->nViewEnd;
    }
    
    document_SetView(pDocument, pDocument->nViewStart + nDistance, pDocument->nViewEnd + nDistance);
}

void document_SetSelection(Document *pDocument, gint64 nSelStart, gint64 nSelEnd)
{
    gint64 nSelEndNew;

    if (nSelStart > nSelEnd)
    {
        nSelEndNew = nSelStart;
        nSelStart = nSelEnd;
        nSelEnd = nSelEndNew;
    }
    
    if (nSelStart == nSelEnd)
    {
        nSelStart = 0;
        nSelEnd = 0;
    }

    if (pDocument->pChunk->nFrames == 0 && pDocument->nSelStart == 0 && pDocument->nSelEnd == 0)
    {
        return;
    }

    g_assert(nSelStart >= 0 && nSelStart < pDocument->pChunk->nFrames && nSelEnd >= 0 && nSelEnd <= pDocument->pChunk->nFrames);

    if (pDocument->nSelStart != nSelStart || pDocument->nSelEnd != nSelEnd)
    {
        pDocument->nOldSelStart = pDocument->nSelStart;
        pDocument->nOldSelEnd = pDocument->nSelEnd;
        pDocument->nSelStart = nSelStart;
        pDocument->nSelEnd = nSelEnd;
        g_signal_emit(pDocument, m_lDocumentSignals[SELECTION_CHANGED_SIGNAL], 0);
    }
}

void document_Zoom(Document *pDocument, gfloat fZoom, gboolean bFollowCursor)
{
    gint64 nDist = pDocument->nViewEnd - pDocument->nViewStart;
    gint64 nNewDist = (gint64)(GFLOAT(nDist) / fZoom);

    if (nNewDist >= pDocument->pChunk->nFrames)
    {
        document_SetView(pDocument, 0, pDocument->pChunk->nFrames);
        
        return;
    }
    
    if (nNewDist < 1)
    {
        nNewDist = 1;
    }

    gint64 nNewStart;
    
    if (bFollowCursor && pDocument->nCursorPos >= pDocument->nViewStart && pDocument->nCursorPos <= pDocument->nViewEnd)
    {
        nNewStart = pDocument->nCursorPos - nNewDist / 2;
    }
    else
    {
        nNewStart = pDocument->nViewStart + (nDist - nNewDist) / 2;
    }

    if (nNewStart < 0)
    {
        nNewStart = 0;
    }
    
    gint64 nNewEnd = nNewStart + nNewDist;

    if (nNewEnd > pDocument->pChunk->nFrames)
    {
        nNewEnd = pDocument->pChunk->nFrames;
        nNewStart = nNewEnd - nNewDist;
    }
    
    document_SetView(pDocument, nNewStart, nNewEnd);
}

gboolean document_CanUndo(Document *pDocument)
{
    return (pDocument->pHistoryEntry != NULL && pDocument->pHistoryEntry->pHistoryEntryPrev != NULL);
}

gboolean document_CanRedo(Document *pDocument)
{
    return (pDocument->pHistoryEntry != NULL && pDocument->pHistoryEntry->pHistoryEntryNext != NULL);
}

static void document_GetStateFromHistory(Document *pDocument)
{
    pDocument->nViewStart = pDocument->pHistoryEntry->nViewStart;
    pDocument->nViewEnd = pDocument->pHistoryEntry->nViewEnd;
    pDocument->nSelStart = pDocument->pHistoryEntry->nSelStart;
    pDocument->nSelEnd = pDocument->pHistoryEntry->nSelEnd;
    g_info("chunk_unref: %d, document:document_GetStateFromHistory %p", chunk_AliveCount(), pDocument->pChunk);
    g_object_unref(pDocument->pChunk);
    pDocument->pChunk = pDocument->pHistoryEntry->pChunk;
    g_info("chunk_ref: %d, document:document_GetStateFromHistory %p", chunk_AliveCount(), pDocument->pChunk);
    g_object_ref(pDocument->pChunk);
    g_signal_emit(pDocument, m_lDocumentSignals[STATE_CHANGED_SIGNAL], 0);
}

void document_Undo(Document *pDocument)
{
    g_assert(document_CanUndo(pDocument));
    
    player_Stop();
    document_FixHistory(pDocument);
    pDocument->pHistoryEntry = pDocument->pHistoryEntry->pHistoryEntryPrev;
    document_GetStateFromHistory(pDocument);
}

void document_Redo(Document *pDocument)
{
    g_assert(document_CanRedo(pDocument));
    
    player_Stop();
    document_FixHistory(pDocument);
    pDocument->pHistoryEntry = pDocument->pHistoryEntry->pHistoryEntryNext;
    document_GetStateFromHistory(pDocument);
}

void document_SetMainWindow(Document *pDocument, struct _MainWindow *pMainWindow)
{
    pDocument->pMainWindow = pMainWindow;
}
