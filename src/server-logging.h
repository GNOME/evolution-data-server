/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* server-logging.h
 *
 * Copyright (C) 2002  Novell, Inc.
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

#ifndef _SERVER_LOGGING_H_
#define _SERVER_LOGGING_H_

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <bonobo/bonobo-event-source.h>
#include "Evolution-DataServer.h"

G_BEGIN_DECLS

#define SERVER_TYPE_LOGGING		(server_logging_get_type ())
#define SERVER_LOGGING(obj)		(G_TYPE_CHECK_INSTANCE_CAST ((obj), SERVER_TYPE_LOGGING, ServerLogging))
#define SERVER_LOGGING_CLASS(klass)	(G_TYPE_CHECK_CLASS_CAST ((klass), SERVER_TYPE_LOGGING, ServerLoggingClass))
#define SERVER_IS_LOGGING(obj)		(G_TYPE_CHECK_INSTANCE_TYPE ((obj), SERVER_TYPE_LOGGING))
#define SERVER_IS_LOGGING_CLASS(klass)	(G_TYPE_CHECK_CLASS_TYPE ((obj), SERVER_TYPE_LOGGING))


typedef struct _ServerLogging        ServerLogging;
typedef struct _ServerLoggingPrivate ServerLoggingPrivate;
typedef struct _ServerLoggingClass   ServerLoggingClass;

struct _ServerLogging {
	BonoboEventSource parent;

	ServerLoggingPrivate *priv;
};

struct _ServerLoggingClass {
	BonoboEventSourceClass parent_class;

	POA_GNOME_Evolution_DataServer_Logging__epv epv;
};


GType server_logging_get_type  (void);
ServerLogging *server_logging_new (void);

void server_logging_register_domain (ServerLogging *logging, const char *domain);

G_END_DECLS

#endif /* _SERVER_LOGGING_H_ */
