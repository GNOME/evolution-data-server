/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* server-logging.c
 *
 * Copyright (C) 2003  Novell, Inc.
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
 * Author: Chris Toshok <toshok@ximian.com>
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "server-logging.h"


#define PARENT_TYPE bonobo_event_source_get_type ()
static BonoboEventSourceClass *parent_class = NULL;


static void
server_logging_class_init (ServerLoggingClass *class)
{
	parent_class = g_type_class_ref (PARENT_TYPE);
}

static void
server_logging_init (ServerLogging *logging)
{
	/* (Nothing to initialize here.)  */
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
	CORBA_any *value;
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

	log_event.domain = CORBA_string_dup (domain);
	log_event.message = CORBA_string_dup (msg);

	CORBA_exception_init (&ev);

	value = bonobo_arg_new (TC_GNOME_Evolution_DataServer_Logging_LogEvent);

	BONOBO_ARG_SET_GENERAL (value, log_event,
				TC_GNOME_Evolution_DataServer_Logging_LogEvent,
				GNOME_Evolution_DataServer_Logging_LogEvent,
				NULL);

	if (bonobo_event_source_has_listener (es, "log_event")) {

		bonobo_event_source_notify_listeners (es, "log_event", value, &ev); 
	}

	CORBA_free (log_event.domain);
	CORBA_free (log_event.message);
	bonobo_arg_release (value);

	g_log_default_handler (domain, flags, msg, NULL);
}

void
server_logging_register_domain (ServerLogging *logging,
				const char *domain)
{
	g_log_set_handler(domain,
			  G_LOG_LEVEL_MASK | G_LOG_FLAG_FATAL | G_LOG_FLAG_RECURSION,
			  server_log_handler, logging);
}
