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

#include <glib/gi18n.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include "message.h"
#include "file.h"
#include "gstreamer.h"

File *file_Open(gchar *sFilePath, gint nMode, gboolean bReportError)
{
    gint nFlag;
    
    if (nMode == FILE_READ)
    {
        nFlag = O_RDONLY;
    }
    else
    {
        nFlag = O_WRONLY | O_CREAT | O_TRUNC;
    }
    
    gint nFile = open(sFilePath, nFlag, 0666);

    if (nFile == -1)
    {
        gchar *sMessage = NULL;
        
        if (bReportError)
        {
            sMessage = g_strdup_printf(_("Could not open %s: %s"), sFilePath, strerror(errno));
        }
        else if (errno != ENOSPC)
        {
            sMessage = g_strdup_printf(_("Unexpected error: %s %s"), sFilePath, strerror(errno));
        }
        
        message_Error(sMessage);
        
        if (sMessage != NULL)
        {
            g_free(sMessage);
        }
        
        return NULL;
    }
            
    File *pFile;
    pFile = g_malloc(sizeof(*pFile));
    pFile->nFile = nFile;
    pFile->sFilePath = g_strdup(sFilePath);
    
    return pFile;
}

gboolean file_Close(File *pFile, gboolean bUnlink)
{
    gboolean bError = FALSE;

    if (close(pFile->nFile) != 0)
    {
        gchar *sMessage = g_strdup_printf(_("Error closing %s: %s"), pFile->sFilePath, strerror(errno));
        message_Error(sMessage);
        g_free(sMessage);
        bError = TRUE;
    }
    
    if (bUnlink && file_Unlink(pFile->sFilePath))
    {
        bError = TRUE;
    }
    
    g_free(pFile->sFilePath);
    g_free(pFile);
    
    return bError;
}

gboolean file_Seek(File *pFile, gint64 nByte, gint nWhence)
{
    if (lseek(pFile->nFile, nByte, nWhence) == (gint64) -1)
    {
        gchar *sMessage = g_strdup_printf(_("Could not seek in %s: %s"), pFile->sFilePath, strerror(errno));
        message_Error(sMessage);
        g_free(sMessage);
        
        return TRUE;
    }
    
    return FALSE;
}

gboolean file_Read(gchar *lBytes, gint64 nBytes, File *pFile)
{
    gint64 nPos = 0;
    gchar *sMessage;

    while (nBytes > 0)
    {
        gint64 nRead = read(pFile->nFile, lBytes + nPos, nBytes);

        if (nRead == 0)
        {
            break;
        }

        if (nRead < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            
            sMessage = g_strdup_printf(_("Could not read from %s: %s"), pFile->sFilePath, strerror(errno));
            message_Error(sMessage);
            g_free(sMessage);
            
            nPos = -1;
            
            break;
        }
        
        nPos += nRead;
        nBytes -= nRead;
    }
    
    if (nPos < nBytes)
    {
        if (nPos >= 0)
        {
            // Translators: %s will be the name of the file
            sMessage = g_strdup_printf(_("Unexpected end of file reading %s"), pFile->sFilePath);
            message_Error(sMessage);
            g_free(sMessage);
        }
        
        return TRUE;
    }
    
    return FALSE;
}

gboolean file_Write(gchar *lBytes, gint64 nBytes, File *pFile)
{
    gint64 nPos = 0;
    
    while (nBytes > 0)
    {
        gint64 nWritten = write(pFile->nFile, lBytes + nPos, nBytes);
        gchar *sMessage;

        if (nWritten == 0)
        {
            sMessage = g_strdup_printf(_("Could not write data to %s"), pFile->sFilePath);
            message_Error(sMessage);
            g_free(sMessage);
            
            return TRUE;
        }
        
        if (nWritten < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }

            sMessage = g_strdup_printf(_("Could not read from %s: %s"), pFile->sFilePath, strerror(errno));
            message_Error(sMessage);
            g_free(sMessage);
            
            return TRUE;
        }
        
        nPos += nWritten;
        nBytes -= nWritten;
    }
    
    return FALSE;
}

gint64 file_Tell(File *pFile)
{
    gint64 nResult = lseek(pFile->nFile, 0, SEEK_CUR);

    if (nResult == -1)
    {
        gchar *sMessage = g_strdup_printf(_("Could not get file position in %s: %s"), pFile->sFilePath, strerror(errno));
        message_Error(sMessage);
        g_free(sMessage);
    }
    
    return nResult;
}

