/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* server-interface-check.h
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 *
 * Author: Sivaiah Nallagatla <snallagatla@novell.com>
 */

/*Note : Copied from src/offline_listener.c . This should be replaced */
/* with network manager code */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "e-offline-listener.h"
#include <gconf/gconf-client.h>

G_DEFINE_TYPE (EOfflineListener, e_offline_listener, G_TYPE_OBJECT)

enum {
	CHANGED,
	NUM_SIGNALS
};

static guint signals[NUM_SIGNALS] = { 0 };

static GObjectClass *parent_class = NULL;

struct _EOfflineListenerPrivate
{
	GConfClient *default_client;
	gboolean is_offline_now;
};

static void
set_online_status (EOfflineListener *eol, gboolean is_offline)
{
	EOfflineListenerPrivate *priv;

	priv = eol->priv;

	g_signal_emit (eol, signals[CHANGED], 0);
}

static void
online_status_changed (GConfClient *client, gint cnxn_id, GConfEntry *entry, gpointer data)
{
	GConfValue *value;
	gboolean offline;
        EOfflineListener *eol;
	EOfflineListenerPrivate *priv;

	eol = E_OFFLINE_LISTENER (data);
	g_return_if_fail (eol != NULL);

	priv = eol->priv;
	offline = FALSE;
	value = gconf_entry_get_value (entry);
	if (value)
		offline = gconf_value_get_bool (value);

	if (priv->is_offline_now != offline) {
		priv->is_offline_now = offline;

		set_online_status (eol, offline);
	}
}

static void
setup_offline_listener (EOfflineListener *eol)
{
	EOfflineListenerPrivate *priv = eol->priv;

	priv->default_client = gconf_client_get_default ();
	gconf_client_add_dir (priv->default_client, "/apps/evolution/shell", GCONF_CLIENT_PRELOAD_RECURSIVE,NULL);
	gconf_client_notify_add (priv->default_client, "/apps/evolution/shell/start_offline",
				 (GConfClientNotifyFunc)online_status_changed,
				 eol, NULL, NULL);

	priv->is_offline_now = gconf_client_get_bool (priv->default_client, "/apps/evolution/shell/start_offline", NULL);
	set_online_status (eol, priv->is_offline_now);
}

EOfflineListener*
e_offline_listener_new (void)
{
	EOfflineListener *eol = g_object_new (E_TYPE_OFFLINE_LISTENER, NULL);

	setup_offline_listener (eol);

	return eol;
}

static void
e_offline_listener_dispose (GObject *object)
{
	EOfflineListener *eol = E_OFFLINE_LISTENER (object);
	if (eol->priv->default_client) {
		g_object_unref (eol->priv->default_client);
		eol->priv->default_client = NULL;
	}

	(* G_OBJECT_CLASS (parent_class)->dispose) (object);
}

static void
e_offline_listener_finalize (GObject *object)
{
	EOfflineListener *eol;
	EOfflineListenerPrivate *priv;

	eol = E_OFFLINE_LISTENER (object);
	priv = eol->priv;

	g_free (priv);
	eol->priv = NULL;

	parent_class->finalize (object);
}

static void
e_offline_listener_init (EOfflineListener *eol)
{
	EOfflineListenerPrivate *priv;

	priv = g_new0 (EOfflineListenerPrivate, 1);
	eol->priv = priv;
}

static void
e_offline_listener_class_init (EOfflineListenerClass *klass)
{
	GObjectClass *object_class;

	parent_class = g_type_class_peek_parent (klass);

	object_class = G_OBJECT_CLASS (klass);
	object_class->dispose = e_offline_listener_dispose;
	object_class->finalize = e_offline_listener_finalize;

	signals[CHANGED] =
		g_signal_new ("changed",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (EOfflineListenerClass, changed),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);
}

EOfflineListenerState
e_offline_listener_get_state (EOfflineListener *eol)
{
	g_return_val_if_fail (E_IS_OFFLINE_LISTENER (eol), EOL_STATE_OFFLINE);

	return eol->priv->is_offline_now ? EOL_STATE_OFFLINE : EOL_STATE_ONLINE;
}
