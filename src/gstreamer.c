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

#include <gst/app/gstappsrc.h>
#include <gst/app/gstappsink.h>
#include <gst/pbutils/pbutils.h>
#include "gstreamer.h"

typedef struct
{
    gchar *sName;
    gchar *sSignal;
    GCallback pCallback;
    gpointer pParent;
    
} Signal;

typedef struct
{
    GstElement *pElement;
    guint nId;
    
} SignalConnected;
 
typedef struct
{
    gchar *sName;
    gchar *sProperty;
    gchar *sType;
    guint nValue;
    GstCaps* pCaps;

} Property;

static GstBase* gstbase_New();
static void gstbase_Init(GstBase *pGstBase, gchar *sCommand, gboolean bBlock, gchar *sFilePath, GstAudioInfo *pAudioInfo);
static gboolean gstbase_OnError(GstBus *pBus, GstMessage *pMessage, gpointer pUserData);
static void gstbase_Pause(GstBase *pGstBase);
static void gstbase_Play(GstBase *pGstBase);
static void gstbase_AddSignal(GstBase *pGstBase, gchar *sName, gchar *sSignal, GCallback pCallback, gpointer pParent);
static void gstbase_Free(GstBase *pGstBase);
static void gstreader_OnPadAdded(GstElement *pDecoder, GstPad *pPad, gpointer pUserData);
static void gstplayer_OnNeedData(GstElement *pElement, guint nBytes, gpointer pUserData);
static gboolean gstplayer_OnSeekData(GstElement *pElement, guint nOffset, gpointer pUserData);

static gchar* string_Replace(gchar *sHaystack, gchar *sNeedle, gchar *sReplace, gboolean bFree)
{
    gchar **lParts = g_strsplit(sHaystack, sNeedle, -1);
    gchar *sResult = g_strjoinv(sReplace, lParts);
    g_strfreev(lParts);
    
    if (bFree)
    {
        g_free(sHaystack);
    }
    
    return sResult;
}

GstBase* gstbase_New()
{
    GstBase *pGstBase = g_malloc(sizeof(GstBase));
    pGstBase->pPipeline = NULL;
    pGstBase->lSignals = NULL;
    pGstBase->lSignalsConnected = NULL;
    pGstBase->nWatchId = 0;
    pGstBase->pAudioInfo = NULL;
    
    return pGstBase;
}

static void gstbase_Init(GstBase *pGstBase, gchar *sCommand, gboolean bBlock, gchar *sFilePath, GstAudioInfo *pAudioInfo)
{   
    if (pGstBase->pPipeline == NULL)
    {        
        gchar *sCommandNew = NULL;
        
        if (sFilePath != NULL)
        {   
            gchar *sFilePathNew = string_Replace(sFilePath, "\"", "\\\"", FALSE);
            sCommandNew = string_Replace(sCommand, "\tFILE\t", sFilePathNew, FALSE);
            g_free(sFilePathNew);
        }
        
        if (pAudioInfo != NULL)
        {
            GstCaps *pCaps = gst_audio_info_to_caps(pAudioInfo);
            gchar *sCaps = gst_caps_to_string(pCaps);  
            
            if (sCommandNew == NULL)  
            {  
                sCommandNew = string_Replace(sCommand, "\tCAPS\t", sCaps, FALSE);
            }
            else
            {
                sCommandNew = string_Replace(sCommandNew, "\tCAPS\t", sCaps, TRUE);
            }
            
            gst_caps_unref(pCaps);
            g_free(sCaps);
        }
        
        if (sFilePath == NULL && pAudioInfo == NULL)
        {
            sCommandNew = sCommand;
        }

        pGstBase->pPipeline = GST_PIPELINE_CAST(gst_parse_launch(sCommandNew, NULL));
        
        if (sFilePath != NULL || pAudioInfo != NULL)
        {
            g_free(sCommandNew);
        }
        
        GstBus *pBus = gst_pipeline_get_bus(pGstBase->pPipeline);

        g_assert(pGstBase->lSignalsConnected == NULL);

        for (GList *l = pGstBase->lSignals; l != NULL; l = l->next)
        {
            Signal *pSignal = (Signal *)(l->data);

            GstElement *pElement = NULL;

            if (pSignal->sName)
            {
                pElement = gst_bin_get_by_name(GST_BIN_CAST(pGstBase->pPipeline), pSignal->sName);
            }
            else
            {
                pElement = GST_ELEMENT_CAST(pBus);
            }
            
            SignalConnected *pSignalConnected = g_malloc(sizeof(SignalConnected));
            pSignalConnected->pElement = pElement;
            pSignalConnected->nId = g_signal_connect(pElement, pSignal->sSignal, pSignal->pCallback, pSignal->pParent);
            pGstBase->lSignalsConnected = g_list_append(pGstBase->lSignalsConnected, pSignalConnected);
        }
        
        gst_bus_add_signal_watch(pBus);
        pGstBase->nWatchId = g_signal_connect(pBus, "message::error", G_CALLBACK(gstbase_OnError), NULL);
        gst_object_unref(pBus);
    }
    
    gstbase_Pause(pGstBase);
    
    if (bBlock)
    {
        gst_element_get_state(GST_ELEMENT_CAST(pGstBase->pPipeline), NULL, NULL, GST_CLOCK_TIME_NONE);
    }
}