gboolean file_Copy(gchar *sFrom, gchar *sTo)
{
    File *pFile = file_Open(sFrom, FILE_READ, TRUE);

    if (pFile == NULL)
    {
        return TRUE;
    }

    if (file_Seek(pFile, 0, SEEK_END))
    {
        file_Close(pFile, FALSE);
        
        return TRUE;
    }
    
    gint64 nSize = file_Tell(pFile);
    file_Close(pFile, FALSE);

    if (nSize == -1)
    {
        return TRUE;
    }
    
    File *pFileFrom = file_Open(sFrom, FILE_READ, TRUE);

    if (!pFileFrom)
    {
        return TRUE;
    }
    
    File *pFileTo = file_Open(sTo, FILE_WRITE, TRUE);

    if (!pFileTo)
    {
        file_Close(pFileFrom, FALSE);
        
        return TRUE;
    }
    
    gboolean bError = FALSE;
    gchar lBuf[BUFFER_SIZE];
    gint64 nCount = nSize / sizeof(lBuf);
    gint64 nRest = nSize % sizeof(lBuf);
    
    for (; nCount > 0; nCount--)
    {
        if (file_Read(lBuf, sizeof(lBuf), pFileFrom) || file_Write(lBuf, sizeof(lBuf), pFileTo))
        {
            bError = TRUE;
            
            break;
        }
    }

    if (!bError && nRest > 0)
    {
        bError = (file_Read(lBuf, nRest, pFileFrom) || file_Write(lBuf, nRest, pFileTo));
    }
    
    file_Close(pFileFrom, FALSE);

    if (bError)
    {
        file_Close(pFileTo, TRUE);
    }
    else
    {
        file_Close(pFileTo, FALSE);
    }
    
    return bError;
}

gboolean file_Unlink(gchar *sFilePath)
{
    gchar *sMessage;

    if (unlink(sFilePath) == -1 && errno != ENOENT)
    {
        sMessage = g_strdup_printf(_("Could not delete '%s': %s"), sFilePath, strerror(errno));
        message_Error(sMessage);
        g_free(sMessage);
        
        return TRUE;
    }
    
    return FALSE;
}

gint file_Rename(gchar *sOldName, gchar *sNewName)
{
    gchar *sMessage;

    g_assert(strcmp(sOldName, sNewName) != 0);

    if (rename(sOldName, sNewName) == 0)
    {
        return 0;
    }

    if (errno == EXDEV)
    {
        return file_Copy(sOldName, sNewName) ? 1 : 0;
    }
    else
    {
        sMessage = g_strdup_printf(_("Error creating link to '%s': %s"), sOldName, strerror(errno));
        message_Error(sMessage);
        g_free(sMessage);
        
        return 1;
    }
}

//Present in Glib 2.58 as g_canonicalize_filename
gchar *file_Canonicalize(const gchar *filename, const gchar *relative_to)
{
    gchar *canon, *start, *p, *q;
    guint i;

    g_return_val_if_fail (relative_to == NULL || g_path_is_absolute (relative_to), NULL);

    if (!g_path_is_absolute (filename))
    {
        gchar *cwd_allocated = NULL;
        const gchar  *cwd;

        if (relative_to != NULL)
        {
            cwd = relative_to;
        }
        else
        {
            cwd = cwd_allocated = g_get_current_dir ();
        }
        
        canon = g_build_filename (cwd, filename, NULL);
        g_free (cwd_allocated);
    }
    else
    {
        canon = g_strdup (filename);
    }

    start = (char *)g_path_skip_root (canon);

    if (start == NULL)
    {
        g_free (canon);
        
        return g_build_filename (G_DIR_SEPARATOR_S, filename, NULL);
    }

    i = 0;
    
    for (p = start - 1; (p >= canon) && G_IS_DIR_SEPARATOR (*p); p--)
    {
        i++;
    }
    
    if (i > 2)
    {
        i -= 1;
        start -= i;
        memmove (start, start+i, strlen (start+i) + 1);
    }

    p++;
    
    while (p < start && G_IS_DIR_SEPARATOR (*p))
    {
        *p++ = G_DIR_SEPARATOR;
    }
    
    p = start;
    
    while (*p != 0)
    {
        if (p[0] == '.' && (p[1] == 0 || G_IS_DIR_SEPARATOR (p[1])))
        {
            memmove (p, p+1, strlen (p+1)+1);
        }
        else if (p[0] == '.' && p[1] == '.' && (p[2] == 0 || G_IS_DIR_SEPARATOR (p[2])))
        {
            q = p + 2;
            p = p - 2;
            
            if (p < start)
            {
                p = start;
            }
            
            while (p > start && !G_IS_DIR_SEPARATOR (*p))
            {
                p--;
            }
            
            if (G_IS_DIR_SEPARATOR (*p))
            {
                *p++ = G_DIR_SEPARATOR;
            }
            
            memmove (p, q, strlen (q)+1);
        }
        else
        {
            while (*p != 0 && !G_IS_DIR_SEPARATOR (*p))
            {
                p++;
            }
            
            if (*p != 0)
            {
                *p++ = G_DIR_SEPARATOR;
            }
        }

        q = p;
        
        while (*q && G_IS_DIR_SEPARATOR (*q))
        {
            q++;
        }
        
        if (p != q)
        {
            memmove (p, q, strlen (q) + 1);
        }
    }

    if (p > start && G_IS_DIR_SEPARATOR (*(p-1)))
    {
        *(p-1) = 0;
    }

    return canon;
}
