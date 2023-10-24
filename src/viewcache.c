/*
    Copyright (C) 2019-2023, Robert Tari <robert@tari.in>
    Copyright (C) 2002 2003 2004 2005 2007 2009, Magnus Hjorth

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
#include "viewcache.h"
#include "main.h"

#define CALC_UNKNOWN 0
#define CALC_DIRTY 1
#define CALC_DONE 2

ViewCache *viewcache_New()
{
    ViewCache *pViewCache;
    pViewCache = g_malloc(sizeof(*pViewCache));
    pViewCache->pChunk = NULL;
    pViewCache->pChunkHandle = NULL;
    pViewCache->lValues = NULL;
    pViewCache->lOffsets = NULL;
    pViewCache->lCalced = NULL;
    pViewCache->lSamples = NULL;
    pViewCache->nBytes = 0;
    pViewCache->bReading = FALSE;

    return pViewCache;
}

static void viewcache_Clear(ViewCache *pViewCache)
{
    if (pViewCache->pChunk)
    {
        if (pViewCache->pChunkHandle)
        {
            chunk_Close(pViewCache->pChunkHandle, FALSE);
        }

        pViewCache->pChunkHandle = NULL;
        g_info("chunk_unref: %d, viewcache:viewcache_Clear %p", chunk_AliveCount(), pViewCache->pChunk);
        g_object_unref(pViewCache->pChunk);
    }

    g_free(pViewCache->lValues);
    pViewCache->lValues = NULL;
    g_free(pViewCache->lOffsets);
    pViewCache->lOffsets = NULL;
    
    if (pViewCache->lCalced != NULL)
    {
        g_free(pViewCache->lCalced);
        pViewCache->lCalced = NULL;
    }
}

void viewcache_Reset(ViewCache *pViewCache)
{   
    g_free(pViewCache->lCalced);
    pViewCache->lCalced = NULL;
}

void viewcache_Free(ViewCache *pViewCache)
{
    viewcache_Clear(pViewCache);
    g_free(pViewCache->lSamples);
    pViewCache->lSamples = NULL;
    g_free(pViewCache);
}

gboolean viewcache_Update(ViewCache *pCache, Chunk *pChunk, gint64 nStartFrame, gint64 nEndFrame, gint nWidth, gint *nUpdatedLeft, gint *nUpdatedRight)
{  
    if (pCache->bReading)
    {
        return FALSE;
    }

    if (nEndFrame == nStartFrame)
    {
        if (pCache->nWidth != nWidth)
        {
            g_free(pCache->lCalced);
            pCache->lCalced = g_malloc0(nWidth);
            pCache->nWidth = nWidth;
        }

        if (nUpdatedLeft)
        {
            *nUpdatedLeft = 0;
            *nUpdatedRight = nWidth - 1;
            *nUpdatedRight += 1;//-
        }

        return FALSE;
    }

    gboolean bChunkChanged = (pCache->pChunk != pChunk);
    gboolean bRangeChanged = (pChunk != NULL) && (bChunkChanged || pCache->nStart != nStartFrame || pCache->nEnd != nEndFrame || pCache->nWidth != nWidth);

    if (!bChunkChanged && !bRangeChanged && pCache->pChunkHandle == NULL)
    {
        return FALSE;
    }

    gdouble fFramesPerPixel = GDOUBLE(nEndFrame - nStartFrame) / GDOUBLE(nWidth);

    if (bChunkChanged)
    {
        viewcache_Clear(pCache);
        pCache->pChunk = pChunk;

        if (pChunk != NULL)
        {
            g_object_ref(pChunk);
        }

        pCache->bChunkError = FALSE;
    }

    if (pCache->bChunkError)
    {
        return FALSE;
    }
    
    guint nChannels = pChunk ? pChunk->pAudioInfo->channels : 0;

    if (bRangeChanged)
    {
        if (pCache->pChunkHandle == NULL)
        {
            pCache->pChunkHandle = chunk_Open(pChunk, FALSE);
        }

        gfloat *lNewValues = g_malloc0(nWidth * nChannels * 2 * sizeof(*lNewValues));
        gint64 *lNewOffsets = g_malloc((nWidth + 1) * sizeof(*lNewOffsets));
        gchar *lNewCalced = g_malloc0(nWidth);
        lNewOffsets[0] = nStartFrame;
        lNewOffsets[nWidth] = nEndFrame;
        gdouble d = GDOUBLE(nStartFrame) + fFramesPerPixel;

        for (guint nWidthIter = 1; nWidthIter < nWidth; nWidthIter++, d += fFramesPerPixel)
        {
            lNewOffsets[nWidthIter] = (gint64)d;
        }

        if (bChunkChanged || nEndFrame <= pCache->nStart || nStartFrame >= pCache->nEnd)
        {
            // No overlap - don't do anything
        }
        else if (nWidth == pCache->nWidth && nEndFrame - nStartFrame == pCache->nEnd - pCache->nStart)
        {
            // Scroll
            guint nPosOld = 0;
            guint nPosNew = 0;

            if (pCache->lOffsets[0] < lNewOffsets[0])
            {
                nPosOld = GUINT32(GDOUBLE(lNewOffsets[0] - pCache->lOffsets[0]) / fFramesPerPixel);
            }
            else
            {
                nPosNew = GUINT32(GDOUBLE(pCache->lOffsets[0] - lNewOffsets[0]) / fFramesPerPixel);
            }

            guint nArraySize = nWidth - MAX(nPosNew, nPosOld);
            memcpy(lNewValues + (2 * nPosNew * nChannels), pCache->lValues + (2 * nPosOld * nChannels), nArraySize * nChannels * 2 * 4);

            for (guint nWidthIter = 0; nWidthIter <= nWidth; nWidthIter++)
            {
                lNewOffsets[nWidthIter] += pCache->lOffsets[nPosOld] - lNewOffsets[nPosNew];
            }

            if (fFramesPerPixel >= 1.0)
            {
                memcpy(lNewCalced + nPosNew, pCache->lCalced + nPosOld, nArraySize);
            }
            else
            {
                memset(lNewCalced + nPosNew, CALC_DIRTY, nArraySize);
            }
        }
        else if (nEndFrame - nStartFrame > pCache->nEnd - pCache->nStart)
        {
            // Zoom out
            guint nPosOld = 0;
            guint nPosNew = 0;

            while (1)
            {
                while (nPosOld < pCache->nWidth && (!pCache->lCalced[nPosOld] || pCache->lOffsets[nPosOld + 1] < lNewOffsets[nPosNew]))
                {
                    nPosOld++;
                }

                if (nPosOld == pCache->nWidth)
                {
                    break;
                }

                while (pCache->lOffsets[nPosOld] > lNewOffsets[nPosNew] && nPosNew < nWidth)
                {
                    nPosNew++;
                }

                if (nPosNew == nWidth)
                {
                    break;
                }

                lNewCalced[nPosNew] = CALC_DIRTY;
                memcpy(lNewValues + nPosNew * 2 * nChannels, pCache->lValues + 2 * nPosOld * nChannels, 2 * nChannels * sizeof(lNewValues[0]));
                nPosNew++;
            }
        }
        else
        {
            // Zoom in
            guint nPosOld = 0;
            guint nPosNew = 0;
            guint nPosLast = 0;

            while (1)
            {
                while (nPosOld < pCache->nWidth && (!pCache->lCalced[nPosOld] || pCache->lOffsets[nPosOld + 1] < lNewOffsets[nPosNew]))
                {
                    nPosOld++;
                }

                if (nPosOld == pCache->nWidth)
                {
                    break;
                }

                while (lNewOffsets[nPosNew] < pCache->lOffsets[nPosOld] && nPosNew < nWidth)
                {
                    nPosNew++;
                }

                if (nPosNew == nWidth)
                {
                    break;
                }

                nPosLast = nPosNew;

                while (lNewOffsets[nPosLast + 1] < pCache->lOffsets[nPosOld + 1] && nPosLast + 1 < nWidth)
                {
                    nPosLast++;
                }

                lNewCalced[nPosNew] = CALC_DONE;
                memcpy(lNewValues + nPosNew * 2 * nChannels, pCache->lValues + nPosOld * 2 * nChannels, 2 * nChannels * sizeof(lNewValues[0]));
                nPosNew = nPosLast + 1;
                nPosOld++;
            }
        }

        if (pCache->pChunkHandle == NULL)
        {
            memset(lNewValues, 0, nWidth * nChannels * 2 * 4);
            memset(lNewCalced, CALC_DONE, nWidth);
            pCache->bChunkError = TRUE;
        }

        pCache->nStart = nStartFrame;
        pCache->nEnd = nEndFrame;
        pCache->nWidth = nWidth;
        g_free(pCache->lValues);
        pCache->lValues = lNewValues;
        g_free(pCache->lOffsets);
        pCache->lOffsets = lNewOffsets;
        g_free(pCache->lCalced);
        pCache->lCalced = lNewCalced;

        if (nUpdatedLeft != NULL)
        {
            *nUpdatedLeft = 0;
            *nUpdatedRight = nWidth - 1;
            *nUpdatedRight += 1;//-
        }

        return TRUE;
    }

    if (pChunk == NULL || pCache->pChunkHandle == NULL || pCache->bChunkError)
    {
        return FALSE;
    }

    if (fFramesPerPixel < 1.0)
    {
        guint nOffsetPos = pCache->lOffsets[nWidth - 1] - pCache->lOffsets[0] + 1;
        guint nBytes = nOffsetPos * nChannels * 4;

        if (nBytes > pCache->nBytes)
        {
            g_free(pCache->lSamples);
            pCache->nBytes = nBytes;
            pCache->lSamples = g_malloc(nBytes);
        }

        pCache->bReading = TRUE;
        chunk_Read(pCache->pChunkHandle, pCache->lOffsets[0], nOffsetPos, (gchar *)pCache->lSamples, TRUE, FALSE);
        pCache->bReading = FALSE;

        for (guint nWidthIter = 0; nWidthIter < nWidth; nWidthIter++)
        {
            for (guint nChannel = 0; nChannel < nChannels; nChannel++)
            {
                pCache->lValues[(nWidthIter * nChannels + nChannel) * 2] = pCache->lValues[(nWidthIter * nChannels + nChannel) * 2 + 1] = pCache->lSamples[(pCache->lOffsets[nWidthIter] - pCache->lOffsets[0]) * nChannels + nChannel];
            }
        }

        memset(pCache->lCalced, CALC_DONE, nWidth);
        chunk_Close(pCache->pChunkHandle, FALSE);
        pCache->pChunkHandle = NULL;

        if (nUpdatedLeft)
        {
            *nUpdatedLeft = 0;
            *nUpdatedRight = nWidth - 1;
            *nUpdatedRight += 1;//-
        }

        return TRUE;
    }

    gchar *nUncalcPos = memchr(pCache->lCalced, CALC_UNKNOWN, nWidth);

    if (nUncalcPos == NULL)
    {
        nUncalcPos = memchr(pCache->lCalced, CALC_DIRTY, nWidth);
    }

    if (nUncalcPos == NULL)
    {
        chunk_Close(pCache->pChunkHandle, FALSE);
        pCache->pChunkHandle = NULL;

        if (nUpdatedLeft)
        {
            *nUpdatedLeft = *nUpdatedRight = 0;
        }

        return TRUE;
    }

    guint nUncalcStart = nUncalcPos - pCache->lCalced;
    guint nUncalcEnd = nUncalcStart + 1;
    guint nPixelsPerUpdate = round(GDOUBLE(BUFFER_SIZE / (nChannels * 4)) / fFramesPerPixel);

    while (nUncalcEnd < nUncalcStart + nPixelsPerUpdate && nUncalcEnd < nWidth)//- && pCache->lCalced[nUncalcEnd] < CALC_DONE
    {
        nUncalcEnd++;
    }

    memset(pCache->lCalced + nUncalcStart, CALC_DONE, nUncalcEnd - nUncalcStart);

    if (nUpdatedLeft)
    {
        *nUpdatedLeft = nUncalcStart;
        *nUpdatedRight = nUncalcEnd;
        *nUpdatedRight += 1;//-
    }

    guint nFramesToRead = pCache->lOffsets[nUncalcEnd] - pCache->lOffsets[nUncalcStart];

    if (nFramesToRead < 1)
    {
        nFramesToRead = 1;
    }

    guint nBytesRead = nFramesToRead * nChannels * 4;

    if (nBytesRead > pCache->nBytes)
    {
        g_free(pCache->lSamples);
        pCache->nBytes = nBytesRead;
        pCache->lSamples = g_malloc(pCache->nBytes);
    }

    pCache->bReading = TRUE;
    guint nFramesRead = chunk_Read(pCache->pChunkHandle, pCache->lOffsets[nUncalcStart], nFramesToRead, (gchar *)pCache->lSamples, TRUE, FALSE);
    pCache->bReading = FALSE;

    g_assert(nFramesRead == 0 || nFramesRead == nFramesToRead);

    if (nFramesRead == 0)
    {
        memset(pCache->lCalced, CALC_DONE, nWidth);

        return TRUE;
    }

    guint nFramePos = 0;
    guint nCalcedPos = nUncalcStart;

    while (nCalcedPos < nUncalcEnd)
    {
        for (guint nChannel = 0; nChannel < nChannels; nChannel++)
        {
            pCache->lValues[(nCalcedPos * nChannels + nChannel) * 2] = 1.0;
            pCache->lValues[(nCalcedPos * nChannels + nChannel) * 2 + 1] = -1.0;
        }

        while (nFramePos + pCache->lOffsets[nUncalcStart] < pCache->lOffsets[nCalcedPos + 1])
        {
            for (guint nChannel = 0; nChannel < nChannels; nChannel++)
            {
                gfloat fSample = pCache->lSamples[nFramePos * nChannels + nChannel];

                if (fSample < pCache->lValues[(nCalcedPos * nChannels + nChannel) * 2])
                {
                    pCache->lValues[(nCalcedPos * nChannels + nChannel) * 2] = fSample;
                }

                if (fSample > pCache->lValues[(nCalcedPos * nChannels + nChannel) * 2 + 1])
                {
                    pCache->lValues[(nCalcedPos * nChannels + nChannel) * 2 + 1] = fSample;
                }
            }

            nFramePos++;
        }

        nCalcedPos++;
    }

    return TRUE;
}

gboolean viewcache_Updated(ViewCache *pViewCache)
{
    return (pViewCache->pChunkHandle == NULL);
}

void viewcache_DrawPart(ViewCache *pViewCache, cairo_t *pCairo, gint nWidth, gint nHeight, gfloat fScale)
{   
    if (pViewCache->lCalced == NULL)
    {
        return;
    }

    guint nChannels = (pViewCache->pChunk) ? pViewCache->pChunk->pAudioInfo->channels : 1;
    gfloat nChannelHeight = nHeight / (2 * nChannels) - 5;
    Segment *pSegment1 = g_malloc(((nChannels + 1) / 2) * nWidth * sizeof(Segment));
    Segment *pSegment2;

    if (nChannels > 1)
    {
        pSegment2 = g_malloc((nChannels / 2) * nWidth * sizeof(Segment));
    }
    else
    {
        pSegment2 = NULL;
    }
    
    gint nSegment1 = 0;
    gint nSegment2 = 0;
    gint nMin = 0;
    gint nMax = 0;
    
    for (gint nChannel = 0; nChannel < nChannels; nChannel++)
    {
        Segment *pSegmentCurr;
        gint nSegmentCurr;
        gint *pSegment;
        
        if ((nChannel & 1) == 0)
        {
            pSegmentCurr = pSegment1;
            pSegment = &nSegment1;
        }
        else
        {
            pSegmentCurr = pSegment2;
            pSegment = &nSegment2;
        }

        nSegmentCurr = *pSegment;

        gfloat fMiddle = ((nHeight * nChannel) / nChannels) + (nHeight / (2 * nChannels));
        gboolean bHasLast = FALSE;

        for (gint nWidthIter = 0; nWidthIter < nWidth; nWidthIter++)
        {
            if (pViewCache->lCalced[nWidthIter] == 0)
            {
                gchar *pDirtyPos = memchr(pViewCache->lCalced + nWidthIter, CALC_DIRTY, nWidth - nWidthIter);
                gchar *pCalcedPos = memchr(pViewCache->lCalced + nWidthIter, CALC_DONE, nWidth - nWidthIter);

                if (pDirtyPos == NULL || (pCalcedPos != NULL && pCalcedPos < pDirtyPos))
                {
                    pDirtyPos = pCalcedPos;
                }
                
                if (pDirtyPos == NULL)
                {
                    break;
                }
                
                nWidthIter = pDirtyPos - pViewCache->lCalced;
                bHasLast = FALSE;
            }

            gfloat fPositive = pViewCache->lValues[(nWidthIter * nChannels + nChannel) * 2] * fScale;
            gfloat fNegative = pViewCache->lValues[(nWidthIter * nChannels + nChannel) * 2 + 1] * fScale;

            if (pViewCache->nWidth == nWidth)//-
            {
                if (nWidthIter == nWidth - 1 && pViewCache->lValues[((nWidthIter - 2) * nChannels + nChannel) * 2] == 0 && pViewCache->lValues[((nWidthIter - 2) * nChannels + nChannel) * 2 + 1] == 0)
                {
                    fPositive = 0;
                    fNegative = 0;
                }
                else if (nWidthIter == 0 && pViewCache->lValues[(nChannels + nChannel) * 2] == 0 && pViewCache->lValues[(nChannels + nChannel) * 2 + 1] == 0)
                {
                    fPositive = 0;
                    fNegative = 0;
                }
            }

            if (fPositive < -1.0)
            {
                fPositive = -1.0;
            }
            else if (fPositive > 1.0)
            {
                fPositive = 1.0;
            }

            if (fNegative < -1.0)
            {
                fNegative = -1.0;
            }
            else if (fNegative > 1.0)
            {
                fNegative = 1.0;
            }

            gfloat fPosTop = -fPositive * nChannelHeight + fMiddle;
            gfloat fNegBottom = -fNegative * nChannelHeight + fMiddle;

            if (fPositive != 0)
            {
                fPosTop += 0.5;
            }

            if (fNegative != 0)
            {
                fNegBottom -= 0.5;
            }

            gint nNegBottom = (gint)fNegBottom;
            gint nPosTop = (gint)fPosTop;

            if (bHasLast)
            {
                if (nNegBottom > nMax)
                {
                    nNegBottom = nMax + 1;
                }
                else if (nPosTop < nMin)
                {
                    nPosTop = nMin - 1;
                }
            }

            pSegmentCurr[nSegmentCurr].nX1 = pSegmentCurr[nSegmentCurr].nX2 = nWidthIter;
            pSegmentCurr[nSegmentCurr].nY1 = nPosTop;
            pSegmentCurr[nSegmentCurr].nY2 = nNegBottom;
            nSegmentCurr++;
            nMin = nNegBottom;
            nMax = nPosTop;
            bHasLast = TRUE;
        }

        *pSegment = nSegmentCurr;
    }

    cairo_set_line_width(pCairo, 1);
    gdk_cairo_set_source_rgba(pCairo, &g_lColours[WAVE1]);

    while (nSegment1 > 0)
    {
        nSegment1--;
        cairo_move_to(pCairo, pSegment1[nSegment1].nX1 + 0.5, pSegment1[nSegment1].nY1);
        cairo_line_to(pCairo, pSegment1[nSegment1].nX2 + 0.5, pSegment1[nSegment1].nY2);
        cairo_stroke(pCairo);
    }

    gdk_cairo_set_source_rgba(pCairo, &g_lColours[WAVE2]);

    if (pSegment2 != NULL)
    {
        while (nSegment2 > 0)
        {
            nSegment2--;
            cairo_move_to(pCairo, pSegment2[nSegment2].nX1 + 0.5, pSegment2[nSegment2].nY1);
            cairo_line_to(pCairo, pSegment2[nSegment2].nX2 + 0.5, pSegment2[nSegment2].nY2);
            cairo_stroke(pCairo);
        }
    }

    g_free(pSegment1);
    g_free(pSegment2);
}