static GstAudioFormat gstbase_GetAudioFormat(GstAudioFormat nAudioFormat)
{
    switch (nAudioFormat)
    {
        case GST_AUDIO_FORMAT_F32BE:
        case GST_AUDIO_FORMAT_F64LE:
        case GST_AUDIO_FORMAT_F64BE:
        case GST_AUDIO_FORMAT_F32:
        {
            return GST_AUDIO_FORMAT_F32LE;
        }
        case GST_AUDIO_FORMAT_UNKNOWN:
        case GST_AUDIO_FORMAT_ENCODED:
        case GST_AUDIO_FORMAT_S16BE:
        case GST_AUDIO_FORMAT_U16LE:
        case GST_AUDIO_FORMAT_U16BE:
        case GST_AUDIO_FORMAT_S16:
        {
            return GST_AUDIO_FORMAT_S16LE;
        }
        case GST_AUDIO_FORMAT_S32BE:
        case GST_AUDIO_FORMAT_U32LE:
        case GST_AUDIO_FORMAT_U32BE:
        case GST_AUDIO_FORMAT_S32:
        {
            return GST_AUDIO_FORMAT_S32LE;
        }
        case GST_AUDIO_FORMAT_S24_32LE:
        case GST_AUDIO_FORMAT_S24_32BE:
        case GST_AUDIO_FORMAT_U24_32LE:
        case GST_AUDIO_FORMAT_U24_32BE:
        case GST_AUDIO_FORMAT_S24BE:
        case GST_AUDIO_FORMAT_U24LE:
        case GST_AUDIO_FORMAT_U24BE:
        case GST_AUDIO_FORMAT_S20LE:
        case GST_AUDIO_FORMAT_S20BE:
        case GST_AUDIO_FORMAT_U20LE:
        case GST_AUDIO_FORMAT_U20BE:
        case GST_AUDIO_FORMAT_S18LE:
        case GST_AUDIO_FORMAT_S18BE:
        case GST_AUDIO_FORMAT_U18LE:
        case GST_AUDIO_FORMAT_U18BE:
        case GST_AUDIO_FORMAT_S24:
        {
            return GST_AUDIO_FORMAT_S24LE;
        }
        default:
        {
            return nAudioFormat;
        }
    }
}

guint gstbase_GetPosition(GstBase *pGstBase)
{
    gint64 nPosition = 0;

    if (pGstBase->pPipeline)
    {
        gst_element_query_position(GST_ELEMENT_CAST(pGstBase->pPipeline), GST_FORMAT_TIME, &nPosition);

        if (nPosition == -1)
        {
            nPosition = 0;
        }
        
        nPosition = GST_CLOCK_TIME_TO_FRAMES(nPosition, pGstBase->pAudioInfo->rate);
    }
    
    return (guint)nPosition;
}

static void gstbase_Pause(GstBase *pGstBase)
{
    gst_element_set_state(GST_ELEMENT_CAST(pGstBase->pPipeline), GST_STATE_PAUSED);
}

