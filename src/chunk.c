/*
    Copyright (C) 2019-2023, Robert Tari <robert@tari.in>
    Copyright (C) 2002 2003 2004 2005 2006 2007 2008 2009 2011 2012, Magnus Hjorth

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

#include <libodiosacd/libodiosacd.h>
#include <glib/gi18n.h>
#include "message.h"
#include "chunk.h"
#include "tempfile.h"
#include "main.h"
#include "mainwindow.h"

G_DEFINE_TYPE(Chunk, chunk, G_TYPE_OBJECT)

typedef struct
{
    struct _MainWindow *pMainWindow;
    gchar *sFilePath;
    gfloat fProgress;
    gboolean bCancel;

} ConvertParams;

static GList *m_lChunks = NULL;

guint chunk_AliveCount()
{
    return g_list_length(m_lChunks);
}

static void chunk_OnDispose(GObject *pObject)
{
    g_info("chunk_OnDispose %p", pObject);
    Chunk *pChunk = OE_CHUNK(pObject);

    g_assert(pChunk->nOpenCount == 0);

    if (pChunk->lParts)
    {
        for (GList *l = pChunk->lParts; l != NULL; l = l->next)
        {
            DataPart *pDataPart = (DataPart *)l->data;
            g_info("datasource_unref: %d, chunk:chunk_OnDispose %p", datasource_Count(), pDataPart->pDataSource);
            g_object_unref(pDataPart->pDataSource);
            g_free(pDataPart);
        }

        g_list_free(pChunk->lParts);
    }

    gst_audio_info_free(pChunk->pAudioInfo);
    pChunk->lParts = NULL;
    G_OBJECT_CLASS(chunk_parent_class)->dispose(pObject);
    m_lChunks = g_list_remove(m_lChunks, pChunk);
}

static void chunk_class_init(ChunkClass *cls)
{
    G_OBJECT_CLASS(cls)->dispose = chunk_OnDispose;
}

static void chunk_init(Chunk *pObject)
{
    pObject->pAudioInfo = NULL;
    pObject->nFrames = 0;
    pObject->nBytes = 0;
    pObject->lParts = NULL;
    m_lChunks = g_list_append(m_lChunks, pObject);
}

static Chunk *chunk_new()
{
    Chunk *pChunk = g_object_new(OE_TYPE_CHUNK, NULL);
    g_info("chunk_new %p", pChunk);
    return pChunk;
}

static void chunk_CalcLength(Chunk *pChunk)
{
    gint64 nFrames = 0;

    for(GList *l = pChunk->lParts; l != NULL; l = l->next)
    {
        DataPart *pDataPart = (DataPart *)l->data;
        nFrames += pDataPart->nFrames;
    }

    pChunk->nFrames = nFrames;
    pChunk->nBytes = nFrames * pChunk->pAudioInfo->bpf;
}

Chunk *chunk_Mix(Chunk *pChunk1, Chunk *pChunk2, struct _MainWindow *pMainWindow)
{
    guint nFramesRead = 0;
    guint nTotalFramesRead = 0;
    gint64 nMixLen = MIN(pChunk1->nFrames, pChunk2->nFrames);
    ChunkHandle *pChunkHandle1 = chunk_Open(pChunk1, FALSE);

    if (pChunkHandle1 == NULL)
    {
        return NULL;
    }

    ChunkHandle *pChunkHandle2 = chunk_Open(pChunk2, FALSE);

    if (pChunkHandle2 == NULL)
    {
        chunk_Close(pChunkHandle1, FALSE);
        return NULL;
    }

    TempFile *pTempFile = tempfile_Init(pChunk1->pAudioInfo);
    mainwindow_BeginProgress(pMainWindow, _("Mixing"));
    gchar lBuffer1[BUFFER_SIZE];
    gchar lBuffer2[BUFFER_SIZE];
    gchar lBufferMixed[BUFFER_SIZE];

    for (guint nStartFrame = 0; nStartFrame < nMixLen; nStartFrame += nFramesRead)
    {
        guint nFramesRead1 = chunk_Read(pChunkHandle1, nStartFrame, BUFFER_SIZE / (pChunk1->pAudioInfo->channels * 4), (gchar*)lBuffer1, TRUE, FALSE);
        guint nFramesRead2 = chunk_Read(pChunkHandle2, nStartFrame, BUFFER_SIZE / (pChunk1->pAudioInfo->channels * 4), (gchar*)lBuffer2, TRUE, FALSE);

        if (nFramesRead1 == 0 || nFramesRead2 == 0)
        {
            tempfile_Abort(pTempFile);
            chunk_Close(pChunkHandle1, FALSE);
            chunk_Close(pChunkHandle2, FALSE);
            mainwindow_EndProgress(pMainWindow);

            return NULL;
        }

        nFramesRead = MIN(nFramesRead1, nFramesRead2);
        nTotalFramesRead += nFramesRead;

        for (guint nSample = 0; nSample < nFramesRead * pChunk1->pAudioInfo->channels; nSample++)
        {
            gfloat fSample = ((gfloat *)lBuffer1)[nSample] + ((gfloat *)lBuffer2)[nSample];

            if (fSample > 1.0)
            {
                fSample = 1.0;
            }
            else if (fSample < -1.0)
            {
                fSample = -1.0;
            }

            ((gfloat *)lBufferMixed)[nSample] = fSample;
        }

        gboolean bError = FALSE;

        if (pChunk1->pAudioInfo->finfo->format != GST_AUDIO_FORMAT_F32LE)
        {
            gchar *lBytes = g_malloc(nFramesRead * pChunk1->pAudioInfo->bpf);
            gstconverter_ConvertBuffer(lBufferMixed, lBytes, nFramesRead, pChunk1->pAudioInfo, TRUE);
            bError = tempfile_Write(pTempFile, lBytes, nFramesRead * pChunk1->pAudioInfo->bpf);
            g_free(lBytes);
            lBytes = NULL;
        }
        else
        {
            bError = tempfile_Write(pTempFile, lBufferMixed, nFramesRead * pChunk1->pAudioInfo->bpf);
        }

        if (bError || mainwindow_Progress(pMainWindow, GFLOAT(nTotalFramesRead) / GFLOAT(nMixLen)))
        {
            tempfile_Abort(pTempFile);
            chunk_Close(pChunkHandle1, FALSE);
            chunk_Close(pChunkHandle2, FALSE);
            mainwindow_EndProgress(pMainWindow);

            return NULL;
        }
    }

    chunk_Close(pChunkHandle1, FALSE);
    chunk_Close(pChunkHandle2, FALSE);
    Chunk *pChunkMixed = tempfile_Finished(pTempFile);
    mainwindow_EndProgress(pMainWindow);

    if (!pChunkMixed)
    {
        return NULL;
    }

    if (pChunk1->nFrames == pChunk2->nFrames)
    {
        return pChunkMixed;
    }

    Chunk *pChunkPart;

    if (pChunk1->nFrames > nMixLen)
    {
        pChunkPart = chunk_GetPart(pChunk1, nMixLen, pChunk1->nFrames - nMixLen);
    }
    else
    {
        pChunkPart = chunk_GetPart(pChunk2, nMixLen, pChunk2->nFrames - nMixLen);
    }

    Chunk *pChunkFinal = chunk_Append(pChunkMixed, pChunkPart);
    g_object_unref(pChunkMixed);
    g_object_unref(pChunkPart);

    return pChunkFinal;
}

Chunk *chunk_NewFromDatasource(DataSource *pDataSource)
{
    if (pDataSource == NULL)
    {
        return NULL;
    }

    DataPart *pDataPart = g_malloc(sizeof(*pDataPart));
    pDataPart->pDataSource = pDataSource;
    pDataPart->nPosition = 0;
    pDataPart->nFrames = pDataSource->nFrames;

    Chunk *pChunk = chunk_new();
    pChunk->pAudioInfo = gst_audio_info_copy(pDataSource->pAudioInfo);
    pChunk->nFrames = pDataSource->nFrames;
    pChunk->nBytes = pChunk->nFrames * pChunk->pAudioInfo->bpf;
    pChunk->lParts = g_list_append(NULL, pDataPart);

    return pChunk;
}

ChunkHandle *chunk_Open(Chunk *pChunk, gboolean bPlayer)
{
    for (GList *l = pChunk->lParts; l != NULL; l = l->next)
    {
        DataPart *pDataPart = (DataPart *)l->data;

        if (datasource_Open(pDataPart->pDataSource, bPlayer))
        {
            for (l = l->prev; l != NULL; l = l->prev)
            {
                DataPart *pDataPart = (DataPart *)l->data;
                datasource_Close(pDataPart->pDataSource, bPlayer);
            }

            return NULL;
        }
    }

    pChunk->nOpenCount++;

    return pChunk;
}

void chunk_Close(ChunkHandle *pChunkHandle, gboolean bPlayer)
{
    g_assert(pChunkHandle->nOpenCount > 0);

    for (GList *l = pChunkHandle->lParts; l != NULL; l = l->next)
    {
        DataPart *pDataPart = (DataPart *)l->data;
        datasource_Close(pDataPart->pDataSource, bPlayer);
    }

    pChunkHandle->nOpenCount--;
}

gboolean chunk_Save(Chunk *pChunk, gchar *sFilePath, struct _MainWindow *pMainWindow)
{
    if (g_file_test(sFilePath, G_FILE_TEST_EXISTS))
    {
        if (file_Unlink (sFilePath))
        {
            return TRUE;
        }
    }

    mainwindow_BeginProgress(pMainWindow, _("Saving"));
    gboolean bFatal = FALSE;
    gboolean bError = FALSE;

    GstWriter *pGstWriter = gstwriter_New(sFilePath, pChunk->pAudioInfo);

    if (!pGstWriter)
    {
        gchar *sMessage = g_strdup_printf(_("Failed to open '%s'!"), sFilePath);
        message_Error(sMessage);
        g_free(sMessage);
        bError = -1;
        goto END;
    }

    g_info("chunk_ref: %d, filetypes:chunk_Save %p", chunk_AliveCount(), pChunk);
    g_object_ref(pChunk);
    ChunkHandle *pChunkHandle = chunk_Open(pChunk, FALSE);

    if (pChunkHandle == NULL)
    {
        g_object_unref(pChunk);
        gstwriter_Free(pGstWriter);
        pGstWriter = NULL;
        bError = -1;

        goto END;
    }

    gchar pBuffer[BUFFER_SIZE];
    guint nFramesRead;
    guint nTotalFramesWritten = 0;

    for (guint nStartFrame = 0; nStartFrame < pChunk->nFrames; nStartFrame += nFramesRead)
    {
        nFramesRead = chunk_Read(pChunkHandle, nStartFrame, BUFFER_SIZE / pChunk->pAudioInfo->bpf, pBuffer, FALSE, FALSE);

        if (!nFramesRead)
        {
            chunk_Close(pChunkHandle, FALSE);
            gstwriter_Free(pGstWriter);
            pGstWriter = NULL;
            g_object_unref(pChunk);
            bFatal = TRUE;
            bError = -1;

            goto END;
        }

        guint nFramesWritten = gstwriter_Write(pGstWriter, nFramesRead, pBuffer, nStartFrame + nFramesRead == pChunk->nFrames);
        nTotalFramesWritten += nFramesWritten;

        if (nFramesWritten != nFramesRead)
        {
            gchar *sMessage = g_strdup_printf(_("Failed to write to '%s'!"), sFilePath);
            message_Error(sMessage);
            g_free(sMessage);
            chunk_Close(pChunkHandle, FALSE);
            gstwriter_Free(pGstWriter);
            pGstWriter = NULL;
            g_object_unref(pChunk);
            bFatal = TRUE;
            bError = -1;

            goto END;
        }

        if (mainwindow_Progress(pMainWindow, GFLOAT(nTotalFramesWritten) / GFLOAT(pChunk->nFrames)))
        {
            chunk_Close(pChunkHandle, FALSE);
            gstwriter_Free(pGstWriter);
            pGstWriter = NULL;
            file_Unlink(sFilePath);
            g_object_unref(pChunk);
            bError = -1;

            goto END;
        }
    }

    chunk_Close(pChunkHandle, FALSE);
    gstwriter_Free(pGstWriter);
    pGstWriter = NULL;
    g_info("chunk_unref: %d, filetypes:chunk_Save %p", chunk_AliveCount(), pChunk);
    g_object_unref(pChunk);

END:

    if (bError && bFatal && !mainwindow_Progress(pMainWindow, 0))
    {
        gchar *sMessage = g_strdup_printf(_("File %s may be destroyed since saving failed. Try to free some disk space and save again. If you exit now, the file's contents could be left in a bad state."), sFilePath);
        message_Warning(sMessage);
        g_free(sMessage);
    }

    mainwindow_EndProgress(pMainWindow);

    return bError;
}

static gboolean chunk_OnGstConvert(gfloat fProgress, gpointer pUserData)
{
    ConvertParams *pConvertParams = (ConvertParams*)pUserData;

    return mainwindow_Progress(pConvertParams->pMainWindow, fProgress);
}

static bool chunk_OnSacdConvert(gfloat fProgress, gchar *sFilePath, int nTrack, gpointer pUserData)
{
    ConvertParams *pConvertParams = (ConvertParams*)pUserData;
    pConvertParams->fProgress = fProgress / 100;

    if (sFilePath)
    {
        pConvertParams->sFilePath = g_strdup(sFilePath);
    }

    if (pConvertParams->sFilePath)
    {
        pConvertParams->fProgress = 1.0;
    }

    return !pConvertParams->bCancel;
}

static gboolean chunk_SacdConvert(gpointer pUserData)
{
    ConvertParams *pConvertParams = (ConvertParams*)pUserData;

    return odiolibsacd_Convert("/tmp/odio-edit/", 88200, chunk_OnSacdConvert, pConvertParams);
}

Chunk *chunk_Load(gchar *sFilePath, struct _MainWindow *pMainWindow)
{
    if (!g_file_test(sFilePath, G_FILE_TEST_EXISTS))
    {
        gchar *sMessage = g_strdup_printf(_("File %s does not exist!"), sFilePath);
        message_Error(sMessage);
        g_free(sMessage);

        return NULL;
    }

    if (!g_file_test(sFilePath, G_FILE_TEST_IS_REGULAR))
    {
        gchar *sMessage = g_strdup_printf(_("File %s is not a regular file!"), sFilePath);
        message_Error(sMessage);
        g_free(sMessage);

        return NULL;
    }

    guint nType;
    gchar *sTempFile = NULL;
    GstReader *pGstReaderData;
    gchar *sFilePathLower = g_utf8_strdown(sFilePath, -1);
    ConvertParams cConvertParams;
    cConvertParams.sFilePath = NULL;
    cConvertParams.pMainWindow = pMainWindow;
    cConvertParams.fProgress = 0.0;
    cConvertParams.bCancel = FALSE;

    if (g_str_has_suffix(sFilePathLower, ".dff") || g_str_has_suffix(sFilePathLower, ".dsf"))
    {
        bool bError = odiolibsacd_Open(sFilePath, AREA_AUTO);

        if (bError)
        {
            gchar *sMessage = g_strdup_printf(_("Failed to open '%s'"), sFilePath);
            message_Error(sMessage);
            g_free(sMessage);

            return NULL;
        }

        mainwindow_BeginProgress(pMainWindow, _("Loading"));
        GThread *pThread = g_thread_new(NULL, (GThreadFunc)chunk_SacdConvert, &cConvertParams);

        while (cConvertParams.fProgress < 1 && !cConvertParams.bCancel)
        {
            g_usleep(40000);
            cConvertParams.bCancel = mainwindow_Progress(pMainWindow, cConvertParams.fProgress);
        }

        bError = g_thread_join(pThread);

        if (bError)
        {
            gchar *sMessage = g_strdup_printf(_("Failed to decode '%s'"), sFilePath);
            message_Error(sMessage);
            g_free(sMessage);

            return NULL;
        }

        if (!cConvertParams.bCancel)
        {
            sTempFile = cConvertParams.sFilePath;
        }

        odiolibsacd_Close();
        mainwindow_EndProgress(pMainWindow);
    }
    else
    {
        sTempFile = tempfile_GetFileName();
        GstConverter *pGstConverter = gstconverter_New(sFilePath, chunk_OnGstConvert, &cConvertParams);
        mainwindow_BeginProgress(pMainWindow, _("Loading"));

        if (gstconverter_ConvertFile(pGstConverter, sTempFile))
        {
            file_Unlink(sTempFile);
            g_free(sTempFile);
            cConvertParams.bCancel = TRUE;
        }

        mainwindow_EndProgress(pMainWindow);
        gstconverter_Free(pGstConverter);
        pGstConverter = NULL;
    }

    g_free(sFilePathLower);

    if (cConvertParams.bCancel)
    {
        return NULL;
    }

    nType = DATASOURCE_GSTTEMP;
    pGstReaderData = gstreader_New(sTempFile);

    if (!pGstReaderData)
    {
        gchar *sMessage = g_strdup_printf(_("Failed to open '%s'"), sFilePath);
        message_Error(sMessage);
        g_free(sMessage);

        return NULL;
    }

    DataSource *pDataSource = datasource_new();
    pDataSource->nType = nType;
    pDataSource->pAudioInfo = gst_audio_info_copy(pGstReaderData->pGstBase->pAudioInfo);
    pDataSource->nFrames = pGstReaderData->nFrames;
    pDataSource->nBytes = pDataSource->nFrames * pDataSource->pAudioInfo->bpf;
    pDataSource->nOpenCountData = 1;
    pDataSource->nOpenCountPlayer = 0;
    pDataSource->pData.pGstReader.sFilePath = g_strdup(sFilePath);
    pDataSource->pData.pGstReader.sTempFilePath = sTempFile;
    pDataSource->pData.pGstReader.pHandleData = pGstReaderData;
    pDataSource->pData.pGstReader.nPosData = 0;
    pDataSource->pData.pGstReader.pHandlePlayer = NULL;
    pDataSource->pData.pGstReader.nPosPlayer = 0;
    datasource_Close(pDataSource, FALSE);
    Chunk *pChunk = chunk_NewFromDatasource(pDataSource);

    return pChunk;
}

guint chunk_Read(ChunkHandle *pChunk, gint64 nStartFrame, guint nFrames, gchar *lBuffer, gboolean bFloat, gboolean bPlayer)
{
    g_assert(nStartFrame < pChunk->nFrames);

    guint nFramesReadTotal = 0;

    for (GList *l = pChunk->lParts; l != NULL; l = l->next)
    {
        DataPart *pDataPart = (DataPart *)l->data;

        if (pDataPart->nFrames > nStartFrame)
        {
            guint nOffset = pDataPart->nFrames - nStartFrame;
            guint nFramesToRead = nFrames;

            if (nOffset < nFrames)
            {
                nFramesToRead = nOffset;
            }

            guint nFramesRead = datasource_Read(pDataPart->pDataSource, pDataPart->nPosition + nStartFrame, nFramesToRead, lBuffer, bFloat, bPlayer);

            g_assert(nFramesRead <= nFrames);
            g_assert(nFramesRead <= pDataPart->nFrames - nStartFrame);

            if (nFramesRead == 0)
            {
                return 0;
            }

            nFramesReadTotal += nFramesRead;
            nStartFrame = 0;
            nFrames -= nFramesRead;

            if (bFloat)
            {
                lBuffer += nFramesRead * pChunk->pAudioInfo->channels * 4;
            }
            else
            {
                lBuffer += nFramesRead * pChunk->pAudioInfo->bpf;
            }

            if (nFrames == 0 || l->next == NULL)
            {
                return nFramesReadTotal;
            }
        }
        else
        {
            nStartFrame -= pDataPart->nFrames;
        }
    }

    return nFramesReadTotal;
}

static DataPart *chunk_DatapartListCopy(GList *lSrc, GList **lDest)
{
    DataPart *pDataPartOld = lSrc->data;
    DataPart *pDataPartNew = g_malloc(sizeof(DataPart));
    pDataPartNew->pDataSource = pDataPartOld->pDataSource;
    pDataPartNew->nPosition = pDataPartOld->nPosition;
    pDataPartNew->nFrames = pDataPartOld->nFrames;
    g_info("datasource_ref %d, chunk:chunk_DatapartListCopy %p", datasource_Count(), pDataPartOld->pDataSource);
    g_object_ref(pDataPartOld->pDataSource);
    (*lDest) = g_list_append((*lDest), pDataPartNew);

    return pDataPartNew;
}

Chunk *chunk_Append(Chunk *pChunk, Chunk *pChunkPart)
{
    GList *lParts = NULL;
    gint64 nFrames = 0;

    for (GList *l = pChunk->lParts; l != NULL; l = l->next)
    {
        DataPart *pDataPart = chunk_DatapartListCopy(l, &lParts);
        nFrames += pDataPart->nFrames;
    }

    for (GList *l = pChunkPart->lParts; l != NULL; l = l->next)
    {
        DataPart *pDataPart = chunk_DatapartListCopy(l, &lParts);
        nFrames += pDataPart->nFrames;
    }

    Chunk *pChunkOut = chunk_new();
    pChunkOut->pAudioInfo = gst_audio_info_copy(pChunk->pAudioInfo);
    pChunkOut->lParts = lParts;
    pChunkOut->nFrames = nFrames;
    pChunkOut->nBytes = nFrames * pChunkOut->pAudioInfo->bpf;

    return pChunkOut;
}

Chunk *chunk_Insert(Chunk *pChunk, Chunk *pChunkPart, gint64 nStartFrame)
{
    GList *lParts = NULL;
    GList *l = NULL;

    g_assert(nStartFrame <= pChunk->nFrames);

    if (nStartFrame == pChunk->nFrames)
    {
        return chunk_Append(pChunk, pChunkPart);
    }

    for (l = pChunk->lParts; nStartFrame > 0; l = l->next)
    {
        DataPart *pDataPart = chunk_DatapartListCopy(l, &lParts);

        if (nStartFrame < pDataPart->nFrames)
        {
            pDataPart->nFrames = nStartFrame;

            break;
        }

        nStartFrame -= pDataPart->nFrames;
    }

    for (GList *ll = pChunkPart->lParts; ll != NULL; ll = ll->next)
    {
        chunk_DatapartListCopy(ll, &lParts);
    }

    for (; l != NULL; l = l->next)
    {
        DataPart *pDataPart = chunk_DatapartListCopy(l, &lParts);
        pDataPart->nPosition += nStartFrame;
        pDataPart->nFrames -= nStartFrame;
        nStartFrame = 0;
    }

    Chunk *pChunkOut = chunk_new();
    pChunkOut->pAudioInfo = gst_audio_info_copy(pChunk->pAudioInfo);
    pChunkOut->lParts = lParts;
    pChunkOut->nFrames = pChunk->nFrames + pChunkPart->nFrames;
    pChunkOut->nBytes = pChunk->nBytes + pChunkPart->nBytes;

    return pChunkOut;
}

Chunk *chunk_GetPart(Chunk *pChunk, gint64 nStartFrame, gint64 nFrames)
{
    GList *lParts = NULL;
    GList *l = pChunk->lParts;

    while (1)
    {
        DataPart *pDataPart = (DataPart *)l->data;

        if (nStartFrame < pDataPart->nFrames)
        {
            break;
        }

        nStartFrame -= pDataPart->nFrames;
        l = l->next;
    }

    while (nFrames > 0 && l != NULL)
    {
        DataPart *pDataPart = chunk_DatapartListCopy(l, &lParts);
        pDataPart->nPosition += nStartFrame;
        pDataPart->nFrames -= nStartFrame;
        nStartFrame = 0;

        if (pDataPart->nFrames > nFrames)
        {
            pDataPart->nFrames = nFrames;
        }

        nFrames -= pDataPart->nFrames;
        l = l->next;
    }

    Chunk *pChunkOut = chunk_new();
    pChunkOut->pAudioInfo = gst_audio_info_copy(pChunk->pAudioInfo);
    pChunkOut->lParts = lParts;
    chunk_CalcLength(pChunkOut);

    return pChunkOut;
}

Chunk *chunk_RemovePart(Chunk *pChunk, gint64 nStartFrame, gint64 nFrames)
{
    GList *lParts = NULL;
    GList *l = pChunk->lParts;

    while (nStartFrame > 0)
    {
        DataPart *pDataPart = chunk_DatapartListCopy(l, &lParts);

        if (nStartFrame < pDataPart->nFrames)
        {
            pDataPart->nFrames = nStartFrame;
            break;
        }

        nStartFrame -= pDataPart->nFrames;
        l = l->next;
    }

    nFrames += nStartFrame;

    while (nFrames > 0 && l != NULL)
    {
        DataPart *pDataPart = (DataPart *)l->data;

        if (pDataPart->nFrames > nFrames)
        {
            break;
        }

        nFrames -= pDataPart->nFrames;
        l = l->next;
    }

    for (; l != NULL; l = l->next)
    {
        DataPart *pDataPart = chunk_DatapartListCopy(l, &lParts);
        pDataPart->nPosition += nFrames;
        pDataPart->nFrames -= nFrames;
        nFrames = 0;
    }

    Chunk *pChunkOut = chunk_new();
    pChunkOut->pAudioInfo = gst_audio_info_copy(pChunk->pAudioInfo);
    pChunkOut->lParts = lParts;
    chunk_CalcLength(pChunkOut);

    return pChunkOut;
}

Chunk *chunk_ReplacePart(Chunk *pChunk, gint64 nStartFrame, gint64 nFrames, Chunk *pChunkPart)
{
    GList *lParts = NULL;
    guint nPos;

    g_assert(gst_audio_info_is_equal(pChunk->pAudioInfo, pChunkPart->pAudioInfo));

    for (GList *l = pChunkPart->lParts; l != NULL; l = l->next)
    {
        chunk_DatapartListCopy(l, &lParts);
    }

    Chunk *pChunkOut = chunk_RemovePart(pChunk, nStartFrame, nFrames);

    GList *l;

    for(l = pChunkOut->lParts, nPos = 0; nStartFrame > 0; l = l->next, nPos++)
    {
        DataPart *pDataPart = (DataPart *)l->data;

        g_assert(pDataPart->nFrames <= nStartFrame);

        nStartFrame -= pDataPart->nFrames;
    }

    for (GList *l = lParts; l != NULL; l = l->next, nPos++)
    {
        pChunkOut->lParts = g_list_insert(pChunkOut->lParts, l->data, nPos);
    }

    g_list_free(lParts);
    pChunkOut->nFrames += pChunkPart->nFrames;
    pChunkOut->nBytes = pChunkOut->nFrames * pChunkOut->pAudioInfo->bpf;

    return pChunkOut;
}

Chunk *chunk_Fade(Chunk *pChunk, gfloat fStartFactor, gfloat fEndFactor, struct _MainWindow *pMainWindow)
{
    ChunkHandle *pChunkHandle = chunk_Open(pChunk, FALSE);

    if (pChunkHandle == NULL)
    {
        return NULL;
    }

    gint64 nFramesDone = 0;
    gint64 nFramesLeft = pChunk->nFrames;
    gint64 nFramesPos = 0;
    TempFile *pTempFile = tempfile_Init(pChunk->pAudioInfo);
    mainwindow_BeginProgress(pMainWindow, _("Fading"));
    gchar lBuffer[BUFFER_SIZE];

    while (nFramesLeft > 0)
    {
        guint nFramesRead = chunk_Read(pChunkHandle, nFramesPos, MIN(BUFFER_SIZE / (pChunkHandle->pAudioInfo->channels * 4), nFramesLeft), lBuffer, TRUE, FALSE);

        if (!nFramesRead)
        {
            chunk_Close(pChunkHandle, FALSE);
            tempfile_Abort(pTempFile);
            mainwindow_EndProgress(pMainWindow);

            return NULL;
        }

        for (guint nFrame = 0; nFrame < nFramesRead; nFrame++)
        {
            gfloat fFactor = fStartFactor + ((fEndFactor - fStartFactor) * (GFLOAT(nFramesDone) / GFLOAT(pChunk->nFrames)));

            for (gint nChannel = 0; nChannel < pChunkHandle->pAudioInfo->channels; nChannel++)
            {
                ((gfloat *)lBuffer)[nFrame * pChunkHandle->pAudioInfo->channels + nChannel] *= fFactor;
            }

            nFramesDone++;
        }

        gboolean bError = FALSE;

        if (pChunkHandle->pAudioInfo->finfo->format != GST_AUDIO_FORMAT_F32LE)
        {
            gchar *lBytes = g_malloc(nFramesRead * pChunkHandle->pAudioInfo->bpf);
            gstconverter_ConvertBuffer(lBuffer, lBytes, nFramesRead, pChunkHandle->pAudioInfo, TRUE);
            bError = tempfile_Write(pTempFile, lBytes, nFramesRead * pChunkHandle->pAudioInfo->bpf);
            g_free(lBytes);
            lBytes = NULL;
        }
        else
        {
            bError = tempfile_Write(pTempFile, lBuffer, nFramesRead * pChunkHandle->pAudioInfo->bpf);
        }

        if (bError)
        {
            chunk_Close(pChunkHandle, FALSE);
            tempfile_Abort(pTempFile);
            mainwindow_EndProgress(pMainWindow);

            return NULL;
        }

        nFramesLeft -= nFramesRead;
        nFramesPos += nFramesRead;

        if (mainwindow_Progress(pMainWindow, GFLOAT(nFramesPos) / GFLOAT(pChunk->nFrames)))
        {
            chunk_Close(pChunkHandle, FALSE);
            tempfile_Abort(pTempFile);
            mainwindow_EndProgress(pMainWindow);

            return NULL;
        }
    }

    chunk_Close(pChunkHandle, FALSE);
    Chunk *pChunkFaded = tempfile_Finished(pTempFile);
    mainwindow_EndProgress(pMainWindow);

    return pChunkFaded;
}

/*Chunk* chunk_NewWithRamp(GstAudioInfo *pAudioInfo, gint64 nFrames, gfloat *startvals, gfloat *endvals, struct _MainWindow *pMainWindow)
{
    gint i, k;
    gfloat diffval[8], *sbuf;
    gfloat slength = GFLOAT(nFrames);
    TempFile *tf;
    gint64 ctr = 0;
    gint channels = pAudioInfo->channels;
    gint bufctr;
    Chunk *r;

    g_assert(channels <= 8);

    for (i = 0; i < channels; i++)
    {
        diffval[i] = endvals[i] - startvals[i];
    }

    tf = tempfile_Init(pAudioInfo);

    if (tf == NULL)
    {
        return NULL;
    }

    mainwindow_BeginProgress(pMainWindow, NULL);
    sbuf = g_malloc(BUFFER_SIZE);

    while (ctr < nFrames)
    {
        bufctr = BUFFER_SIZE / channels;

        if ((nFrames-ctr) < (gint64)bufctr)
        {
            bufctr = (guint)(nFrames-ctr);
        }

        for (i = 0; i < bufctr * channels; i += channels, ctr++)
        {
            for (k = 0; k < channels; k++)
            {
                sbuf[i + k] = startvals[k] + diffval[k] * ((GFLOAT(ctr)) / slength);
            }
        }

        if (tempfile_Write(tf, (gchar *)sbuf, bufctr * channels * 4) || mainwindow_Progress(pMainWindow, GFLOAT(bufctr) / GFLOAT(nFrames)))
        {
            g_free(sbuf);
            tempfile_Abort(tf);
            mainwindow_EndProgress(pMainWindow);

            return NULL;
        }
    }

    g_free(sbuf);
    r = tempfile_Finished(tf);
    mainwindow_EndProgress(pMainWindow);

    return r;
}*/

