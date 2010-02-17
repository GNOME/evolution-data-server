/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Authors :
 *  Ebby Wiselyn <ebbywiselyn@gmail.com>
 *  Jason Willis <zenbrother@gmail.com>
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 *
 */

#include <config.h>
#include <gdata-service-iface.h>

void
gdata_service_set_proxy (GDataService *self, SoupURI *proxy)
{
	GDATA_SERVICE_GET_IFACE(self)->set_proxy(self, proxy);
}

void
gdata_service_set_credentials (GDataService *self, const gchar *username, const gchar *password)
{
	GDATA_SERVICE_GET_IFACE(self)->set_credentials(self, username, password);
}

GDataFeed*
gdata_service_get_feed (GDataService *self, const gchar * feedURL, GError **error)
{
	return GDATA_SERVICE_GET_IFACE(self)->get_feed(self, feedURL, error);
}

GDataEntry*
gdata_service_insert_entry (GDataService *self, const gchar *feed_postURL, GDataEntry *entry, GError **error)
{
	return GDATA_SERVICE_GET_IFACE(self)->insert_entry(self, feed_postURL, entry, error);
}

GDataEntry*
gdata_service_get_entry (GDataService *self, const gchar *entry_getURL, GError **error)
{
	return	GDATA_SERVICE_GET_IFACE(self)->get_entry(self, entry_getURL, error);
}

GDataEntry*
gdata_service_update_entry (GDataService *self, GDataEntry *entry, GError **error)
{
	return GDATA_SERVICE_GET_IFACE(self)->update_entry(self, entry, error);
}

GDataEntry*
gdata_service_update_entry_with_link (GDataService *self, GDataEntry *entry, gchar *link, GError **error)
{
	return GDATA_SERVICE_GET_IFACE(self)->update_entry_with_link(self, entry, link, error);
}

gboolean
gdata_service_delete_entry (GDataService *self, GDataEntry *entry, GError **error)
{
	return GDATA_SERVICE_GET_IFACE(self)->delete_entry (self, entry, error);
}

static void
gdata_service_base_init (gpointer g_class)
{
	static gboolean initialized = FALSE;

	if (!initialized) {
		/* create interface signals here. */
		initialized = TRUE;
	}
}

GType
gdata_service_get_type (void)
{
        static volatile gsize define_type_id__volatile = 0;

	if (g_once_init_enter (&define_type_id__volatile)) {
		static const GTypeInfo info =
		{
			sizeof (GDataServiceIface),
			gdata_service_base_init,   /* base_init */
			NULL,   /* base_finalize */
			NULL,   /* class_init */
			NULL,   /* class_finalize */
			NULL,   /* class_data */
			0,
			0,      /* n_preallocs */
			NULL    /* instance_init */
		};
		GType type = g_type_register_static (G_TYPE_INTERFACE, "GDataService", &info, 0);

		g_type_interface_add_prerequisite (type, G_TYPE_OBJECT);

		g_once_init_leave (&define_type_id__volatile, type);
	}

	return define_type_id__volatile;
}