void gstbase_Close(GstBase *pGstBase)
{    
    if (pGstBase->pPipeline != NULL)
    {
        for (GList *l = pGstBase->lSignalsConnected; l != NULL; l = l->next)
        {
            SignalConnected *pSignalConnected = (SignalConnected *)(l->data);
            g_signal_handler_disconnect(pSignalConnected->pElement, pSignalConnected->nId);
            g_object_unref(pSignalConnected->pElement);
            g_free(pSignalConnected);
        }

        g_list_free(pGstBase->lSignalsConnected);
        pGstBase->lSignalsConnected = NULL;

        if (pGstBase->pPipeline != NULL)
        {
            gst_element_set_state(GST_ELEMENT_CAST(pGstBase->pPipeline), GST_STATE_NULL);
            gst_element_get_state(GST_ELEMENT_CAST(pGstBase->pPipeline), NULL, NULL, GST_CLOCK_TIME_NONE);
        }
  
        GstBus *pBus = gst_pipeline_get_bus(pGstBase->pPipeline);
        g_signal_handler_disconnect(pBus, pGstBase->nWatchId);
        gst_bus_remove_signal_watch(pBus);
        gst_object_unref(pBus);
        
        for (GList *l = pGstBase->lSignals; l != NULL; l = l->next)
        {
            Signal *pSignal = (Signal *)(l->data);
            g_free(pSignal);
        }
        
        g_list_free(pGstBase->lSignals);
        pGstBase->lSignals = NULL;
        gst_object_unref(GST_OBJECT(pGstBase->pPipeline));
        pGstBase->pPipeline = NULL;
    }
}

static void gstbase_Free(GstBase *pGstBase)
{         
    if (pGstBase != NULL)
    {
        if (pGstBase->pAudioInfo != NULL)
        {
            gst_audio_info_free(pGstBase->pAudioInfo);
            pGstBase->pAudioInfo = NULL;
        } 

        gstbase_Close(pGstBase);
        g_free(pGstBase);
    }       
}

static gboolean gstbase_OnError(GstBus *pBus, GstMessage *pMessage, gpointer pData)
{   
    GError *pEerror;
    gst_message_parse_error(pMessage, &pEerror, NULL);
    g_critical("PANIC: %s\n", pEerror->message);
    g_error_free(pEerror);
    
    return TRUE;
}

static void gstbase_Play(GstBase *pGstBase)
{
    gst_element_set_state(GST_ELEMENT_CAST(pGstBase->pPipeline), GST_STATE_PLAYING);
}

void gstbase_Seek(GstBase *pGstBase, guint nFrame)
{    
    if (pGstBase->pPipeline)
    {    
        gint64 nTime = GST_FRAMES_TO_CLOCK_TIME(nFrame, pGstBase->pAudioInfo->rate);
        gst_element_seek_simple(GST_ELEMENT_CAST(pGstBase->pPipeline), GST_FORMAT_TIME, GST_SEEK_FLAG_ACCURATE | GST_SEEK_FLAG_FLUSH, nTime);
        gst_element_get_state(GST_ELEMENT_CAST(pGstBase->pPipeline), NULL, NULL, GST_CLOCK_TIME_NONE);
    }
}

static void gstbase_Wait(GstBase *pGstBase)
{
    GstBus *pBus = gst_pipeline_get_bus(pGstBase->pPipeline);
    GstMessage *pGstMessage = gst_bus_timed_pop_filtered(pBus, GST_CLOCK_TIME_NONE, GST_MESSAGE_EOS);

    if (pGstMessage != NULL)
    {
        gst_message_unref(pGstMessage);
    }

    gst_object_unref(pBus);
}

static void gstbase_AddSignal(GstBase *pGstBase, gchar *sName, gchar *sSignal, GCallback pCallback, gpointer pParent)
{    
    Signal *pSignal = g_malloc(sizeof(Signal));
    pSignal->sName = sName;
    pSignal->sSignal = sSignal;
    pSignal->pCallback = pCallback;
    pSignal->pParent = pParent;
    pGstBase->lSignals = g_list_append(pGstBase->lSignals, pSignal);
}

