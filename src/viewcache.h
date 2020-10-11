/*
    Copyright (C) 2019-2020, Robert Tari <robert@tari.in>
    Copyright (C) 2003 2004, Magnus Hjorth

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

#ifndef VIEWCACHE_H_INCLUDED
#define VIEWCACHE_H_INCLUDED

#include "chunk.h"

typedef struct
{
    gint nX1;
    gint nY1;
    gint nX2;
    gint nY2;
    
} Segment;

typedef struct
{
    Chunk *pChunk;
    ChunkHandle *pChunkHandle;
    gboolean bChunkError;
    gint64 nStart;
    gint64 nEnd;
    gint nWidth;
    gfloat *lValues;
    gint64 *lOffsets;
    gchar *lCalced;
    gfloat *lSamples;
    guint nBytes;
    gboolean bReading;

} ViewCache;

ViewCache *viewcache_New();
void viewcache_Reset(ViewCache *pViewCache);
void viewcache_Free(ViewCache *pViewCache);
gboolean viewcache_Update(ViewCache *pViewCache, Chunk *pChunk, gint64 nStartFrame, gint64 nEndFrame, gint nWidth, gint *nUpdatedLeft, gint *nUpdatedRight);
gboolean viewcache_Updated(ViewCache *pViewCache);
void viewcache_DrawPart(ViewCache *pViewCache, cairo_t *pCairo, gint nWidth, gint nHeight, gfloat fScale);

#endif
