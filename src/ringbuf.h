/*
    Copyright (C) 2019-2020, Robert Tari <robert@tari.in>
    Copyright (C) 2002 2003 2005, Magnus Hjorth

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

#ifndef RINGBUF_H_INCLUDED
#define RINGBUF_H_INCLUDED

#include <glib.h>

typedef struct
{

    volatile guint64 nStart;
    volatile guint64 nEnd;
    guint64 nBytes;
    gchar lBytes[1];

} Ringbuf;

Ringbuf *ringbuf_New();
void ringbuf_Free(Ringbuf *pRingbuf);
guint64 ringbuf_Available(Ringbuf *pRingbuf);
guint64 ringbuf_Enqueue(Ringbuf *pRingbuf, gchar *lBytes, guint64 nBytes);
guint64 ringbuf_Dequeue(Ringbuf *pRingbuf, gchar *lBytes, guint64 nBytes);

#endif