GstReader* gstreader_New(gchar * sPath)
{ 
    GstReader *pGstReader = g_malloc(sizeof(GstReader));
    pGstReader->pGstBase = gstbase_New();
    pGstReader->sFilePath = sPath;
    pGstReader->fDuration = 0.0;
    pGstReader->nFrames = 0;
    gstbase_AddSignal(pGstReader->pGstBase, "decode", "pad-added", G_CALLBACK(gstreader_OnPadAdded), pGstReader);
    gstbase_Init(pGstReader->pGstBase, "filesrc location=\"\tFILE\t\" ! decodebin name=decode ! audioconvert ! audio/x-raw ! fakesink", TRUE, pGstReader->sFilePath, NULL);
    gstbase_Play(pGstReader->pGstBase);
    gstbase_Wait(pGstReader->pGstBase);
    gstbase_Close(pGstReader->pGstBase);

    return pGstReader;
}

static void gstreader_OnPadAdded(GstElement *pDecoder, GstPad *pPad, gpointer pData)
{
    GstReader *pGstReader = (GstReader*)pData;
    GstCaps *pCaps = gst_pad_get_current_caps(pPad);
    pGstReader->pGstBase->pAudioInfo = gst_audio_info_new();
    gst_audio_info_from_caps(pGstReader->pGstBase->pAudioInfo, pCaps);
    pGstReader->pGstBase->pAudioInfo->layout = GST_AUDIO_LAYOUT_INTERLEAVED;   
    GstAudioFormat nAudioFormat = gstbase_GetAudioFormat(pGstReader->pGstBase->pAudioInfo->finfo->format);

    if (nAudioFormat != pGstReader->pGstBase->pAudioInfo->finfo->format)
    {
        GstAudioInfo *pAudioInfoNew = gst_audio_info_new();
        gst_audio_info_set_format(pAudioInfoNew, nAudioFormat, pGstReader->pGstBase->pAudioInfo->rate, pGstReader->pGstBase->pAudioInfo->channels, pGstReader->pGstBase->pAudioInfo->position);
        gst_audio_info_free(pGstReader->pGstBase->pAudioInfo);
        pGstReader->pGstBase->pAudioInfo = pAudioInfoNew;
    }

    gint64 nDuration = 0;
    gst_element_query_duration(GST_ELEMENT_CAST(pGstReader->pGstBase->pPipeline), GST_FORMAT_TIME, &nDuration);
    pGstReader->fDuration = (gfloat)((gdouble)nDuration / (gdouble)GST_SECOND);
    pGstReader->nFrames = GST_CLOCK_TIME_TO_FRAMES(nDuration, pGstReader->pGstBase->pAudioInfo->rate);
    gst_caps_unref(pCaps);   
    GstBus *pBus = gst_pipeline_get_bus(pGstReader->pGstBase->pPipeline);
    GstMessage *pGstMessage = gst_message_new_eos(GST_OBJECT_CAST(pDecoder));
    gst_bus_post(pBus, pGstMessage);   
    gst_object_unref(pBus);
}

