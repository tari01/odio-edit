/*
    Copyright (C) 2019-2020, Robert Tari <robert@tari.in>
    Copyright (C) 2002 2003 2004 2005 2007 2008 2009 2010, Magnus Hjorth

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

#include <math.h>
#include "chunkview.h"
#include "main.h"

G_DEFINE_TYPE(ChunkView, chunkview, GTK_TYPE_DRAWING_AREA)

enum {VIEW_CHANGED_SIGNAL, SELECTION_CHANGED_SIGNAL, CURSOR_CHANGED_SIGNAL, DOUBLE_CLICK_SIGNAL, LAST_SIGNAL};

static gboolean m_bDragging = FALSE;
static gint64 m_nDragStart;
static gint64 m_nDragEnd;
static gboolean m_bAutoScroll = FALSE;
static guint m_nAutoScrollSource = 0;
static ChunkView *m_pChunkViewAutoScroll;
static gfloat m_fAutoScrollAmount;
static guint m_nFontHeight = 0;
static guint m_nFontWidth = 0;
static guint m_lChunkViewSignals[LAST_SIGNAL] = {0};
static void chunkview_RedrawFrames(ChunkView *pChunkView, gint64 nStart, gint64 nEnd);
static gint chunkview_CalcX(ChunkView *pChunkView, gint64 nFrame);
static gboolean chunkview_AutoScroll();
static guint chunkview_FindTimescalePoints(guint32 nSampleRate, gint64 nStartFrame, gint64 nEndFrame, gint64 *lPoints, gint *nPoints, gint64 *lMidPoints, gint *nMidPoints, gint64 *lMinorPoints, gint *nMinorPoints);

static void chunkview_OnChanged(Document *pDocument, ChunkView *pChunkView)
{
    gtk_widget_queue_draw(GTK_WIDGET(pChunkView));
}

static void chunkview_OnSelectionChanged(Document *pDocument, ChunkView *pChunkView)
{
    gint64 nOldStart = MAX(pDocument->nViewStart, pDocument->nOldSelStart);
    gint64 nOldEnd = MIN(pDocument->nViewEnd, pDocument->nOldSelEnd);
    gint64 nNewStart = MAX(pDocument->nViewStart, pDocument->nSelStart);
    gint64 nNewEnd = MIN(pDocument->nViewEnd, pDocument->nSelEnd);

    if (nOldStart >= nOldEnd && nNewStart >= nNewEnd)
    {
        // Do nothing
    }
    else if (nOldStart >= nOldEnd)
    {
        chunkview_RedrawFrames(pChunkView, nNewStart, nNewEnd);
    }
    else if (nNewStart >= nNewEnd)
    {
        chunkview_RedrawFrames(pChunkView, nOldStart, nOldEnd);
    }
    else if ((nNewStart >= nOldStart && nNewStart < nOldEnd) || (nOldStart >= nNewStart && nOldStart < nNewEnd))
    {
        if (nOldStart != nNewStart)
        {
            chunkview_RedrawFrames(pChunkView, MIN(nOldStart, nNewStart), MAX(nOldStart, nNewStart));
        }

        if (nOldEnd != nNewEnd)
        {
            chunkview_RedrawFrames(pChunkView, MIN(nOldEnd, nNewEnd), MAX(nOldEnd, nNewEnd));
        }
    }
    else
    {
        chunkview_RedrawFrames(pChunkView, nOldStart, nOldEnd);
        chunkview_RedrawFrames(pChunkView, nNewStart, nNewEnd);
    }
}

static void chunkview_OnCursorChanged(Document *pDocument, gboolean bRolling, ChunkView *pChunkView)
{
    gint lPix[2];
    lPix[0] = chunkview_CalcX(pChunkView, pDocument->nOldCursorPos);
    lPix[1] = chunkview_CalcX(pChunkView, pDocument->nCursorPos);

    if (lPix[0] != lPix[1])
    {
        for (gint nPix = 0; nPix < 2; nPix++)
        {
            if (lPix[nPix] > -1 && lPix[nPix] < pChunkView->nWidth)
            {
                gtk_widget_queue_draw_area(GTK_WIDGET(pChunkView), lPix[nPix], 0, 1, pChunkView->nHeight - m_nFontHeight);
            }
        }
    }
}

static void chunkview_OnDestroy(GtkWidget *pWidget)
{
    ChunkView *pChunkView = OE_CHUNK_VIEW(pWidget);

    if (pChunkView->pDocument != NULL)
    {
        g_signal_handlers_disconnect_matched(pChunkView->pDocument, G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, pChunkView);
        g_info("document_unref, chunkview:chunkview_OnDestroy %p", pChunkView->pDocument);
        g_object_unref(pChunkView->pDocument);
    }

    pChunkView->pDocument = NULL;
    GTK_WIDGET_CLASS(chunkview_parent_class)->destroy(pWidget);

    if (pChunkView->pViewCache)
    {
        viewcache_Free(pChunkView->pViewCache);
    }

    pChunkView->pViewCache = NULL;
}

static gint chunkview_CalcX(ChunkView *pChunkView, gint64 nFrame)
{
    if (nFrame < pChunkView->pDocument->nViewStart)
    {
        return -1;
    }

    if (nFrame > pChunkView->pDocument->nViewEnd)
    {
        return pChunkView->nWidth;
    }
    else
    {
        gfloat fStart = nFrame - pChunkView->pDocument->nViewStart;
        gfloat fEnd = pChunkView->pDocument->nViewEnd - pChunkView->pDocument->nViewStart;

        return pChunkView->nWidth * (fStart / fEnd);
    }
}

static void chunkview_UpdateImageMain(ChunkView *pChunkView, cairo_t *pCairo)
{
    if (pChunkView->nWidth < 0)
    {
        return;
    }

    gint nHeight = pChunkView->nHeight - m_nFontHeight;
    cairo_rectangle(pCairo, 0, 0, pChunkView->nWidth, pChunkView->nHeight);
    gdk_cairo_set_source_rgba(pCairo, &g_lColours[BACKGROUND]);
    cairo_fill(pCairo);

    if (pChunkView->pDocument == NULL || pChunkView->pDocument->pChunk->nFrames == 0)
    {
        return;
    }

    if (pChunkView->pDocument->nSelStart != pChunkView->pDocument->nSelEnd && pChunkView->pDocument->nSelStart < pChunkView->pDocument->nViewEnd && pChunkView->pDocument->nSelEnd > pChunkView->pDocument->nViewStart)
    {
        gint nHorPos;
        
        if (pChunkView->pDocument->nSelStart > pChunkView->pDocument->nViewStart)
        {
            nHorPos = chunkview_CalcX(pChunkView, pChunkView->pDocument->nSelStart);
        }
        else
        {
            nHorPos = 0;
        }

        if (nHorPos < 0)
        {
            nHorPos = 0;
        }

        if (nHorPos <= pChunkView->nWidth)
        {
            gint nHorSize;
            
            if (pChunkView->pDocument->nSelEnd < pChunkView->pDocument->nViewEnd)
            {
                nHorSize = chunkview_CalcX(pChunkView, pChunkView->pDocument->nSelEnd);
            }
            else
            {
                nHorSize = pChunkView->nWidth;
            }

            if (nHorSize > pChunkView->nWidth)
            {
                nHorSize = pChunkView->nWidth;
            }

            if (nHorSize >= 0)
            {
                cairo_rectangle(pCairo, nHorPos, 0, nHorSize - nHorPos, nHeight);
                gdk_cairo_set_source_rgba(pCairo, &g_lColours[SELECTION]);
                cairo_fill(pCairo);
            }
        }
    }

    gint nVertSize = nHeight / pChunkView->pDocument->pChunk->pAudioInfo->channels;

    for (gint nChannel = 0; nChannel < pChunkView->pDocument->pChunk->pAudioInfo->channels; nChannel++)
    {
        gint nVertPos = nVertSize / 2 + nChannel * nVertSize;
        cairo_move_to(pCairo, 0, nVertPos - 0.5);
        cairo_line_to(pCairo, pChunkView->nWidth, nVertPos - 0.5);
        cairo_set_line_width(pCairo, 1);
        gdk_cairo_set_source_rgba(pCairo, &g_lColours[BARS]);
        cairo_stroke(pCairo);
    }

    viewcache_DrawPart(pChunkView->pViewCache, pCairo, pChunkView->nWidth, nHeight, pChunkView->fScaleFactor);
}

static void chunkview_DrawTimeBars(ChunkView *pChunkView, cairo_t *pCairo, gint64 *lPoints, gint nPoints, gint64 *lIgnPoints, gint nIgnPoints, gboolean bSmall, gint bText)
{
    gint nIgnore = 0;

    for (gint nPoint = 0; nPoint < nPoints; nPoint++)
    {
        while (nIgnore < nIgnPoints && lIgnPoints[nIgnore] < lPoints[nPoint])
        {
            nIgnore++;
        }

        if (nIgnore < nIgnPoints && lIgnPoints[nIgnore] == lPoints[nPoint])
        {
            continue;
        }

        gint nLinePos = pChunkView->nWidth * (GFLOAT(lPoints[nPoint] - pChunkView->pDocument->nViewStart) / GFLOAT(pChunkView->pDocument->nViewEnd - pChunkView->pDocument->nViewStart));
        cairo_move_to(pCairo, nLinePos - 0.5, pChunkView->nHeight - m_nFontHeight - (bSmall ? (m_nFontHeight * 0.4) : (m_nFontHeight * 0.8)));
        cairo_line_to(pCairo, nLinePos - 0.5, pChunkView->nHeight - m_nFontHeight);
        cairo_set_line_width(pCairo, 1);
        gdk_cairo_set_source_rgba(pCairo, &g_lColours[SCALE]);
        cairo_stroke(pCairo);

        if (bText >= 0)
        {
            gchar lText[32];
            gchar *sText;
            
            if (bText > 0)
            {
                guint nMiliSecs = ((lPoints[nPoint] % pChunkView->pDocument->pChunk->pAudioInfo->rate) * 1000) / pChunkView->pDocument->pChunk->pAudioInfo->rate;
                g_snprintf(lText, 50, ".%03d", nMiliSecs);
                sText = lText;
            }
            else
            {
                sText = getTime(pChunkView->pDocument->pChunk->pAudioInfo->rate, lPoints[nPoint], lText, FALSE);
            }

            if (sText == NULL)
            {
                continue;
            }
            
            gint nPixelSize;
            GtkWidget *pWidget = GTK_WIDGET(pChunkView);
            PangoLayout *pPangoLayout = gtk_widget_create_pango_layout(pWidget, lText);
            pango_layout_get_pixel_size(pPangoLayout, &nPixelSize, NULL);
            gdk_cairo_set_source_rgba(pCairo, &g_lColours[TEXT]);
            cairo_move_to(pCairo, nLinePos - nPixelSize / 2, pChunkView->nHeight - m_nFontHeight);
            pango_cairo_show_layout(pCairo, pPangoLayout);
            g_object_unref(pPangoLayout);
        }
    }
}

static guint chunkview_FindTimescalePoints(guint32 nSampleRate, gint64 nStartFrame, gint64 nEndFrame, gint64 *lPoints, gint *nPoints, gint64 *lMidPoints, gint *nMidPoints, gint64 *lMinorPoints, gint *nMinorPoints)
{   
    gint nMaxPoints = *nPoints;
    gint nMaxMinorPoints = *nMinorPoints;
    gint nMaxMidPoints = *nMidPoints;

    g_assert(nMaxPoints > 2);
    g_assert(nMaxMinorPoints >= nMaxPoints);

    *nMidPoints = 0;
    gint nBigSize = 0;
    const gint lBigSizes[] = {1, 2, 5, 10, 20, 30, 60, 120, 180, 300, 600, 900, 1800, 3600, 36000};

    while (nBigSize < (ARRAY_LENGTH(lBigSizes) - 1) && ((nEndFrame - nStartFrame) / (lBigSizes[nBigSize] * nSampleRate) > (gint64)(nMaxPoints - 2)))
    {
        nBigSize++;
    }
    
    gint64 nStart = nStartFrame / (lBigSizes[nBigSize] * nSampleRate);
    gint64 nEnd = nEndFrame / (lBigSizes[nBigSize] * nSampleRate);
    guint nPoint = 0;

    while (1)
    {
        lPoints[nPoint++] = (nStart++) * lBigSizes[nBigSize] * nSampleRate;

        g_assert(nPoint <= nMaxPoints);

        if (nStart > nEnd)
        {
            break;
        }
    }

    *nPoints = nPoint;
    guint nMinorPoint = 0;

    if (nBigSize > 0)
    {
        nBigSize--;
        const gboolean lBigSkip[] = {FALSE, TRUE, FALSE, FALSE, TRUE, FALSE, FALSE, TRUE, TRUE, FALSE, TRUE, TRUE, FALSE, FALSE};

        while (lBigSkip[nBigSize])
        {
            nBigSize--;
        }

        if ((nEndFrame - nStartFrame) / (lBigSizes[nBigSize] * nSampleRate) >= (gint64)(nMaxMinorPoints - 2))
        {
            *nMinorPoints = 0;
            
            return 0;
        }

        while (nBigSize > 0 && ((nEndFrame - nStartFrame) / (lBigSizes[nBigSize - 1] * nSampleRate) < (gint64)(nMaxMinorPoints - 2)))
        {
            nBigSize--;
        }

        nStart = nStartFrame / (lBigSizes[nBigSize] * nSampleRate);
        nEnd = nEndFrame / (lBigSizes[nBigSize] * nSampleRate);

        for (gint64 i = nStart; i <= nEnd + 1; i++)
        {
            lMinorPoints[nMinorPoint++] = i * lBigSizes[nBigSize] * nSampleRate;
            g_assert(nMinorPoint <= nMaxMinorPoints);
        }

        *nMinorPoints = nMinorPoint;
        
        return 0;
    }

    const gint lSmallSizes[] = {1000, 100, 10};
    gint nSmallSizes = ARRAY_LENGTH(lSmallSizes);

    gint nSmallSize = 0;

    while (nSmallSize < nSmallSizes && ((nEndFrame - nStartFrame) * lSmallSizes[nSmallSize]) / nSampleRate >= (gint64)(nMaxMinorPoints - 2))
    {
        nSmallSize++;
    }

    if (nSmallSize >= nSmallSizes)
    {
        *nMinorPoints = 0;
        
        return 0;
    }

    nStart = (nStartFrame * lSmallSizes[nSmallSize]) / nSampleRate;
    nEnd = (nEndFrame * lSmallSizes[nSmallSize]) / nSampleRate;

    for (gint64 i = nStart; i <= nEnd + 1; i++)
    {
        lMinorPoints[nMinorPoint++] = (i * nSampleRate + lSmallSizes[nSmallSize] - 1) / lSmallSizes[nSmallSize];
    }

    *nMinorPoints = nMinorPoint;

    do
    {
        nSmallSize++;
    }
    while (nSmallSize < nSmallSizes && ((nEndFrame - nStartFrame) * lSmallSizes[nSmallSize]) / nSampleRate >= (gint64)(nMaxMidPoints - 2));

    if (nSmallSize >= nSmallSizes)
    {
        return 1;
    }

    nStart = (nStartFrame * lSmallSizes[nSmallSize]) / nSampleRate;
    nEnd = (nEndFrame * lSmallSizes[nSmallSize]) / nSampleRate;
    guint nMidPoint = 0;

    for (gint64 i = nStart; i <= nEnd + 1; i++)
    {
        lMidPoints[nMidPoint++] = (i * nSampleRate + lSmallSizes[nSmallSize] - 1) / lSmallSizes[nSmallSize];
    }

    *nMidPoints = nMidPoint;

    return 1;
}

static gint chunkview_OnDraw(GtkWidget *pWidget, cairo_t *pCairo)
{
    ChunkView *pChunkView = OE_CHUNK_VIEW(pWidget);

    chunkview_UpdateImageMain(pChunkView, pCairo);

    if (pChunkView->pDocument == NULL)
    {
        return FALSE;
    }

    if (pChunkView->pDocument->nCursorPos >= pChunkView->pDocument->nViewStart && pChunkView->pDocument->nCursorPos <= pChunkView->pDocument->nViewEnd)
    {
        gint nX = chunkview_CalcX(pChunkView, pChunkView->pDocument->nCursorPos);

        if (0 <= nX && pChunkView->nWidth > nX)
        {
            cairo_move_to(pCairo, nX + 0.5, 0);
            cairo_line_to(pCairo, nX + 0.5, pChunkView->nHeight - m_nFontHeight - 1);
            cairo_set_line_width(pCairo, 1);
            gdk_cairo_set_source_rgba(pCairo, &g_lColours[CURSOR]);
            cairo_stroke(pCairo);
        }
    }

    if (pChunkView->pDocument->pChunk)
    {
        cairo_rectangle(pCairo, 0, pChunkView->nHeight - m_nFontHeight, pChunkView->nWidth, m_nFontHeight);
        gdk_cairo_set_source_rgba(pCairo, &g_lColours[STRIPE]);
        cairo_fill(pCairo);

        gint nPoints = pChunkView->nWidth / m_nFontWidth + 1;

        if (nPoints < 3)
        {
            nPoints = 3;
        }

        gint nMidPoints = nPoints;
        gint nMinorPoints = pChunkView->nWidth / 8 + 1;

        if (nMinorPoints < nPoints)
        {
            nMinorPoints = nPoints;
        }

        gint64 *lPoints = g_malloc(nPoints * sizeof(gint64));
        gint64 *lMidPoints = g_malloc(nMidPoints * sizeof(gint64));
        gint64 *lMinorPoints = g_malloc(nMinorPoints * sizeof(gint64));
        guint nMinorText = chunkview_FindTimescalePoints(pChunkView->pDocument->pChunk->pAudioInfo->rate, pChunkView->pDocument->nViewStart, pChunkView->pDocument->nViewEnd, lPoints, &nPoints, lMidPoints, &nMidPoints, lMinorPoints, &nMinorPoints);
        guint nMidText = nMinorText;

        if (nMinorPoints > 0 && (pChunkView->nWidth / nMinorPoints) < m_nFontWidth)
        {
            nMinorText = -1;
        }
        else
        {
            nMidText = -1;
        }

        chunkview_DrawTimeBars(pChunkView, pCairo, lPoints, nPoints, NULL, 0, FALSE, 0);

        if (nMidText >= 0)
        {
            chunkview_DrawTimeBars(pChunkView, pCairo, lMidPoints, nMidPoints, lPoints, nPoints, FALSE, nMidText);
        }

        chunkview_DrawTimeBars(pChunkView, pCairo, lMinorPoints, nMinorPoints, lPoints, nPoints, TRUE, nMinorText);

        g_free(lPoints);
        g_free(lMidPoints);
        g_free(lMinorPoints);
    }

    return FALSE;
}

static void chunkview_OnSizeAllocate(GtkWidget *pWidget, GtkAllocation *pAllocation)
{
    ChunkView *pChunkView = OE_CHUNK_VIEW(pWidget);
    GTK_WIDGET_CLASS(chunkview_parent_class)->size_allocate(pWidget, pAllocation);
    pChunkView->nWidth = pAllocation->width;
    pChunkView->nHeight = pAllocation->height;

    if (pChunkView->pDocument == NULL)
    {
        return;
    }

    viewcache_Update(pChunkView->pViewCache, pChunkView->pDocument->pChunk, pChunkView->pDocument->nViewStart, pChunkView->pDocument->nViewEnd, pAllocation->width, NULL, NULL);
}

static gint64 chunkview_CalcFrame(ChunkView *pChunkView, gfloat fPos, gfloat fMax)
{
    return pChunkView->pDocument->nViewStart + (fPos / fMax) * (pChunkView->pDocument->nViewEnd - pChunkView->pDocument->nViewStart);
}

static gint chunkview_ScrollWheel(GtkWidget *pWidget, gdouble fMouseX, gint nDirection)
{
    ChunkView *pChunkView = OE_CHUNK_VIEW(pWidget);

    if (pChunkView->pDocument == NULL)
    {
        return FALSE;
    }

    m_nDragStart = m_nDragEnd = chunkview_CalcFrame(pChunkView, fMouseX, GFLOAT(pChunkView->nWidth));
    gfloat fZoom;

    if (nDirection == -1)
    {
        if (pChunkView->pDocument->nViewEnd - pChunkView->pDocument->nViewStart == pChunkView->nWidth)
        {
            return FALSE;
        }

        fZoom = 1.0 / 1.4;
    }
    else if (pChunkView->pDocument->nViewEnd - pChunkView->pDocument->nViewStart < 2)
    {
        fZoom = 4.0;
    }
    else
    {
        fZoom = 1.4;
    }

    gint64 nViewStart = m_nDragStart - (gint64)(GFLOAT(m_nDragStart - pChunkView->pDocument->nViewStart)) * fZoom;
    gint64 nViewEnd = m_nDragStart + (gint64)(GFLOAT(pChunkView->pDocument->nViewEnd - m_nDragStart)) * fZoom;

    if (nViewStart < 0)
    {
        nViewStart = 0;
    }

    if (nViewEnd < m_nDragStart || nViewEnd > pChunkView->pDocument->pChunk->nFrames)
    {
        nViewEnd = pChunkView->pDocument->pChunk->nFrames;
    }

    if (nViewEnd == nViewStart)
    {
        if (nViewEnd < pChunkView->pDocument->pChunk->nFrames)
        {
            nViewEnd++;
        }
        else if (nViewStart > 0)
        {
            nViewStart--;
        }
    }

    if (pChunkView->pDocument->pChunk->nFrames - nViewEnd - nViewStart > 0 && pChunkView->pDocument->pChunk->nFrames - nViewEnd - nViewStart <= 10)//-
    {
        nViewStart = 0;
        nViewEnd = pChunkView->pDocument->pChunk->nFrames;
    }

    document_SetView(pChunkView->pDocument, nViewStart, nViewEnd);

    return FALSE;
}

static gint chunkview_OnScroll(GtkWidget *pWidget, GdkEventScroll *pEventScroll)
{
    if (pEventScroll->direction == GDK_SCROLL_UP)
    {
        return chunkview_ScrollWheel(pWidget, pEventScroll->x, 1);
    }
    else
    {
        return chunkview_ScrollWheel(pWidget, pEventScroll->x, -1);
    }
}

static gint chunkview_OnButtonPress(GtkWidget *pWidget, GdkEventButton *pEventButton)
{
    ChunkView *pChunkView = OE_CHUNK_VIEW(pWidget);

    if (pChunkView->pDocument == NULL)
    {
        return FALSE;
    }

    if (pEventButton->type == GDK_2BUTTON_PRESS && pEventButton->button == 1)
    {
        gint64 nFrame = chunkview_CalcFrame(pChunkView, pEventButton->x, GFLOAT(pChunkView->nWidth));
        g_signal_emit(pChunkView, m_lChunkViewSignals[DOUBLE_CLICK_SIGNAL], 0, &nFrame);

        return FALSE;
    }

    m_nDragStart = m_nDragEnd = chunkview_CalcFrame(pChunkView, pEventButton->x, GFLOAT(pChunkView->nWidth));
    gdouble fSelStart = GDOUBLE(chunkview_CalcX(pChunkView, pChunkView->pDocument->nSelStart));
    gdouble fSelEnd = GDOUBLE(chunkview_CalcX(pChunkView, pChunkView->pDocument->nSelEnd));

    if (fabs(pEventButton->x - fSelStart) < 3.0)
    {
        m_nDragStart = m_nDragEnd = pChunkView->pDocument->nSelStart;
    }
    else if (fabs(pEventButton->x - fSelEnd) < 3.0)
    {
        m_nDragStart = m_nDragEnd = pChunkView->pDocument->nSelEnd;
    }

    if ((pEventButton->state & GDK_SHIFT_MASK) != 0 && pChunkView->pDocument->nSelEnd != pChunkView->pDocument->nSelStart)
    {
        m_bDragging = TRUE;

        if ((pEventButton->button == 1 && m_nDragStart-pChunkView->pDocument->nSelEnd > pChunkView->pDocument->nSelStart - m_nDragStart) || (pEventButton->button != 1 && m_nDragStart - pChunkView->pDocument->nSelEnd < pChunkView->pDocument->nSelStart - m_nDragStart))
        {
            m_nDragStart = pChunkView->pDocument->nSelStart;
        }
        else
        {
            m_nDragStart = pChunkView->pDocument->nSelEnd;
        }

        document_SetSelection(pChunkView->pDocument, m_nDragStart, m_nDragEnd);

    }
    else if (pEventButton->button == 1 || pEventButton->button == 2)
    {
        m_bDragging = TRUE;

        if (pChunkView->pDocument->nSelEnd != pChunkView->pDocument->nSelStart)
        {
            if (pChunkView->pDocument->nSelStart >= pChunkView->pDocument->nViewStart && m_nDragStart == pChunkView->pDocument->nSelStart)
            {
                m_nDragStart = pChunkView->pDocument->nSelEnd;
                m_nDragEnd = pChunkView->pDocument->nSelStart;

                return FALSE;
            }
            else if (pChunkView->pDocument->nSelEnd <= pChunkView->pDocument->nViewEnd && m_nDragStart == pChunkView->pDocument->nSelEnd)
            {
                m_nDragStart = pChunkView->pDocument->nSelStart;
                m_nDragEnd = pChunkView->pDocument->nSelEnd;

                return FALSE;
            }
        }

        document_SetSelection(pChunkView->pDocument, m_nDragStart, m_nDragEnd);
    }
    else if (pEventButton->button == 3)
    {
        document_SetCursor(pChunkView->pDocument, m_nDragStart);
    }
    else if (pEventButton->button == 4 || pEventButton->button == 5)
    {
        if (pEventButton->button == 5)
        {
            chunkview_ScrollWheel(pWidget, pEventButton->x, -1);
        }
        else
        {
            chunkview_ScrollWheel(pWidget, pEventButton->x, 1);
        }
    }

    return FALSE;
}

static gint chunkview_OnMotionNotify(GtkWidget *pWidget, GdkEventMotion *pEventMotion)
{
    static GdkCursor *pCursor = NULL;
    
    ChunkView *pChunkView = OE_CHUNK_VIEW(pWidget);

    if (pChunkView->pDocument == NULL)
    {
        return FALSE;
    }

    if (m_bDragging)
    {
        if (pEventMotion->x < pChunkView->nWidth)
        {
            m_nDragEnd = chunkview_CalcFrame(pChunkView, (pEventMotion->x > 0.0) ? pEventMotion->x : 0.0, GFLOAT(pChunkView->nWidth));
        }
        else
        {
            m_nDragEnd = pChunkView->pDocument->nViewEnd;
        }

        document_SetSelection(pChunkView->pDocument, m_nDragStart, m_nDragEnd);

        if ((pEventMotion->x < 0.0 && pChunkView->pDocument->nViewStart > 0) || (pEventMotion->x > pChunkView->nWidth && pChunkView->pDocument->nViewEnd < pChunkView->pDocument->pChunk->nFrames))
        {
            if (!m_bAutoScroll)
            {
                m_bAutoScroll = TRUE;
                m_pChunkViewAutoScroll = pChunkView;
            }

            if (pEventMotion->x < 0.0)
            {
                m_fAutoScrollAmount = pEventMotion->x;
            }
            else
            {
                m_fAutoScrollAmount = pEventMotion->x - pChunkView->nWidth;
            }

            chunkview_AutoScroll();
        }
        else
        {
            m_bAutoScroll = FALSE;
        }
    }
    else if (pChunkView->pDocument->nSelStart != pChunkView->pDocument->nSelEnd)
    {
        gint nSelStart;
        
        if (pChunkView->pDocument->nSelStart >= pChunkView->pDocument->nViewStart)
        {
            nSelStart = chunkview_CalcX(pChunkView, pChunkView->pDocument->nSelStart);
        }
        else
        {
            nSelStart = -500;
        }

        gint nSelEnd;
        
        if (pChunkView->pDocument->nSelEnd <= pChunkView->pDocument->nViewEnd)
        {
            nSelEnd = chunkview_CalcX(pChunkView, pChunkView->pDocument->nSelEnd);
        }
        else
        {
            nSelEnd = -500;
        }

        if (fabs(pEventMotion->x - GDOUBLE(nSelStart)) < 3.0 || fabs(pEventMotion->x - GDOUBLE(nSelEnd)) < 3.0)
        {
            if (pCursor == NULL)
            {
                pCursor = gdk_cursor_new_for_display(gdk_display_get_default(), GDK_SB_H_DOUBLE_ARROW);
            }

            gdk_window_set_cursor(gtk_widget_get_window(pWidget), pCursor);
        }
        else
        {
            gdk_window_set_cursor(gtk_widget_get_window(pWidget), NULL);
        }
    }

    return FALSE;
}

static gboolean chunkview_AutoScroll()
{
    gfloat fFrame = m_fAutoScrollAmount * GFLOAT(m_pChunkViewAutoScroll->pDocument->nViewEnd - m_pChunkViewAutoScroll->pDocument->nViewStart) / GFLOAT(OE_CHUNK_VIEW(m_pChunkViewAutoScroll)->nWidth);

    if (fFrame < 0.0)
    {
        gint64 nFrame = (gint64)(-fFrame);

        if (nFrame > m_nDragEnd)
        {
            nFrame = m_nDragEnd;
            m_bAutoScroll = FALSE;
        }

        m_nDragEnd -= nFrame;
        document_SetSelection(m_pChunkViewAutoScroll->pDocument,m_nDragEnd, m_nDragStart);
        document_SetView(m_pChunkViewAutoScroll->pDocument, m_nDragEnd, m_pChunkViewAutoScroll->pDocument->nViewEnd - nFrame);
    }
    else
    {
        gint64 nFrame = (gint64)fFrame;

        if (m_nDragEnd + nFrame > m_pChunkViewAutoScroll->pDocument->pChunk->nFrames)
        {
            nFrame = m_pChunkViewAutoScroll->pDocument->pChunk->nFrames - m_nDragEnd;
            m_bAutoScroll = FALSE;
        }

        m_nDragEnd += nFrame;
        document_SetSelection(m_pChunkViewAutoScroll->pDocument, m_nDragStart, m_nDragEnd);
        document_SetView(m_pChunkViewAutoScroll->pDocument, m_pChunkViewAutoScroll->pDocument->nViewStart + nFrame, m_nDragEnd);
    }

    if (m_bAutoScroll)
    {
        if (m_nAutoScrollSource == 0)
        {
            m_nAutoScrollSource = g_timeout_add(40, (GSourceFunc)chunkview_AutoScroll, NULL);
        }
        
        return G_SOURCE_CONTINUE;
    }
    else
    {
        m_nAutoScrollSource = 0;
        
        return G_SOURCE_REMOVE;
    }
}

static gint chunkview_OnButtonRelease(GtkWidget *pWidget, GdkEventButton *pEventButton)
{
    ChunkView *pChunkView = OE_CHUNK_VIEW(pWidget);

    if (pEventButton->button == 2 && pChunkView->pDocument != NULL)
    {
        document_PlaySelection(pChunkView->pDocument);
    }

    m_bAutoScroll = FALSE;
    m_bDragging = FALSE;

    return FALSE;
}

static void chunkview_class_init(ChunkViewClass *cls)
{
    GTK_WIDGET_CLASS(cls)->destroy = chunkview_OnDestroy;
    GTK_WIDGET_CLASS(cls)->draw = chunkview_OnDraw;
    GTK_WIDGET_CLASS(cls)->size_allocate = chunkview_OnSizeAllocate;
    GTK_WIDGET_CLASS(cls)->button_press_event = chunkview_OnButtonPress;
    GTK_WIDGET_CLASS(cls)->motion_notify_event = chunkview_OnMotionNotify;
    GTK_WIDGET_CLASS(cls)->button_release_event = chunkview_OnButtonRelease;
    GTK_WIDGET_CLASS(cls)->scroll_event = chunkview_OnScroll;

    m_lChunkViewSignals[DOUBLE_CLICK_SIGNAL] = g_signal_new("double-click", G_TYPE_FROM_CLASS(cls), G_SIGNAL_RUN_FIRST, 0, NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_POINTER);
}

static void chunkview_init(ChunkView *pChunkView)
{
    pChunkView->pDocument = NULL;
    pChunkView->fScaleFactor = 1.0;
    pChunkView->pViewCache = viewcache_New();
    gtk_widget_set_events(GTK_WIDGET(pChunkView), GDK_BUTTON_MOTION_MASK | GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK | GDK_POINTER_MOTION_MASK | GDK_SCROLL_MASK);

    if (!m_nFontHeight)
    {
        PangoLayout *pPangoLayout = gtk_widget_create_pango_layout(GTK_WIDGET(pChunkView), "0123456789");
        pango_layout_get_pixel_size(pPangoLayout, (gint *)&m_nFontWidth, (gint *)&m_nFontHeight);
        g_object_unref(pPangoLayout);
        pPangoLayout = NULL;
    }
}

ChunkView *chunkview_new()
{
    return g_object_new(OE_TYPE_CHUNK_VIEW, NULL);
}

void chunkview_SetDocument(ChunkView *pChunkView, Document *pDocument)
{
    if (pChunkView->pDocument == pDocument)
    {
        return;
    }

    if (pChunkView->pDocument != NULL)
    {
        g_signal_handlers_disconnect_matched(pChunkView->pDocument, G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, pChunkView);
        g_info("document_unref, chunkview:chunkview_SetDocument %p", pChunkView->pDocument);
        g_object_unref(pChunkView->pDocument);
    }

    pChunkView->pDocument = pDocument;

    if (pDocument != NULL)
    {
        g_info("document_ref, chunkview:chunkview_SetDocument %p", pChunkView->pDocument);
        g_object_ref(pDocument);

        if (g_object_is_floating(pDocument))
        {
            g_object_ref_sink(pDocument);
            g_object_unref(pDocument);
        }

        g_signal_connect(pDocument, "view_changed", G_CALLBACK(chunkview_OnChanged), pChunkView);
        g_signal_connect(pDocument, "status_changed", G_CALLBACK(chunkview_OnChanged), pChunkView);
        g_signal_connect(pDocument, "selection_changed", G_CALLBACK(chunkview_OnSelectionChanged), pChunkView);
        g_signal_connect(pDocument, "cursor_changed", G_CALLBACK(chunkview_OnCursorChanged), pChunkView);
    }
    else
    {
        viewcache_Reset(pChunkView->pViewCache);
    }

    chunkview_OnChanged(pChunkView->pDocument, pChunkView);
}

static void chunkview_RedrawFrames(ChunkView *pChunkView, gint64 nStart, gint64 nEnd)
{
    guint nStartPix = chunkview_CalcX(pChunkView, nStart);
    guint nEndPix = chunkview_CalcX(pChunkView, nEnd);

    gtk_widget_queue_draw_area(GTK_WIDGET(pChunkView), nStartPix, 0, nEndPix - nStartPix, pChunkView->nHeight - m_nFontHeight);
}

gboolean chunkview_UpdateCache(ChunkView *pChunkView)
{
    gint nNeedRedrawLeft;
    gint nNeedRedrawRight;
    gboolean bUpdate = viewcache_Update(pChunkView->pViewCache, pChunkView->pDocument->pChunk, pChunkView->pDocument->nViewStart, pChunkView->pDocument->nViewEnd, pChunkView->nWidth, &nNeedRedrawLeft, &nNeedRedrawRight);

    if (!bUpdate && pChunkView->nNeedRedrawLeft != -1)
    {
        gtk_widget_queue_draw_area(GTK_WIDGET(pChunkView), pChunkView->nNeedRedrawLeft, 0, pChunkView->nNeedRedrawRight - pChunkView->nNeedRedrawLeft, pChunkView->nHeight - m_nFontHeight);
        pChunkView->nNeedRedrawLeft = -1;
    }

    if (bUpdate && nNeedRedrawLeft != nNeedRedrawRight)
    {
        if (pChunkView->nNeedRedrawLeft == -1)
        {
            pChunkView->nNeedRedrawLeft = nNeedRedrawLeft;
            pChunkView->nNeedRedrawRight = nNeedRedrawRight;
        }
        else
        {
            if (nNeedRedrawLeft < pChunkView->nNeedRedrawLeft)
            {
                pChunkView->nNeedRedrawLeft = nNeedRedrawLeft;
            }

            if (nNeedRedrawRight > pChunkView->nNeedRedrawRight)
            {
                pChunkView->nNeedRedrawRight = nNeedRedrawRight;
            }
        }

        if (pChunkView->nNeedRedrawRight - pChunkView->nNeedRedrawLeft > 20 || (pChunkView->nLastRedrawTime != time(0)) || viewcache_Updated(pChunkView->pViewCache))
        {
            gtk_widget_queue_draw_area(GTK_WIDGET(pChunkView), pChunkView->nNeedRedrawLeft, 0, pChunkView->nNeedRedrawRight - pChunkView->nNeedRedrawLeft, pChunkView->nHeight - m_nFontHeight);
            pChunkView->nLastRedrawTime = time(0);
            pChunkView->nNeedRedrawLeft = -1;
        }
    }

    return bUpdate;
}

void chunkview_ForceRepaint(ChunkView *pChunkView)
{
    chunkview_OnChanged(pChunkView->pDocument, pChunkView);
}

void chunkview_SetScale(ChunkView *pChunkView, gfloat fScaleFactor)
{
    pChunkView->fScaleFactor = fScaleFactor;
    chunkview_OnChanged(pChunkView->pDocument, pChunkView);
}
