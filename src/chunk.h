/*
    Copyright (C) 2019-2020, Robert Tari <robert@tari.in>
    Copyright (C) 2002 2003 2004 2005 2006 2007 2008 2009 2011, Magnus Hjorth

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

#ifndef CHUNK_H_INCLUDED
#define CHUNK_H_INCLUDED

#include <gtk/gtk.h>
#include "datasource.h"

#define OE_TYPE_CHUNK chunk_get_type()
G_DECLARE_FINAL_TYPE(Chunk, chunk, OE, CHUNK, GObject)

struct _MainWindow;

struct _Chunk
{
    GObject parent_instance;
    GstAudioInfo *pAudioInfo;
    GList *lParts;
    gint64 nFrames;
    gint64 nBytes;
    guint nOpenCount;
};

typedef struct
{
    DataSource *pDataSource;
    gint64 nPosition;
    gint64 nFrames;

} DataPart;

typedef Chunk ChunkHandle;

Chunk *chunk_NewFromDatasource(DataSource *pDataSource);
guint chunk_AliveCount();
ChunkHandle *chunk_Open(Chunk *pChunk, gboolean bPlayer);
//gboolean chunk_readFloat(ChunkHandle *pChunk, gint64 nStartFrame, gchar *lBuffer);
guint chunk_Read(ChunkHandle *pChunk, gint64 nStartFrame, guint nFrames, gchar *lBuffer, gboolean bFloat, gboolean bPlayer);
void chunk_Close(ChunkHandle *pChunk, gboolean bPlayer);
Chunk *chunk_Mix(Chunk *pChunk1, Chunk *pChunk2, struct _MainWindow *pMainWindow);
Chunk *chunk_Fade(Chunk *pChunk, gfloat fStartFactor, gfloat fEndFactor, struct _MainWindow *pMainWindow);
Chunk *chunk_Append(Chunk *pChunk, Chunk *pChunkPart);
//Chunk *chunk_InterpolateEndpoints(Chunk *pChunk, struct _MainWindow *pMainWindow);
Chunk *chunk_Insert(Chunk *pChunk, Chunk *pChunkPart, gint64 nPosition);
Chunk *chunk_GetPart(Chunk *pChunk, gint64 nStartFrame, gint64 nFrames);
Chunk *chunk_RemovePart(Chunk *pChunk, gint64 nStartFrame, gint64 nFrames);
Chunk *chunk_ReplacePart(Chunk *pChunk, gint64 nStartFrame, gint64 nFrames, Chunk *pChunkPart);
Chunk *chunk_Load(gchar *sFilePath, struct _MainWindow *pMainWindow);
gboolean chunk_Save(Chunk *chunk, gchar *sFilePath, struct _MainWindow *pMainWindow);

#endif
