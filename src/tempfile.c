/*
    Copyright (C) 2019-2020, Robert Tari <robert@tari.in>
    Copyright (C) 2002 2003 2004 2005 2006, Magnus Hjorth

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

#include "tempfile.h"
#include "main.h"

G_LOCK_DEFINE_STATIC(TEMPFILE);

static gint m_nTempfiles = 0;

static guint8 *tempfile_CopyLE16(guint8 *lBytes, guint16 nValue)
{
    memcpy(lBytes, &nValue, 2);

    return lBytes + 2;
}

static guint8 *tempfile_CopyBE16(guint8 *lBytes, guint16 nValue)
{
    nValue = GUINT16_TO_BE(nValue);
    memcpy(lBytes, &nValue, 2);

    return lBytes + 2;
}

static guint8 *tempfile_CopyLE32(guint8 *lBytes, guint32 nValue)
{
    memcpy(lBytes, &nValue, 4);

    return lBytes + 4;
}

static guint8 *tempfile_CopyBE32(guint8 *lBytes, guint32 nValue)
{
    nValue = GUINT32_TO_BE(nValue);
    memcpy(lBytes, &nValue, 4);

    return lBytes + 4;
}

static gboolean tempfile_WriteWavHeader(File *pFile, GstAudioInfo *pAudioInfo, gint64 nBytes)
{    
    guint32 nLength = nBytes + 36;

    if (pAudioInfo->finfo->flags & GST_AUDIO_FORMAT_FLAG_FLOAT)
    {
        nLength = nBytes + 50;
    }
    
    if (GUINT32(nLength) < nBytes)
    {
        nLength = 0xFFFFFFFF;
    }
    
    guint8 lBuffer[58];
    guint8 *pBuffer = lBuffer;

    if (pAudioInfo->finfo->endianness == G_BIG_ENDIAN)
    {
        memcpy(pBuffer, "RIFX", 4);
    }
    else
    {
        memcpy(pBuffer, "RIFF", 4);
    }

    pBuffer += 4;
    pBuffer = (pAudioInfo->finfo->endianness == G_BIG_ENDIAN) ? tempfile_CopyBE32(pBuffer, nLength) : tempfile_CopyLE32(pBuffer, nLength);

    if (pAudioInfo->finfo->flags & GST_AUDIO_FORMAT_FLAG_FLOAT)
    {
        memcpy(pBuffer, "WAVEfmt \22\0\0\0\3\0", 14);
    }
    else
    {
        if (pAudioInfo->finfo->endianness == G_BIG_ENDIAN)
        {
            memcpy(pBuffer, "WAVEfmt \0\0\0\20\0\1", 14);
        }
        else
        {
            memcpy(pBuffer, "WAVEfmt \20\0\0\0\1\0", 14);
        }
    }

    pBuffer += 14;

    if (pAudioInfo->finfo->endianness == G_BIG_ENDIAN)
    {
        pBuffer = tempfile_CopyBE16(pBuffer, pAudioInfo->channels);
        pBuffer = tempfile_CopyBE32(pBuffer, pAudioInfo->rate);
        pBuffer = tempfile_CopyBE32(pBuffer, pAudioInfo->rate * pAudioInfo->bpf);
        pBuffer = tempfile_CopyBE16(pBuffer, pAudioInfo->bpf);
        pBuffer = tempfile_CopyBE16(pBuffer, pAudioInfo->finfo->width);
    }
    else
    {
        pBuffer = tempfile_CopyLE16(pBuffer, pAudioInfo->channels);
        pBuffer = tempfile_CopyLE32(pBuffer, pAudioInfo->rate);
        pBuffer = tempfile_CopyLE32(pBuffer, pAudioInfo->rate * pAudioInfo->bpf);
        pBuffer = tempfile_CopyLE16(pBuffer, pAudioInfo->bpf);
        pBuffer = tempfile_CopyLE16(pBuffer, pAudioInfo->finfo->width);
    }

    if (pAudioInfo->finfo->flags & GST_AUDIO_FORMAT_FLAG_FLOAT)
    {
        memcpy(pBuffer, "\0\0fact\4\0\0\0", 10);
        pBuffer += 10;
        nLength = nBytes / pAudioInfo->bpf;

        if (((gint64)nLength) < nBytes / pAudioInfo->bpf)
        {
            nLength = 0xFFFFFFFF;
        }

        pBuffer = tempfile_CopyLE32(pBuffer, nLength);
    }

    memcpy(pBuffer, "data", 4);
    pBuffer += 4;
    nLength = GUINT32(nBytes);

    if (GUINT32(nLength) < nBytes)
    {
        nLength = 0xFFFFFFFF;
    }
    
    pBuffer = (pAudioInfo->finfo->endianness == G_BIG_ENDIAN) ? tempfile_CopyBE32(pBuffer, nLength) : tempfile_CopyLE32(pBuffer, nLength);

    return file_Write((gchar *)lBuffer, (guint)((pBuffer - (guint8 *)lBuffer)), pFile);
}

gchar *tempfile_GetFileName()
{
    G_LOCK(TEMPFILE);

    gchar *sFileName = g_strdup_printf("/tmp/odio-edit/%d-%04d.wav", (gint)getpid(), ++m_nTempfiles);

    G_UNLOCK(TEMPFILE);

    return sFileName;
}

TempFile* tempfile_Init(GstAudioInfo *pAudioInfo)
{
    TempFile *pTempFile = g_malloc(sizeof(TempFile));
    pTempFile->pAudioInfo = pAudioInfo;
    pTempFile->pFile = NULL;
    pTempFile->nBytesWritten = 0;
    pTempFile->pRingbuf = ringbuf_New();

    g_assert(ringbuf_Available(pTempFile->pRingbuf) == 0);

    pTempFile->nBufPos = 0;

    return pTempFile;
}

static gboolean tempfile_writeMain(TempFile *pTempFile, gchar *lBytes, guint nBytes)
{
    if (nBytes == 0)
    {
        return FALSE;
    }

    if (pTempFile->pFile == NULL)
    {
        gchar *strFileName = tempfile_GetFileName();
        pTempFile->pFile = file_Open(strFileName, FILE_WRITE, FALSE);
        g_free(strFileName);

        if (pTempFile->pFile != NULL && tempfile_WriteWavHeader(pTempFile->pFile, pTempFile->pAudioInfo, 0x7FFFFFFF))
        {
            file_Close(pTempFile->pFile, TRUE);
            pTempFile->pFile = NULL;
        }
    }

    gboolean bError = file_Write(lBytes, nBytes, pTempFile->pFile);
    pTempFile->nBytesWritten += nBytes;

    return bError;
}

gboolean tempfile_Write(TempFile *pTempFile, gchar *lBuffer, guint nBytes)
{
    gchar *lOutBuffer = lBuffer;

    if (pTempFile->pRingbuf != NULL)
    {
        guint64 nBytesDone = ringbuf_Enqueue(pTempFile->pRingbuf, lOutBuffer, nBytes);

        if (nBytesDone == nBytes)
        {
            return FALSE;
        }

        lOutBuffer = lOutBuffer + nBytesDone;
        nBytes = nBytes - nBytesDone;
        gchar lDequeueBuffer[BUFFER_SIZE];

        while (1)
        {
            nBytesDone = ringbuf_Dequeue(pTempFile->pRingbuf, lDequeueBuffer, BUFFER_SIZE);

            if (nBytesDone == 0)
            {
                break;
            }

            if (tempfile_writeMain(pTempFile, lDequeueBuffer, nBytesDone))
            {
                return TRUE;
            }
        }

        ringbuf_Free(pTempFile->pRingbuf);
        pTempFile->pRingbuf = NULL;
    }

    return tempfile_writeMain(pTempFile, lOutBuffer, nBytes);
}

void tempfile_Abort(TempFile *pTempFile)
{
    if (pTempFile->pFile != NULL)
    {
        file_Close(pTempFile->pFile, TRUE);
    }

    if (pTempFile->pRingbuf != NULL)
    {
        ringbuf_Free(pTempFile->pRingbuf);
    }

    g_free(pTempFile);
}

Chunk *tempfile_Finished(TempFile *pTempFile)
{
    if (pTempFile->pRingbuf != NULL)
    {
        DataSource *pDataSource = datasource_new();
        pDataSource->nType = DATASOURCE_REAL;
        pDataSource->pAudioInfo = gst_audio_info_copy(pTempFile->pAudioInfo);
        guint64 nBytes = ringbuf_Available(pTempFile->pRingbuf);
        pDataSource->pData.lReal = g_malloc(nBytes);
        ringbuf_Dequeue(pTempFile->pRingbuf, pDataSource->pData.lReal, nBytes);
        pDataSource->nBytes = (gint64)nBytes;
        pDataSource->nFrames = (gint64)(nBytes / pDataSource->pAudioInfo->bpf);
        tempfile_Abort(pTempFile);

        return chunk_NewFromDatasource(pDataSource);
    }

    Chunk *pChunk = NULL;
    
    if (pTempFile->pFile == NULL)
    {
        goto END;
    }

    if (pTempFile->nBytesWritten == 0)
    {
        file_Close(pTempFile->pFile, TRUE);
        pTempFile->pFile = NULL;
        
        goto END;
    }

    if (file_Seek(pTempFile->pFile, 0, SEEK_SET) || tempfile_WriteWavHeader(pTempFile->pFile, pTempFile->pAudioInfo, pTempFile->nBytesWritten))
    {
        file_Close(pTempFile->pFile, TRUE);
        pTempFile->pFile = NULL;
        
        goto END;
    }

    gint64 nOffset = file_Tell(pTempFile->pFile);

    if (nOffset < 0)
    {
        file_Close(pTempFile->pFile, TRUE);
        pTempFile->pFile = NULL;
        
        goto END;
    }

    DataSource *pDataSource = datasource_new();
    pDataSource->nType = DATASOURCE_TEMPFILE;
    pDataSource->pAudioInfo = gst_audio_info_copy(pTempFile->pAudioInfo);
    pDataSource->nFrames = pTempFile->nBytesWritten / pTempFile->pAudioInfo->bpf;
    pDataSource->nBytes = pDataSource->nFrames * pDataSource->pAudioInfo->bpf;
    pDataSource->pData.pVirtual.sFilePath = g_strdup(pTempFile->pFile->sFilePath);
    pDataSource->pData.pVirtual.nOffset = nOffset;
    file_Close(pTempFile->pFile, FALSE);
    pTempFile->pFile = NULL;
    pChunk = chunk_NewFromDatasource(pDataSource);
    
END:
    
    tempfile_Abort(pTempFile);

    return pChunk;
}