/*Chunk *chunk_InterpolateEndpoints(Chunk *pChunk, struct _MainWindow *pMainWindow)
{
    gint i;
    Chunk *start, *mid, *end, *c, *d;
    ChunkHandle *ch;

    gfloat startval[8], endval[8], zeroval[8];
    gint64 nFrames = pChunk->nFrames;

    ch = chunk_Open(pChunk, FALSE);

    if (ch == NULL)
    {
        return NULL;
    }

    if (chunk_readFloat(ch, 0, startval) || chunk_readFloat(ch, nFrames - 1, endval))
    {
        chunk_Close(ch, FALSE);
        return NULL;
    }

    chunk_Close(ch, FALSE);
    memset(zeroval, 0, sizeof(zeroval));
    i = pChunk->pAudioInfo->rate / 10;

    if (i < 1)
    {
        i = 1;
    }

    start = chunk_NewWithRamp(pChunk->pAudioInfo, i, startval, zeroval, pMainWindow);

    if (start == NULL)
    {
        return NULL;
    }

    end = chunk_NewWithRamp(pChunk->pAudioInfo, i, zeroval, endval, pMainWindow);

    if (end == NULL)
    {
        if (g_object_is_floating(start))
        {
            g_object_ref_sink(start);
            g_object_unref(start);
        }

        return NULL;
    }

    mid = chunk_NewFromDatasource(datasource_NewSilent(pChunk->pAudioInfo, pChunk->nFrames - 2 * i));

    g_assert(mid != NULL);

    c = chunk_Append(start, mid);
    d = chunk_Append(c, end);

    if (g_object_is_floating(start))
    {
        g_object_ref_sink(start);
        g_object_unref(start);
    }

    if (g_object_is_floating(mid))
    {
        g_object_ref_sink(mid);
        g_object_unref(mid);
    }

    if (g_object_is_floating(end))
    {
        g_object_ref_sink(end);
        g_object_unref(end);
    }

    if (g_object_is_floating(c))
    {
        g_object_ref_sink(c);
        g_object_unref(c);
    }

    return d;
}*/

/*gboolean chunk_readFloat(ChunkHandle *pChunkHandle, gint64 nStartFrame, gchar *lBuffer)
{
    for (GList *l = pChunkHandle->lParts; l != NULL; l = l->next)
    {
        DataPart *pDataPart = (DataPart *)l->data;

        if (pDataPart->nFrames > nStartFrame)
        {
            return (datasource_Read(pDataPart->pDataSource, pDataPart->nPosition + nStartFrame, 1, lBuffer, TRUE, FALSE) != 1);
        }
        else
        {
            nStartFrame -= pDataPart->nFrames;
        }
    }

    g_assert_not_reached();

    return TRUE;
}*/
