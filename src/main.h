/*
    Copyright (C) 2019-2021, Robert Tari <robert@tari.in>
    Copyright (C) 2002 2003 2004 2005 2006 2007 2008 2009 2010 2011 2012, Magnus Hjorth

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

#ifndef MAIN_H_INCLUDED
#define MAIN_H_INCLUDED

#include <gtk/gtk.h>

#define APPVERSION "22.7.5"
#define ARRAY_LENGTH(arr) (sizeof(arr)/sizeof((arr)[0]))
#define GFLOAT(x) ((gfloat)(x))
#define GDOUBLE(x) ((gdouble)(x))
#define GUINT32(x) ((guint32)(x))

enum Colours {TEXT, SCALE, BACKGROUND, WAVE1, WAVE2, CURSOR, STRIPE, SELECTION, BARS, LAST_COLOR};

extern gboolean g_bQuitFlag;
extern gboolean g_bIdleWork;
extern GdkRGBA g_lColours[LAST_COLOR];
extern GSettings *g_pGSettings;

gboolean checkExtension(GtkFileFilter *pFileFilter, gchar *sFilePath);
gchar *getTime(guint32 nSampleRate, gint64 nFrames, gchar *sTime, gboolean bFull);
void mainLoop();
#endif
