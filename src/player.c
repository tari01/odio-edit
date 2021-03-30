/*
    Copyright (C) 2019-2021, Robert Tari <robert@tari.in>

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

#include "player.h"

G_LOCK_DEFINE(PLAYER_LOCK);

static gboolean m_bPlaying = FALSE;
static GstPlayer *m_pGstPlayer = NULL;
static ChunkHandle *m_pChunkHandle = NULL;
static guint m_nCurPos = 0;
static guint m_nStartPos = 0;
static guint m_nEndPos = 0;
static OnNotify m_pOnNotify = NULL;

static gboolean player_OnNotify()
{   
    if (!m_bPlaying)
    {
        return FALSE;
    }
    
    guint nCurPos = gstbase_GetPosition(m_pGstPlayer->pGstBase);
        
    if (nCurPos >= m_nEndPos || nCurPos >= m_pChunkHandle->nFrames)
    {
        player_Stop();  
        
        return FALSE; 
    }
        
    m_pOnNotify(nCurPos, TRUE);        
    
    return TRUE;
}

static void player_OnGetFrames(gchar *lBuffer, guint nFrames, guint *nFramesRead)
{
    G_LOCK(PLAYER_LOCK);
    
    if (!m_bPlaying || m_nEndPos - m_nCurPos == 0)
    {
        *nFramesRead = 0;
    }
    else
    {
        *nFramesRead = chunk_Read(m_pChunkHandle, m_nCurPos, nFrames, lBuffer, FALSE, TRUE);
        m_nCurPos += *nFramesRead;
    }
    
    G_UNLOCK(PLAYER_LOCK);
}

gboolean player_Play(Chunk *pChunk, gint64 nStartPos, gint64 nEndPos, OnNotify pOnNotify)
{
    if (nStartPos == nEndPos)
    {
        return TRUE;
    }

    player_Stop();
    m_pChunkHandle = chunk_Open(pChunk, TRUE);
    g_info("chunk_ref: %d, player_Play %p", chunk_AliveCount(), m_pChunkHandle);
    g_object_ref(m_pChunkHandle);
    
    if (m_pGstPlayer == NULL)
    {
        m_pGstPlayer = gstplayer_New(pChunk->pAudioInfo, player_OnGetFrames);
    }

    m_nStartPos = nStartPos;
    m_nEndPos = nEndPos;
    m_bPlaying = TRUE;
    m_pOnNotify = pOnNotify;
    player_SetPos(nStartPos);
    g_timeout_add(40, player_OnNotify, NULL);

    return FALSE;
}

void player_SetPos(gint64 nPos)
{
    m_nCurPos = nPos;
    gstbase_Seek(m_pGstPlayer->pGstBase, nPos);
}

void player_Stop()
{
    if (!m_bPlaying)
    {    
        return;
    }
  
    m_bPlaying = FALSE;           
    
    G_LOCK(PLAYER_LOCK);
    
    if (m_pGstPlayer != NULL)
    {
        gstplayer_Free(m_pGstPlayer);
        m_pGstPlayer = NULL;
    }
        
    if (m_pChunkHandle != NULL)
    {
        chunk_Close(m_pChunkHandle, TRUE);
        g_info("chunk_unref: %d, player:player_Stop %p", chunk_AliveCount(), m_pChunkHandle);
        g_object_unref(m_pChunkHandle);
        m_pChunkHandle = NULL;
    }
        
    m_pOnNotify(-1, FALSE);
    
    G_UNLOCK(PLAYER_LOCK);
}

gboolean player_Playing()
{
    return m_bPlaying;
}

gint64 player_GetPos()
{
    return m_nCurPos;
}

void player_ChangeRange(gint64 nStart, gint64 nEnd)
{
    m_nStartPos = nStart;
    m_nEndPos = nEnd;
}

void player_Switch(Chunk *pChunk, gint64 nMoveStart, gint64 nMoveDist)
{
    if (m_pChunkHandle == NULL)
    {
        return;
    }

    gint64 nNewPos = m_nCurPos;

    if (nNewPos >= nMoveStart)
    {
        nNewPos += nMoveDist;

        if (nNewPos < nMoveStart)
        {
            player_Stop();
            
            return;
        }

        if (nNewPos > pChunk->nFrames)
        {
            player_Stop();
            
            return;
        }
    }

    gint64 nNewStart = m_nStartPos;
    ;
    if (nNewStart >= nMoveStart)
    {
        nNewStart += nMoveDist;

        if (nNewStart < nMoveStart)
        {
            nNewStart = nMoveStart;
        }

        if (nNewStart >= pChunk->nFrames)
        {
            nNewStart = pChunk->nFrames;
        }
    }

    gint64 nNewEnd = m_nEndPos;

    if (nNewEnd >= nMoveStart)
    {
        nNewEnd += nMoveDist;

        if (nNewEnd < nMoveStart)
        {
            nNewEnd = nMoveStart;
        }

        if (nNewEnd >= pChunk->nFrames)
        {
            nNewEnd = pChunk->nFrames;
        }
    }

    ChunkHandle *pChunkHandle = chunk_Open(pChunk, TRUE);

    if (pChunkHandle == NULL)
    {
        return;
    }

    chunk_Close(m_pChunkHandle, TRUE);
    g_info("chunk_unref: %d, player:player_Switch %p", chunk_AliveCount(), m_pChunkHandle);
    g_object_unref(m_pChunkHandle);
    m_pChunkHandle = pChunkHandle;
    g_info("chunk_ref: %d, player:player_Switch %p", chunk_AliveCount(), m_pChunkHandle);
    g_object_ref(m_pChunkHandle);

    m_nStartPos = nNewStart;
    m_nEndPos = nNewEnd;
    m_nCurPos = nNewPos;
}