guint gstreader_Read(GstReader* pGstReader, gchar *lBuffer, guint nStartFrame, guint nFramesToRead, gboolean bFloat)
{   
    guint nSampleWidth = bFloat ? 4 : (pGstReader->pGstBase->pAudioInfo->finfo->width / 8);
    guint64 nBytesToRead = nFramesToRead * pGstReader->pGstBase->pAudioInfo->channels * nSampleWidth;
    guint64 nBytesRead = 0;

    if (pGstReader->pGstBase->pPipeline == NULL)
    {
        const gchar *sFormat;

        if (bFloat && pGstReader->pGstBase->pAudioInfo->finfo->flags & GST_AUDIO_FORMAT_FLAG_INTEGER)
        {
            sFormat = "F32LE";
        }
        else
        {
            sFormat = pGstReader->pGstBase->pAudioInfo->finfo->name;
        }
        
        gchar *sCommand = g_strdup_printf("filesrc location=\"\tFILE\t\" name=src ! decodebin ! audioconvert ! audio/x-raw, format=%s, layout=interleaved ! appsink name=sink sync=FALSE", sFormat);
        gstbase_Init(pGstReader->pGstBase, sCommand, TRUE, pGstReader->sFilePath, NULL);
        g_free(sCommand); 
    }

    gstbase_Seek(pGstReader->pGstBase, nStartFrame);
    gstbase_Play(pGstReader->pGstBase);
    GstAppSink *pAppSink = GST_APP_SINK_CAST(gst_bin_get_by_name(GST_BIN_CAST(pGstReader->pGstBase->pPipeline), "sink"));

    while (!gst_app_sink_is_eos(pAppSink))
    {
        GstSample *pSample = gst_app_sink_pull_sample(pAppSink);

        if (pSample)
        {
            GstBuffer *pBuffer = gst_sample_get_buffer(pSample);
            gint64 nWantOffset = (nBytesRead / (pGstReader->pGstBase->pAudioInfo->channels * nSampleWidth)) + nStartFrame;
        
            if (pBuffer->offset != nWantOffset)
            {
                g_warning("Bad pBuffer->offset: %"G_GINT64_FORMAT" %"G_GUINT64_FORMAT, nWantOffset, pBuffer->offset);
            }

            GstMapInfo pMapInfo;
            gboolean bSuccess = gst_buffer_map(pBuffer, &pMapInfo, GST_MAP_READ);

            if (bSuccess)
            {           
                gint64 nRead = MIN(pMapInfo.size, nBytesToRead - nBytesRead);
                memcpy(lBuffer + nBytesRead, pMapInfo.data, nRead);
                nBytesRead += nRead;
                gst_buffer_unmap(pBuffer, &pMapInfo);
            }
            
            gst_sample_unref(pSample);
        }
        
        if (nBytesRead == nBytesToRead)
        {
            break;
        }
    }

    gst_object_unref(pAppSink);
    gstbase_Pause(pGstReader->pGstBase);

    if (nBytesRead < nBytesToRead)
    {
        memset(lBuffer + nBytesRead, 0, nBytesToRead - nBytesRead);
        g_warning("EOS: Padded with %"G_GUINT64_FORMAT" frames",  (nBytesToRead - nBytesRead) / (pGstReader->pGstBase->pAudioInfo->channels * nSampleWidth));
    }

    return nFramesToRead;
}

void gstreader_Free(GstReader *pGstReader)
{
    gstbase_Free(pGstReader->pGstBase);
    pGstReader->pGstBase = NULL;
    g_free(pGstReader);
    pGstReader = NULL;
}

GstWriter* gstwriter_New(gchar *sFilePath, GstAudioInfo *pAudioInfo)
{ 
    GstWriter *pGstWriter = g_malloc(sizeof(GstWriter));
    pGstWriter->pGstBase = gstbase_New();
    pGstWriter->pGstBase->pAudioInfo = pAudioInfo;
    gstbase_Init(pGstWriter->pGstBase, "appsrc name=src block=TRUE caps=\"\tCAPS\t\" ! wavenc ! filesink location=\"\tFILE\t\"", FALSE, sFilePath, pAudioInfo); 
    gstbase_Play(pGstWriter->pGstBase);

    return pGstWriter;
}

guint gstwriter_Write(GstWriter *pGstWriter, guint nFrames, gchar *lBytes, gboolean bLast)
{
    GstBuffer *pBuffer = gst_buffer_new_and_alloc(nFrames * pGstWriter->pGstBase->pAudioInfo->bpf);
    GstMapInfo pMapInfo;
    gboolean bSuccess = gst_buffer_map(pBuffer, &pMapInfo, GST_MAP_WRITE);
    
    if (bSuccess)
    {           
        memcpy(pMapInfo.data, lBytes, nFrames * pGstWriter->pGstBase->pAudioInfo->bpf);
        gst_buffer_unmap(pBuffer, &pMapInfo);
    }
    else
    {
        g_critical("gstwriter_Write: gst_buffer_map failed");
        
        return 0;
    }
    
    GstAppSrc *pAppSrc = GST_APP_SRC_CAST(gst_bin_get_by_name(GST_BIN_CAST(pGstWriter->pGstBase->pPipeline), "src"));
    GstFlowReturn pFlowReturn = gst_app_src_push_buffer(pAppSrc, pBuffer);

    if (pFlowReturn != GST_FLOW_OK)
    {
        gst_object_unref(pAppSrc);
        g_error("gstwriter_Write: pFlowReturn is %i", pFlowReturn);
        
        return 0;
    }
    
    while (gst_app_src_get_current_level_bytes(pAppSrc) != 0)
    {
        g_usleep(1000);        
    }

    if (bLast)
    {   
        gst_app_src_end_of_stream(pAppSrc);
        gstbase_Wait(pGstWriter->pGstBase);
    }
    
    gst_object_unref(pAppSrc);

    return nFrames;
}

