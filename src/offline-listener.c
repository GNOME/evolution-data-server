/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* server-interface-check.h
 *
 * Copyright (C) 2004  Novell, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 * Author: Sivaiah Nallagatla <snallagatla@novell.com>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "offline-listener.h"
#include <libedata-book/e-data-book-factory.h>
#include <libedata-cal/e-data-cal-factory.h>
#include <gconf/gconf-client.h>

enum {

	OFFLINE_MODE=1,
	ONLINE_MODE
};

static GObjectClass *parent_class = NULL;

struct _OfflineListenerPrivate 
{
	GConfClient *default_client;
	EDataCalFactory *cal_factory;
	EDataBookFactory *book_factory;
	gboolean is_offline_now;
};


static void 
set_online_status (OfflineListener *offline_listener, gboolean is_offline)
{
	OfflineListenerPrivate *priv;
	
	priv = offline_listener->priv;
	
	if (is_offline) {
		e_data_cal_factory_set_backend_mode (priv->cal_factory, OFFLINE_MODE);
		e_data_book_factory_set_backend_mode (priv->book_factory, OFFLINE_MODE);

	} else {
		e_data_cal_factory_set_backend_mode (priv->cal_factory, ONLINE_MODE);
		e_data_book_factory_set_backend_mode (priv->book_factory, ONLINE_MODE);
	}

}
static void 
online_status_changed (GConfClient *client, int cnxn_id, GConfEntry *entry, gpointer data)
{
	GConfValue *value;
	gboolean offline;
        OfflineListener *offline_listener;
	OfflineListenerPrivate *priv;

	offline_listener = OFFLINE_LISTENER(data);
	priv = offline_listener->priv;
	offline = FALSE;
	value = gconf_entry_get_value (entry);
	if (value)
		offline = gconf_value_get_bool (value);
	if (priv->is_offline_now != offline) {
		priv->is_offline_now = offline;
		set_online_status (offline_listener ,offline);
	}
	
}


static void 
setup_offline_listener (OfflineListener *offline_listener)
{
	OfflineListenerPrivate *priv = offline_listener->priv;
	GConfValue *value;
	
	priv->default_client = gconf_client_get_default ();
	gconf_client_add_dir (priv->default_client, "/apps/evolution/shell", GCONF_CLIENT_PRELOAD_RECURSIVE,NULL);
	gconf_client_notify_add (priv->default_client, "/apps/evolution/shell/start_offline", (GConfClientNotifyFunc)online_status_changed, offline_listener, NULL, NULL);
	priv->is_offline_now = gconf_client_get_bool (priv->default_client, "/apps/evolution/shell/start_offline", NULL);
	set_online_status (offline_listener, priv->is_offline_now); 
}

OfflineListener*
offline_listener_new (EDataBookFactory *book_factory, EDataCalFactory *cal_factory)
{
	OfflineListener *offline_listener = g_object_new (OFFLINE_TYPE_LISTENER, NULL);
	OfflineListenerPrivate *priv = offline_listener->priv;
	
	priv->book_factory = book_factory;
	priv->cal_factory = cal_factory;
	setup_offline_listener (offline_listener);
	return offline_listener;

}


static void
offline_listener_dispose (GObject *object)
{
	OfflineListener *offline_listener = OFFLINE_LISTENER (object);
	if (offline_listener->priv->default_client) {
		g_object_unref (offline_listener->priv->default_client);
		offline_listener->priv->default_client = NULL;
	}
	(* G_OBJECT_CLASS (parent_class)->dispose) (object);
}

static void
offline_listener_finalize (GObject *object)
{
	OfflineListener *offline_listener;
	OfflineListenerPrivate *priv;

	offline_listener = OFFLINE_LISTENER (object);
	priv = offline_listener->priv;
	
	g_free (priv);
	offline_listener->priv = NULL;
	
	parent_class->finalize (object);
}

static void
offline_listener_init (OfflineListener *listener)
{
	OfflineListenerPrivate *priv;
	
	priv =g_new0 (OfflineListenerPrivate, 1);
	listener->priv = priv;
	
}



static void
offline_listener_class_init (OfflineListener *klass)
{
	GObjectClass *object_class;

	parent_class = g_type_class_peek_parent (klass);

	object_class = G_OBJECT_CLASS (klass);
	object_class->dispose = offline_listener_dispose;
	object_class->finalize = offline_listener_finalize;
	
	

}


GType
offline_listener_get_type (void)
{
	static GType type = 0;

	if (!type) {
		static GTypeInfo info = {
                        sizeof (OfflineListenerClass),
                        (GBaseInitFunc) NULL,
                        (GBaseFinalizeFunc) NULL,
                        (GClassInitFunc) offline_listener_class_init,
                        NULL, NULL,
                        sizeof (OfflineListener),
                        0,
                        (GInstanceInitFunc) offline_listener_init,
                };
		type = g_type_register_static (G_TYPE_OBJECT, "OfflineListener", &info, 0);
	}

	return type;
}
