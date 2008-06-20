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
 * Author: Ettore Perazzoli <ettore@ximian.com>
 */

#ifndef _SERVER_INTERFACE_CHECK_H_
#define _SERVER_INTERFACE_CHECK_H_

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <bonobo/bonobo-object.h>
#include "Evolution-DataServer.h"

G_BEGIN_DECLS

#define SERVER_TYPE_INTERFACE_CHECK		(server_interface_check_get_type ())
#define SERVER_INTERFACE_CHECK(obj)		(GTK_CHECK_CAST ((obj), SERVER_TYPE_INTERFACE_CHECK, ServerInterfaceCheck))
#define SERVER_INTERFACE_CHECK_CLASS(klass)	(GTK_CHECK_CLASS_CAST ((klass), SERVER_TYPE_INTERFACE_CHECK, ServerInterfaceCheckClass))
#define SERVER_IS_INTERFACE_CHECK(obj)		(GTK_CHECK_TYPE ((obj), SERVER_TYPE_INTERFACE_CHECK))
#define SERVER_IS_INTERFACE_CHECK_CLASS(klass)	(GTK_CHECK_CLASS_TYPE ((obj), SERVER_TYPE_INTERFACE_CHECK))


typedef struct _ServerInterfaceCheck        ServerInterfaceCheck;
typedef struct _ServerInterfaceCheckPrivate ServerInterfaceCheckPrivate;
typedef struct _ServerInterfaceCheckClass   ServerInterfaceCheckClass;

struct _ServerInterfaceCheck {
	BonoboObject parent;
};

struct _ServerInterfaceCheckClass {
	BonoboObjectClass parent_class;

	POA_GNOME_Evolution_DataServer_InterfaceCheck__epv epv;
};


GType server_interface_check_get_type  (void);
ServerInterfaceCheck *server_interface_check_new (void);

G_END_DECLS

#endif /* _SERVER_INTERFACE_CHECK_H_ */