void gstwriter_Free(GstWriter *pGstWriter)
{
    pGstWriter->pGstBase->pAudioInfo = NULL;
    gstbase_Free(pGstWriter->pGstBase);
    pGstWriter->pGstBase = NULL;
    g_free(pGstWriter);
    pGstWriter = NULL;
}

GstPlayer* gstplayer_New(GstAudioInfo *pAudioInfo, OnGetFrames pOnGetFrames)
{ 
    GstPlayer *pGstPlayer = g_malloc(sizeof(GstPlayer));
    pGstPlayer->pGstBase = gstbase_New();
    pGstPlayer->pGstBase->pAudioInfo = pAudioInfo;
    pGstPlayer->pOnGetFrames = pOnGetFrames;
    gstbase_AddSignal(pGstPlayer->pGstBase, "src", "need-data", G_CALLBACK(gstplayer_OnNeedData), pGstPlayer);
    gstbase_AddSignal(pGstPlayer->pGstBase, "src", "seek-data", G_CALLBACK(gstplayer_OnSeekData), pGstPlayer);
    gstbase_Init(pGstPlayer->pGstBase, "appsrc stream-type=GST_APP_STREAM_TYPE_RANDOM_ACCESS format=GST_FORMAT_TIME name=src caps=\"\tCAPS\t\" ! autoaudiosink", FALSE, NULL, pAudioInfo);   
    gstbase_Play(pGstPlayer->pGstBase);
    
    return pGstPlayer;
}
    
static void gstplayer_OnNeedData(GstElement *pElement, guint nBytes, gpointer pUserData)
{       
    GstPlayer *pGstPlayer = (GstPlayer*)pUserData;
    guint nFramesRead = 0;
    pGstPlayer->pOnGetFrames(pGstPlayer->lBuffer, BUFFER_SIZE / pGstPlayer->pGstBase->pAudioInfo->bpf, &nFramesRead);
   
    if (nFramesRead == 0)
    {
        return;
    }
    
    GstBuffer *pBuffer = gst_buffer_new_and_alloc(nFramesRead * pGstPlayer->pGstBase->pAudioInfo->bpf);
    GstMapInfo pMapInfo;
    gst_buffer_map(pBuffer, &pMapInfo, GST_MAP_WRITE);
    memcpy(pMapInfo.data, pGstPlayer->lBuffer, nFramesRead * pGstPlayer->pGstBase->pAudioInfo->bpf);
    gst_buffer_unmap(pBuffer, &pMapInfo);
    GstFlowReturn pFlowReturn = gst_app_src_push_buffer(GST_APP_SRC_CAST(pElement), pBuffer);

    if (pFlowReturn != GST_FLOW_OK && pFlowReturn != GST_FLOW_FLUSHING)
    {
        g_error("PANIC: Push error: %d", pFlowReturn);
    }
}

static gboolean gstplayer_OnSeekData(GstElement *pElement, guint nOffset, gpointer pData)
{
    return TRUE;
}

void gstplayer_Free(GstPlayer *pGstPlayer)
{
    pGstPlayer->pGstBase->pAudioInfo = NULL;
    gstbase_Free(pGstPlayer->pGstBase);
    pGstPlayer->pGstBase = NULL;
    g_free(pGstPlayer);
    pGstPlayer = NULL;
}

