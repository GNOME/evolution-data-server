/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* server-interface-check.c
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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "server-interface-check.h"


#define PARENT_TYPE bonobo_object_get_type ()
static BonoboObjectClass *parent_class = NULL;


static CORBA_char *
impl__get_interfaceVersion (PortableServer_Servant servant,
			    CORBA_Environment *ev)
{
	return CORBA_string_dup (VERSION);
}


static void
server_interface_check_class_init (ServerInterfaceCheckClass *class)
{
	parent_class = g_type_class_ref (PARENT_TYPE);

	class->epv._get_interfaceVersion = impl__get_interfaceVersion;
}

static void
server_interface_check_init (ServerInterfaceCheck *interface_check)
{
	/* (Nothing to initialize here.)  */
}


ServerInterfaceCheck *
server_interface_check_new (void)
{
	return g_object_new (SERVER_TYPE_INTERFACE_CHECK, NULL);
}


BONOBO_TYPE_FUNC_FULL (ServerInterfaceCheck,
		       GNOME_Evolution_DataServer_InterfaceCheck,
		       PARENT_TYPE,
		       server_interface_check)
