/*
    Copyright (C) 2019-2020, Robert Tari <robert@tari.in>

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

#ifndef GSTREAMER_H_INCLUDED
#define GSTREAMER_H_INCLUDED

#include <gst/gst.h>
#include <gst/audio/audio-info.h>

#define BUFFER_SIZE 1008000
//#define BUFFER_SIZE 2016000
//#define BUFFER_SIZE 5019840
//#define BUFFER_SIZE 10019520

typedef struct
{
    GstPipeline *pPipeline;
    GList *lSignals;
    GList *lSignalsConnected;
    guint nWatchId;
    GstAudioInfo *pAudioInfo;
  
} GstBase;

void gstbase_Seek(GstBase *pGstBase, guint nFrame);
guint gstbase_GetPosition(GstBase *pGstBase);
void gstbase_Close(GstBase *pGstBase);

typedef struct
{
    GstBase *pGstBase;
    gchar *sFilePath;
    gfloat fDuration;
    guint nFrames;
    
} GstReader;

GstReader* gstreader_New(gchar * sPath);
guint gstreader_Read(GstReader* pGstReader, gchar *lBuffer, guint nStartFrame, guint nFramesToRead, gboolean bFloat);
void gstreader_Free(GstReader *pGstGstReader);

typedef struct
{
    GstBase *pGstBase;
    gchar *sFilePath;
    
} GstWriter;

GstWriter* gstwriter_New(gchar *sFilePath, GstAudioInfo *pAudioInfo);
guint gstwriter_Write(GstWriter *pGstWriter, guint nFrames, gchar *lBytes, gboolean bLast);
void gstwriter_Free(GstWriter *pGstGstWriter);

typedef void (*OnGetFrames)(gchar *lBuffer, guint nFrames, guint *nFramesRead);

typedef struct
{
    GstBase *pGstBase;
    OnGetFrames pOnGetFrames;
    gchar lBuffer[BUFFER_SIZE];
    
} GstPlayer;

GstPlayer* gstplayer_New(GstAudioInfo *pAudioInfo, OnGetFrames pOnGetFrames);
void gstplayer_Free(GstPlayer *pGstPlayer);

typedef gboolean (*OnConvert)(gfloat fProgress, gpointer pUserData);

typedef struct
{
    GstBase *pGstBase;
    guint nFrames;
    gchar *sFileIn;
    const gchar *sFormat;
    OnConvert pOnConvert;
    gpointer pUserData;
    
} GstConverter;

GstConverter* gstconverter_New(gchar *sFileIn, OnConvert pOnConvert, gpointer pUserData);
void gstconverter_ConvertBuffer(gchar *lFloat, gchar *lByte, guint nFrames, GstAudioInfo *pAudioInfo, gboolean bFromFloat);
gboolean gstconverter_ConvertFile(GstConverter *pGstConverter, gchar *sFileOut);
void gstconverter_Free(GstConverter *pGstConverter);

#endif
