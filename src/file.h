/*
    Copyright (C) 2019-2023, Robert Tari <robert@tari.in>
    Copyright (C) 2002 2003 2004 2005 2006 2011, Magnus Hjorth

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

#ifndef FILE_H_INCLUDED
#define FILE_H_INCLUDED

#include <glib.h>

#define FILE_READ 0
#define FILE_WRITE 1

typedef struct
{
    gint nFile;
    gchar *sFilePath;

} File;

gboolean file_Unlink(gchar *sFilePath);
gint file_Rename(gchar *sOldname, gchar *sNewname);
File *file_Open(gchar *sFilePath, gint nMode, gboolean bReportErrors);
gboolean file_Close(File *pFile, gboolean bUnlink);
gboolean file_Seek(File *pFile, gint64 nByte, gint nWhence);
gboolean file_Read(gchar *lBytes, gint64 nBytes, File *pFile);
gboolean file_Write(gchar *lBytes, gint64 nBytes, File *pFile);
gint64 file_Tell(File *pFile);
gboolean file_Copy(gchar *sFrom, gchar *sTo);
gchar *file_Canonicalize(const gchar *filename, const gchar *relative_to);
#endif
