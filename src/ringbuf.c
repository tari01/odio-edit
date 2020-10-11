/*
    Copyright (C) 2019-2020, Robert Tari <robert@tari.in>
    Copyright (C) 2002 2003 2005, Magnus Hjorth

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

#include <unistd.h>
#include "ringbuf.h"
#include "gstreamer.h"

Ringbuf *ringbuf_New()
{
    guint64 nBytes = (((sysconf(_SC_PHYS_PAGES) * sysconf(_SC_PAGESIZE)) / 2) / BUFFER_SIZE) * BUFFER_SIZE;
    Ringbuf *pRingbuf = g_malloc(sizeof(Ringbuf) + nBytes);
    pRingbuf->nBytes = nBytes;
    pRingbuf->nStart = 0;
    pRingbuf->nEnd = 0;

    return pRingbuf;
}

void ringbuf_Free(Ringbuf *pRingbuf)
{
    g_free(pRingbuf);
}

guint64 ringbuf_Available(Ringbuf *pRingbuf)
{
    guint64 nStart = pRingbuf->nStart;
    guint64 nEnd = pRingbuf->nEnd;

    if (nEnd >= nStart)
    {
        return nEnd - nStart;
    }
    else
    {
        return pRingbuf->nBytes + 1 + nEnd - nStart;
    }
}

guint64 ringbuf_Enqueue(Ringbuf *pRingbuf, gchar *lBytes, guint64 nBytes)
{
    guint64 nBlockSize;
    guint64 nBytesDone = 0;
    guint64 nStart = pRingbuf->nStart;
    guint64 nEnd = pRingbuf->nEnd;

    if (nEnd >= nStart)
    {
        if (nStart == 0)
        {
            nBlockSize = MIN(nBytes, pRingbuf->nBytes - nEnd);
        }
        else
        {
            nBlockSize = MIN(nBytes, 1 + pRingbuf->nBytes - nEnd);
        }

        if (nBlockSize == 0)
        {
            return 0;
        }
        
        memcpy(pRingbuf->lBytes + nEnd, lBytes, nBlockSize);
        nEnd += nBlockSize;

        if (nEnd == 1 + pRingbuf->nBytes)
        {
            nEnd = 0;
            nBytesDone = nBlockSize;
        }
        else
        {
            pRingbuf->nEnd = nEnd;
            
            return nBlockSize;
        }
    }

    nBlockSize = MIN(nBytes - nBytesDone, nStart - nEnd - 1);

    if (nBlockSize == 0)
    {
        pRingbuf->nEnd = nEnd;
        
        return nBytesDone;
    }

    memcpy(pRingbuf->lBytes + nEnd, G_STRUCT_MEMBER_P(lBytes, nBytesDone), nBlockSize);
    nEnd += nBlockSize;
    nBytesDone += nBlockSize;
    pRingbuf->nEnd = nEnd;

    return nBytesDone;
}

guint64 ringbuf_Dequeue(Ringbuf *pRingbuf, gchar *lBytes, guint64 nBytes)
{
    guint64 nBlockSize;
    guint64 nBytesDone = 0;
    guint64 nStart = pRingbuf->nStart;
    guint64 nEnd = pRingbuf->nEnd;

    if (nStart > nEnd)
    {
        nBlockSize = MIN(nBytes, pRingbuf->nBytes + 1 - nStart);
        memcpy(lBytes, pRingbuf->lBytes + nStart, nBlockSize);
        nStart += nBlockSize;

        if (nStart == pRingbuf->nBytes + 1)
        {
            nStart = 0;
            nBytesDone = nBlockSize;
        }
        else
        {
            pRingbuf->nStart = nStart;
            
            return nBlockSize;
        }
    }

    nBlockSize = MIN(nBytes - nBytesDone, nEnd - nStart);

    if (nBlockSize == 0)
    {
        pRingbuf->nStart=nStart;
        
        return nBytesDone;
    }

    memcpy(G_STRUCT_MEMBER_P(lBytes, nBytesDone), pRingbuf->lBytes + nStart, nBlockSize);
    nStart += nBlockSize;
    nBytesDone += nBlockSize;
    pRingbuf->nStart = nStart;

    return nBytesDone;
}
