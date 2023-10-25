/*
    Copyright (C) 2019-2023, Robert Tari <robert@tari.in>
    Copyright (C) 2002 2003 2004 2005 2006 2009, Magnus Hjorth

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

#ifndef DATASOURCE_H_INCLUDED
#define DATASOURCE_H_INCLUDED

#include "file.h"
#include "gstreamer.h"

#define OE_TYPE_DATASOURCE datasource_get_type()
G_DECLARE_FINAL_TYPE(DataSource, datasource, OE, DATASOURCE, GObject)

#define DATASOURCE_REAL 1
#define DATASOURCE_TEMPFILE 2
#define DATASOURCE_SILENCE 3
#define DATASOURCE_GSTTEMP 4

struct _DataSource
{
    GObject parent_instance;
    gint nType;
    GstAudioInfo *pAudioInfo;
    gint64 nFrames;
    gint64 nBytes;
    guint nOpenCountData;
    guint nOpenCountPlayer;

    union
    {
        struct
        {
            gchar *sFilePath;
            gint64 nOffset;
            File *pHandle;
            gint64 nPos;

        } pVirtual;

        gchar *lReal;

        struct
        {
            gchar *sFilePath;
            gchar *sTempFilePath;
            GstReader *pHandleData;
            GstReader *pHandlePlayer;
            gint64 nPosData;
            gint64 nPosPlayer;

        } pGstReader;
        
    } pData;
};

DataSource *datasource_new();
//DataSource *datasource_NewSilent(GstAudioInfo *pAudioInfo, gint64 nFrames);
gboolean datasource_Open(DataSource *pDataSource, gboolean bPlayer);
void datasource_Close(DataSource *pDataSource, gboolean bPlayer);
guint datasource_Read(DataSource *pDataSource, gint64 nStartFrame, guint nFrames, gchar *lBuffer, gboolean bFloat, gboolean bPlayer);
guint datasource_Count();

#endif
