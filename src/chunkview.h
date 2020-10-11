/*
    Copyright (C) 2019-2020, Robert Tari <robert@tari.in>
    Copyright (C) 2002 2003 2004, Magnus Hjorth

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

#ifndef CHUNKVIEW_H_INCLUDED
#define CHUNKVIEW_H_INCLUDED

#include "document.h"
#include "viewcache.h"

#define OE_TYPE_CHUNK_VIEW chunkview_get_type()
G_DECLARE_FINAL_TYPE(ChunkView, chunkview, OE, CHUNK_VIEW, GtkDrawingArea)

struct _ChunkView
{
    GtkDrawingArea parent_instance;
    guint nWidth;
    guint nHeight;
    ViewCache *pViewCache;
    gint nLastRedrawTime;
    gint nNeedRedrawLeft;
    gint nNeedRedrawRight;
    gfloat fScaleFactor;
    Document *pDocument;
};

ChunkView *chunkview_new();
void chunkview_SetDocument(ChunkView *pChunkView, Document *pDocument);
gboolean chunkview_UpdateCache(ChunkView *pChunkView);
void chunkview_ForceRepaint(ChunkView *pChunkView);
void chunkview_SetScale(ChunkView *pChunkView, gfloat fScale);

#endif
