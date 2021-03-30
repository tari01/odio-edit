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

#ifndef PLAYER_H_INCLUDED
#define PLAYER_H_INCLUDED

#include <glib.h>
#include "chunk.h"

typedef void (*OnNotify)(gint nPos, gboolean is_running);

gboolean player_Play(Chunk *pChunk, gint64 nStartPos, gint64 nEndPos, OnNotify pOnNotify);
void player_SetPos(gint64 nPos);
void player_Stop();
gboolean player_Playing();
gint64 player_GetPos();
void player_ChangeRange(gint64 nStart, gint64 nEnd);
void player_Switch(Chunk *pChunk, gint64 nMoveStart, gint64 nMoveDist);

#endif