static void gstconverter_OnPadAdded(GstElement *pDecoder, GstPad *pPad, gpointer pData)
{
    GstConverter *pGstConverter = (GstConverter*)pData;
    GstCaps *pCaps = gst_pad_get_current_caps(pPad);
    pGstConverter->pGstBase->pAudioInfo = gst_audio_info_new();
    gst_audio_info_from_caps(pGstConverter->pGstBase->pAudioInfo, pCaps);
    pGstConverter->pGstBase->pAudioInfo->layout = GST_AUDIO_LAYOUT_INTERLEAVED;   
    GstAudioFormat nAudioFormat = gstbase_GetAudioFormat(pGstConverter->pGstBase->pAudioInfo->finfo->format);

    if (nAudioFormat != pGstConverter->pGstBase->pAudioInfo->finfo->format)
    {
        GstAudioInfo *pAudioInfoNew = gst_audio_info_new();
        gst_audio_info_set_format(pAudioInfoNew, nAudioFormat, pGstConverter->pGstBase->pAudioInfo->rate, pGstConverter->pGstBase->pAudioInfo->channels, pGstConverter->pGstBase->pAudioInfo->position);
        gst_audio_info_free(pGstConverter->pGstBase->pAudioInfo);
        pGstConverter->pGstBase->pAudioInfo = pAudioInfoNew;
    }

    gint64 nDuration = 0;
    gst_element_query_duration(GST_ELEMENT_CAST(pGstConverter->pGstBase->pPipeline), GST_FORMAT_TIME, &nDuration);
    pGstConverter->nFrames = GST_CLOCK_TIME_TO_FRAMES(nDuration, pGstConverter->pGstBase->pAudioInfo->rate);
    pGstConverter->sFormat = pGstConverter->pGstBase->pAudioInfo->finfo->name;
    gst_caps_unref(pCaps);   
    GstBus *pBus = gst_pipeline_get_bus(pGstConverter->pGstBase->pPipeline);
    GstMessage *pGstMessage = gst_message_new_eos(GST_OBJECT_CAST(pDecoder));
    gst_bus_post(pBus, pGstMessage);   
    gst_object_unref(pBus);
}

GstConverter* gstconverter_New(gchar *sFileIn, OnConvert pOnConvert, gpointer pUserData)
{
    GstConverter *pGstConverter = g_malloc(sizeof(GstConverter));
    pGstConverter->pGstBase = gstbase_New();
    pGstConverter->sFileIn = sFileIn;
    pGstConverter->pOnConvert = pOnConvert;
    pGstConverter->pUserData = pUserData;
    pGstConverter->nFrames = 0;
    pGstConverter->sFormat = NULL;
    gstbase_AddSignal(pGstConverter->pGstBase, "decode", "pad-added", G_CALLBACK(gstconverter_OnPadAdded), pGstConverter);
    gstbase_Init(pGstConverter->pGstBase, "filesrc location=\"\tFILE\t\" ! decodebin name=decode ! audioconvert ! audio/x-raw ! fakesink", TRUE, sFileIn, NULL);
    gstbase_Play(pGstConverter->pGstBase);
    gstbase_Wait(pGstConverter->pGstBase);
    gstbase_Close(pGstConverter->pGstBase);
    
    return pGstConverter;
}

gboolean gstconverter_ConvertFile(GstConverter *pGstConverter, gchar *sFileOut)
{
    gchar *sFileIn = string_Replace(pGstConverter->sFileIn, "\"", "\\\"", FALSE);
    gchar *sCommand = g_strdup_printf("filesrc location=\"%s\" ! decodebin ! audioconvert ! audio/x-raw, format=%s, layout=interleaved ! wavenc ! filesink location=\"\tFILE\t\"", sFileIn, pGstConverter->sFormat);
    g_free(sFileIn);
    gstbase_Init(pGstConverter->pGstBase, sCommand, TRUE, sFileOut, NULL);
    g_free(sCommand); 
    gstbase_Play(pGstConverter->pGstBase);
    GstBus *pBus = gst_pipeline_get_bus(pGstConverter->pGstBase->pPipeline);
    
    while (1)
    {
        GstMessage *pMessage = gst_bus_timed_pop_filtered(pBus, 40 * GST_MSECOND, GST_MESSAGE_EOS);

        if (pMessage != NULL)
        {         
            gst_message_unref(pMessage);
            
            break;
        }
        else
        {
            guint nFrames = gstbase_GetPosition(pGstConverter->pGstBase);
            
            if (nFrames >= pGstConverter->nFrames)
            {
                break;
            }
            
            if (pGstConverter->pOnConvert((gfloat)nFrames / (gfloat)pGstConverter->nFrames, pGstConverter->pUserData))
            {
                gstbase_Pause(pGstConverter->pGstBase);
                gst_object_unref(pBus);
                
                return TRUE;
            }
        }
    }

    gst_object_unref(pBus);
    
    return FALSE;
}

