/*
    Copyright (C) 2019-2020, Robert Tari <robert@tari.in>
    Copyright (C) 2002 2003 2004 2005, Magnus Hjorth

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


#ifndef TEMPFILE_H_INCLUDED
#define TEMPFILE_H_INCLUDED

#include "file.h"
#include "ringbuf.h"
#include "chunk.h"

typedef struct
{
    File *pFile;
    gint64 nBytesWritten;
    Ringbuf *pRingbuf;
    GstAudioInfo *pAudioInfo;
    gchar lBuffer[64];
    guint nBufPos;
    
} TempFile;

TempFile* tempfile_Init(GstAudioInfo *pAudioInfo);
gboolean tempfile_Write(TempFile *pTempFile, gchar *lBuffer, guint nBytes);
void tempfile_Abort(TempFile *pTempFile);
Chunk *tempfile_Finished(TempFile *pTempFile);
gchar *tempfile_GetFileName();

#endif
