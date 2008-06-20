/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* server-logging.c
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
 * Author: Chris Toshok <toshok@ximian.com>
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "server-logging.h"


#define PARENT_TYPE bonobo_event_source_get_type ()
static BonoboEventSourceClass *parent_class = NULL;

typedef struct {
	char *domain;
	guint id;
} ServerLoggingHandler;

struct _ServerLoggingPrivate {
	GSList *handlers;
};


static void
server_logging_dispose (GObject *object)
{
	ServerLogging *logging = SERVER_LOGGING (object);
	ServerLoggingPrivate *priv;
	GSList *l;

	priv = logging->priv;

	for (l = priv->handlers; l; l = l->next) {
		ServerLoggingHandler *handler = l->data;

		g_log_remove_handler (handler->domain, handler->id);
		g_free (handler->domain);
		g_free (handler);
	}
	g_slist_free (priv->handlers);
	priv->handlers = NULL;

	G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
server_logging_finalize (GObject *object)
{
	ServerLogging *logging = SERVER_LOGGING (object);

	g_free (logging->priv);

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
server_logging_class_init (ServerLoggingClass *klass)
{
	GObjectClass *object_class;

	parent_class = g_type_class_peek_parent (klass);

	object_class = G_OBJECT_CLASS (klass);
	object_class->dispose = server_logging_dispose;
	object_class->finalize = server_logging_finalize;
}

static void
server_logging_init (ServerLogging *logging)
{
	ServerLoggingPrivate *priv;

	priv = g_new0 (ServerLoggingPrivate, 1);

	logging->priv = priv;
}


ServerLogging *
server_logging_new (void)
{
	return g_object_new (SERVER_TYPE_LOGGING, NULL);
}


BONOBO_TYPE_FUNC_FULL (ServerLogging,
		       GNOME_Evolution_DataServer_Logging,
		       PARENT_TYPE,
		       server_logging)


static void
server_log_handler(const gchar *domain,
		   GLogLevelFlags flags,
		   const gchar *msg,
		   gpointer user_data)
{
	ServerLogging *logging = SERVER_LOGGING (user_data);
	BonoboEventSource *es = BONOBO_EVENT_SOURCE (logging);
	CORBA_Environment ev;
	CORBA_any value;
	GNOME_Evolution_DataServer_Logging_LogEvent log_event;

	printf ("in server_log_handler\n");

	if ((flags & G_LOG_LEVEL_ERROR) == G_LOG_LEVEL_ERROR)
		log_event.level = GNOME_Evolution_DataServer_Logging_Error;
	else if ((flags & G_LOG_LEVEL_CRITICAL) == G_LOG_LEVEL_CRITICAL)
		log_event.level = GNOME_Evolution_DataServer_Logging_Critical;
	else if ((flags & G_LOG_LEVEL_WARNING) == G_LOG_LEVEL_WARNING)
		log_event.level = GNOME_Evolution_DataServer_Logging_Warning;
	else if ((flags & G_LOG_LEVEL_MESSAGE) == G_LOG_LEVEL_MESSAGE)
		log_event.level = GNOME_Evolution_DataServer_Logging_Warning;
	else if ((flags & G_LOG_LEVEL_INFO) == G_LOG_LEVEL_INFO)
		log_event.level = GNOME_Evolution_DataServer_Logging_Info;
	else if ((flags & G_LOG_LEVEL_DEBUG) == G_LOG_LEVEL_DEBUG)
		log_event.level = GNOME_Evolution_DataServer_Logging_Debug;

	log_event.domain = (CORBA_char *) domain;
	log_event.message = (CORBA_char *) msg;

	value._type = TC_GNOME_Evolution_DataServer_Logging_LogEvent;
	value._value = &log_event;

	if (bonobo_event_source_has_listener (es, "log_event")) {
		CORBA_exception_init (&ev);
		bonobo_event_source_notify_listeners (es, "log_event", &value, &ev);
		CORBA_exception_free (&ev);
	}

	g_log_default_handler (domain, flags, msg, NULL);
}

void
server_logging_register_domain (ServerLogging *logging,
				const char *domain)
{
	ServerLoggingPrivate *priv;
	guint handler_id;

	priv = logging->priv;

	handler_id = g_log_set_handler(domain, G_LOG_LEVEL_MASK | G_LOG_FLAG_FATAL | G_LOG_FLAG_RECURSION,
				       server_log_handler, logging);

	if (handler_id) {
		ServerLoggingHandler *handler;

		handler = g_new0 (ServerLoggingHandler, 1);
		handler->domain = g_strdup (domain);
		handler->id = handler_id;

		priv->handlers = g_slist_prepend (priv->handlers, handler);
	}
}