void gstconverter_Free(GstConverter *pGstConverter)
{
    gstbase_Free(pGstConverter->pGstBase);
    pGstConverter->pGstBase = NULL;
    g_free(pGstConverter);
    pGstConverter = NULL;
}

void gstconverter_ConvertBuffer(gchar *lFloat, gchar *lByte, guint nFrames, GstAudioInfo *pAudioInfo, gboolean bFromFloat)
{ 
    GstBase *pGstBase = gstbase_New();
    GstAudioInfo *pAudioInfoIn;
    GstAudioInfo *pAudioInfoOut;
    gchar *lIn;
    gchar *lOut;
    guint64 nBytesIn;
    guint64 nBytesToConvert;
    guint64 nBytesConverted = 0;

    if (bFromFloat)
    {
        pAudioInfoIn = gst_audio_info_new();
        gst_audio_info_set_format(pAudioInfoIn, GST_AUDIO_FORMAT_F32LE, pAudioInfo->rate, pAudioInfo->channels, pAudioInfo->position);
        pAudioInfoOut = pAudioInfo;
        nBytesToConvert = nFrames * pAudioInfo->bpf;
        nBytesIn = nFrames * pAudioInfo->channels * 4;
        lOut = lByte;
        lIn = lFloat;
    }    
    else
    {
        pAudioInfoIn = pAudioInfo;
        pAudioInfoOut = gst_audio_info_new();
        gst_audio_info_set_format(pAudioInfoOut, GST_AUDIO_FORMAT_F32LE, pAudioInfo->rate, pAudioInfo->channels, pAudioInfo->position);
        nBytesToConvert = nFrames * pAudioInfo->channels * 4;
        nBytesIn = nFrames * pAudioInfo->bpf;
        lOut = lFloat;
        lIn = lByte;
    }
 
    gchar *sCommand = g_strdup_printf("appsrc name=src caps=\"\tCAPS\t\" ! audioconvert ! audio/x-raw, format=%s ! appsink name=sink sync=FALSE", pAudioInfoOut->finfo->name);
    gstbase_Init(pGstBase, sCommand, FALSE, NULL, pAudioInfoIn);
    g_free(sCommand);  
    gstbase_Play(pGstBase);
    GstBuffer *pBuffer = gst_buffer_new_and_alloc(nBytesIn);
    GstMapInfo pMapInfo;
    gst_buffer_map(pBuffer, &pMapInfo, GST_MAP_WRITE);
    memcpy(pMapInfo.data, lIn, nBytesIn);
    gst_buffer_unmap(pBuffer, &pMapInfo);
    GstAppSrc *pAppSrc = GST_APP_SRC_CAST(gst_bin_get_by_name(GST_BIN_CAST(pGstBase->pPipeline), "src"));
    gst_app_src_push_buffer(pAppSrc, pBuffer);
    gst_app_src_end_of_stream(pAppSrc);
    gst_object_unref(pAppSrc);
    GstAppSink *pAppSink = GST_APP_SINK_CAST(gst_bin_get_by_name(GST_BIN_CAST(pGstBase->pPipeline), "sink"));
    
    while (!gst_app_sink_is_eos(pAppSink))
    {
        GstSample *pSample = gst_app_sink_pull_sample(pAppSink);

        if (pSample)
        {
            GstBuffer *pBufferOut = gst_sample_get_buffer(pSample);
            GstMapInfo pMapInfo;
            gboolean bSuccess = gst_buffer_map(pBufferOut, &pMapInfo, GST_MAP_READ);

            if (bSuccess)
            {
                memcpy(lOut + nBytesConverted, pMapInfo.data, pMapInfo.size);
                nBytesConverted += pMapInfo.size;
                gst_buffer_unmap(pBufferOut, &pMapInfo);
            }
            
            gst_sample_unref(pSample);
        }
    }
    
    gst_object_unref(pAppSink);
    
    g_assert(nBytesConverted == nBytesToConvert);

    if (bFromFloat)
    {
        gst_audio_info_free(pAudioInfoIn);
    }    
    else
    {
        gst_audio_info_free(pAudioInfoOut);
    }

    gstbase_Free(pGstBase);
    pGstBase = NULL;
}
