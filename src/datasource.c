/*
    Copyright (C) 2019-2023, Robert Tari <robert@tari.in>
    Copyright (C) 2002 2003 2004 2005 2006 2009 2010 2012, Magnus Hjorth

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

#include <glib/gi18n.h>
#include <unistd.h>
#include "message.h"
#include "datasource.h"
#include "tempfile.h"

G_DEFINE_TYPE(DataSource, datasource, G_TYPE_OBJECT)

static GList *m_lDataSources = NULL;

DataSource *datasource_new()
{
    g_info("datasource_new");
    
    return g_object_new(OE_TYPE_DATASOURCE, NULL);
}

guint datasource_Count()
{
    return g_list_length(m_lDataSources);
}

static void datasource_init(DataSource *pDataSource)
{
    m_lDataSources = g_list_append(m_lDataSources, pDataSource);
    pDataSource->nType = DATASOURCE_SILENCE;
    pDataSource->pAudioInfo = NULL;
    pDataSource->nFrames = 0;
    pDataSource->nBytes = 0;
    pDataSource->nOpenCountData = 0;
    pDataSource->nOpenCountPlayer = 0;
}

static void datasource_OnDispose(GObject *pObject)
{
    g_info("datasource_OnDispose");
    DataSource *pDataSource = OE_DATASOURCE(pObject);

    g_assert(pDataSource->nOpenCountData == 0);
    g_assert(pDataSource->nOpenCountPlayer == 0);

    switch (pDataSource->nType)
    {
        case DATASOURCE_TEMPFILE:
        {
            if (pDataSource->pData.pVirtual.sFilePath)
            {
                unlink(pDataSource->pData.pVirtual.sFilePath);
            }
        }
        case DATASOURCE_REAL:
        {
            if (pDataSource->pData.lReal)
            {
                g_free(pDataSource->pData.lReal);
                pDataSource->pData.lReal = NULL;
            }

            break;
        }
        case DATASOURCE_GSTTEMP:
        {
            gchar *sFilePath;
            
            if (pDataSource->pData.pGstReader.sTempFilePath)
            {
                sFilePath = pDataSource->pData.pGstReader.sTempFilePath;
            }
            else
            {
                sFilePath = pDataSource->pData.pGstReader.sFilePath;
            }
                
            if (sFilePath)
            {
                file_Unlink(sFilePath);
            }
            
            if (pDataSource->pData.pGstReader.sTempFilePath)
            {
                g_free(pDataSource->pData.pGstReader.sTempFilePath);
                pDataSource->pData.pGstReader.sTempFilePath = NULL;
            }
        }
    }

    gst_audio_info_free(pDataSource->pAudioInfo);
    pDataSource->pAudioInfo = NULL;
    pDataSource->nType = DATASOURCE_SILENCE;
    m_lDataSources = g_list_remove(m_lDataSources, pObject);
    G_OBJECT_CLASS(datasource_parent_class)->dispose(pObject);
}

static void datasource_class_init(DataSourceClass *cls)
{
    G_OBJECT_CLASS(cls)->dispose = datasource_OnDispose;
}

gboolean datasource_Open(DataSource *pDataSource, gboolean bPlayer)
{
    if ((bPlayer && pDataSource->nOpenCountPlayer == 0) || (!bPlayer && pDataSource->nOpenCountData == 0))
    {
        switch (pDataSource->nType)
        {
            case DATASOURCE_TEMPFILE:
            {
                pDataSource->pData.pVirtual.pHandle = file_Open(pDataSource->pData.pVirtual.sFilePath, FILE_READ, TRUE);

                if (pDataSource->pData.pVirtual.pHandle == NULL)
                {
                    return TRUE;
                }

                pDataSource->pData.pVirtual.nPos = 0;

                break;
            }
            case DATASOURCE_GSTTEMP:
            {
                gchar *sFilePath;
                
                if (pDataSource->pData.pGstReader.sTempFilePath)
                {
                    sFilePath = pDataSource->pData.pGstReader.sTempFilePath;
                }
                else
                {
                    sFilePath = pDataSource->pData.pGstReader.sFilePath;
                }
    
                GstReader *pGstReader = gstreader_New(sFilePath);

                if (pGstReader == NULL)
                {
                    gchar *sMessage = g_strdup_printf(_("Could not open %s"), sFilePath);
                    message_Error(sMessage);
                    g_free(sMessage);

                    return TRUE;
                }
                    
                if (bPlayer)
                {
                    pDataSource->pData.pGstReader.pHandlePlayer = pGstReader;
                    pDataSource->pData.pGstReader.nPosPlayer = 0;
                }
                else
                {
                    pDataSource->pData.pGstReader.pHandleData = pGstReader;
                    pDataSource->pData.pGstReader.nPosData = 0;
                }

                break;
            }
        }
    }

    if (bPlayer)
    {
        pDataSource->nOpenCountPlayer++;
    }
    else
    {
        pDataSource->nOpenCountData++;
    }

    return FALSE;
}

void datasource_Close(DataSource *pDataSource, gboolean bPlayer)
{
    if (bPlayer)
    {
        g_assert(pDataSource->nOpenCountPlayer != 0);
        pDataSource->nOpenCountPlayer--;

        if (pDataSource->nOpenCountPlayer > 0)
        {
            return;
        }
    }
    else
    {
        g_assert(pDataSource->nOpenCountData != 0);
        pDataSource->nOpenCountData--;

        if (pDataSource->nOpenCountData > 0)
        {
            return;
        }
    }

    switch (pDataSource->nType)
    {
        case DATASOURCE_TEMPFILE:
        {
            file_Close(pDataSource->pData.pVirtual.pHandle, FALSE);
            
            break;
        }
        case DATASOURCE_GSTTEMP:
        {
            if (bPlayer)
            {
                gstreader_Free(pDataSource->pData.pGstReader.pHandlePlayer);
                pDataSource->pData.pGstReader.pHandlePlayer = NULL;
            }
            else
            {
                gstreader_Free(pDataSource->pData.pGstReader.pHandleData);
                pDataSource->pData.pGstReader.pHandleData = NULL;
            }

            break;
        }
    }
}

guint datasource_Read(DataSource *pDataSource, gint64 nStartFrame, guint nFrames, gchar *lBuffer, gboolean bFloat, gboolean bPlayer)
{
    if (bPlayer)
    {
        g_assert(pDataSource->nOpenCountPlayer > 0);
    }
    else
    {
        g_assert(pDataSource->nOpenCountData > 0);
    }
    
    g_assert(nFrames <= pDataSource->nFrames);

    if (nFrames > pDataSource->nFrames - nStartFrame)
    {
        nFrames = (guint)(pDataSource->nFrames - nStartFrame);
    }

    if (nFrames == 0)
    {
        return 0;
    }
    
    guint nFrameSize = pDataSource->pAudioInfo->bpf;
    
    if (bFloat)
    {
        nFrameSize = pDataSource->pAudioInfo->channels * 4;
    }

    switch (pDataSource->nType)
    {
        case DATASOURCE_SILENCE:
        {
            memset(lBuffer, 0, nFrames * nFrameSize);

            return nFrames;
        }
        case DATASOURCE_REAL:
        {           
            if (bFloat && pDataSource->pAudioInfo->finfo->format != GST_AUDIO_FORMAT_F32LE)
            {
                gchar *lFloatBuffer = g_malloc(nFrames * nFrameSize);
                gstconverter_ConvertBuffer(lFloatBuffer, pDataSource->pData.lReal + (nStartFrame * pDataSource->pAudioInfo->bpf), nFrames, pDataSource->pAudioInfo, FALSE);
                memcpy(lBuffer, lFloatBuffer, nFrames * nFrameSize);
                g_free(lFloatBuffer);
            }
            else
            {
                memcpy(lBuffer, pDataSource->pData.lReal + (nStartFrame * nFrameSize), nFrames * nFrameSize);
            }
        
            return nFrames;
        }
        case DATASOURCE_TEMPFILE:
        {
            gint64 nStartByte = pDataSource->pData.pVirtual.nOffset + (nStartFrame * pDataSource->pAudioInfo->bpf);

            if (nStartByte != pDataSource->pData.pVirtual.nPos && file_Seek(pDataSource->pData.pVirtual.pHandle, nStartByte, SEEK_SET))
            {
                return 0;
            }

            pDataSource->pData.pVirtual.nPos = nStartByte;

            if (bFloat && pDataSource->pAudioInfo->finfo->format != GST_AUDIO_FORMAT_F32LE)
            {
                gchar *lBytes = g_malloc(nFrames * nFrameSize);
                
                if (file_Read(lBytes, nFrames * pDataSource->pAudioInfo->bpf, pDataSource->pData.pVirtual.pHandle))
                {
                    return 0;
                }
                
                gstconverter_ConvertBuffer(lBuffer, lBytes, nFrames, pDataSource->pAudioInfo, FALSE);
                g_free(lBytes);
            }
            else
            {
                if (file_Read(lBuffer, nFrames * pDataSource->pAudioInfo->bpf, pDataSource->pData.pVirtual.pHandle))
                {
                    return 0;
                }
            }
            
            pDataSource->pData.pVirtual.nPos += nFrames * pDataSource->pAudioInfo->bpf;
            
            return nFrames;
        }
        case DATASOURCE_GSTTEMP:
        {
            guint nFramesRead = 0;
            
            if (bPlayer)
            {
                pDataSource->pData.pGstReader.nPosPlayer = nStartFrame;
                nFramesRead = gstreader_Read(pDataSource->pData.pGstReader.pHandlePlayer, lBuffer, nStartFrame, nFrames, bFloat);

                if (nFramesRead > 0)
                {
                    pDataSource->pData.pGstReader.nPosPlayer += nFramesRead;
                }    
            }
            else
            {
                pDataSource->pData.pGstReader.nPosData = nStartFrame;
                nFramesRead = gstreader_Read(pDataSource->pData.pGstReader.pHandleData, lBuffer, nStartFrame, nFrames, bFloat);

                if (nFramesRead > 0)
                {
                    pDataSource->pData.pGstReader.nPosData += nFramesRead;
                }                
            }
            
            if (nFramesRead < nFrames)
            {
                gchar *sFilePath;
                
                if (pDataSource->pData.pGstReader.sTempFilePath)
                {
                    sFilePath = pDataSource->pData.pGstReader.sTempFilePath;
                }
                else
                {
                    sFilePath = pDataSource->pData.pGstReader.sFilePath;
                }
                
                gchar *sMessage = g_strdup_printf(_("Error reading %s: %s"), sFilePath, "Unexpected end of file");
                message_Error(sMessage);
                g_free(sMessage);
                
                return 0;
            }

            return nFramesRead;
        }
        default:
        {
            g_assert_not_reached();
        }

        return 0;
    }
}

/*DataSource *datasource_NewSilent(GstAudioInfo *pAudioInfo, gint64 nFrames)
{
    DataSource *pDataSource = datasource_new();
    pDataSource->pAudioInfo = gst_audio_info_copy(pAudioInfo);
    pDataSource->nType = DATASOURCE_SILENCE;
    pDataSource->nFrames = nFrames;
    pDataSource->nBytes = nFrames * pDataSource->pAudioInfo->bpf;

    return pDataSource;
}*/
