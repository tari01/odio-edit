/*
    Copyright (C) 2019-2020, Robert Tari <robert@tari.in>
    Copyright (C) 2002 2003 2004 2005 2006 2008 2011 2012, Magnus Hjorth

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

#include <gtk/gtk.h>
#include <glib/gi18n.h>
#include "message.h"
#include "mainwindow.h"
#include "main.h"

static gboolean m_bResponded = FALSE;
static gint m_nResponse;

static void message_OnResponse(GtkDialog *pDialog, gint nResponse, gpointer pUserData)
{
    m_bResponded = TRUE;
    m_nResponse = nResponse;
}

static void message_SetLabelExpand(GtkWidget *pWidget, gpointer pUserData)
{
    gtk_widget_set_vexpand(pWidget, TRUE);
}

static gint message_ShowDialog(GtkMessageType nMessageType, GtkButtonsType nButtonsType, gchar *sMessage)
{
    GtkWidget *pWidget = gtk_message_dialog_new(GTK_WINDOW(g_pFocusedWindow), GTK_DIALOG_MODAL, nMessageType, nButtonsType, "%s", sMessage);
    GtkWidget *pMessageArea = gtk_message_dialog_get_message_area(GTK_MESSAGE_DIALOG(pWidget));
    gtk_container_forall(GTK_CONTAINER(pMessageArea), message_SetLabelExpand, NULL);
    GtkWidget *pContentArea = gtk_widget_get_parent(pMessageArea);
    gtk_widget_set_margin_start(pContentArea, 20);
    gtk_widget_set_margin_end(pContentArea, 20);
    gtk_widget_set_margin_top(pContentArea, 20);
    gtk_widget_set_margin_bottom(pContentArea, 0);

    if (nButtonsType == GTK_BUTTONS_NONE)
    {
        gtk_dialog_add_buttons(GTK_DIALOG(pWidget), _("Yes"), GTK_RESPONSE_YES, _("No"), GTK_RESPONSE_NO, _("Cancel"), GTK_RESPONSE_CANCEL, NULL);
    }

    g_signal_connect(pWidget, "response", G_CALLBACK(message_OnResponse), NULL);
    m_bResponded = FALSE;
    gtk_window_set_position(GTK_WINDOW(pWidget), GTK_WIN_POS_CENTER);
    gtk_widget_show(pWidget);

    while (!m_bResponded)
    {
        mainLoop();
    }

    if (m_nResponse != GTK_RESPONSE_DELETE_EVENT)
    {
        gtk_widget_destroy(pWidget);
    }

    if (m_nResponse == GTK_RESPONSE_OK)
    {
        return GTK_RESPONSE_OK;
    }

    if (m_nResponse == GTK_RESPONSE_CANCEL)
    {
        return GTK_RESPONSE_CANCEL;
    }

    if (m_nResponse == GTK_RESPONSE_YES)
    {
        return GTK_RESPONSE_YES;
    }

    if (m_nResponse == GTK_RESPONSE_NO)
    {
        return GTK_RESPONSE_NO;
    }

    if (nButtonsType == GTK_BUTTONS_OK)
    {
        return GTK_RESPONSE_OK;
    }
    
    return GTK_RESPONSE_CANCEL;
}

gint message_Info(gchar *sMessage, gint nType)
{
    if (nType == GTK_BUTTONS_NONE)
    {
        return message_ShowDialog(GTK_MESSAGE_QUESTION, GTK_BUTTONS_NONE, sMessage);
    }
    else if (nType == GTK_BUTTONS_OK_CANCEL)
    {
        return message_ShowDialog(GTK_MESSAGE_WARNING, GTK_BUTTONS_OK_CANCEL, sMessage);
    }
    
    return message_ShowDialog(GTK_MESSAGE_INFO, GTK_BUTTONS_OK, sMessage);
}

void message_Error(gchar *sMessage)
{
    message_ShowDialog(GTK_MESSAGE_ERROR, GTK_BUTTONS_OK, sMessage);
}

void message_Warning(gchar *sMessage)
{
    message_ShowDialog(GTK_MESSAGE_WARNING, GTK_BUTTONS_OK, sMessage);
}
