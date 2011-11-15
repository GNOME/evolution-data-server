/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-win32.c : Win32-specific bits */

/*
 * Authors: Tor Lillqvist <tml@novell.com>
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
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301
 * USA
 */

#include <errno.h>
#include <io.h>
#include <stdlib.h>
#include <sys/stat.h>

#include <windows.h>

#include <glib/gstdio.h>

#include "camel.h"

G_LOCK_DEFINE_STATIC (mutex);

/* localedir uses system codepage as it is passed to the non-UTF8ified
 * gettext library
 */
static const gchar *localedir = NULL;

/* The others are in UTF-8 */
static const gchar *libexecdir;
static const gchar *providerdir;

/* XXX Where do these get defined?  e-data-server-util.h just has
 * declarations for e_util_get_prefix() and e_util_get_cp_prefix(). */
static const gchar *	get_prefix		(void) G_GNUC_CONST;
static const gchar *	get_cp_prefix		(void) G_GNUC_CONST;

static gchar *
replace_prefix (const gchar *configure_time_prefix,
                const gchar *runtime_prefix,
                const gchar *configure_time_path)
{
	gchar *c_t_prefix_slash;
	gchar *retval;

	c_t_prefix_slash = g_strconcat (configure_time_prefix, "/", NULL);

	if (runtime_prefix != NULL &&
	    g_str_has_prefix (configure_time_path, c_t_prefix_slash)) {
		retval = g_strconcat (
			runtime_prefix,
			configure_time_path + strlen (configure_time_prefix),
			NULL);
	} else
		retval = g_strdup (configure_time_path);

	g_free (c_t_prefix_slash);

	return retval;
}

static void
setup (void)
{
	G_LOCK (mutex);

	if (localedir != NULL) {
		G_UNLOCK (mutex);
		return;
	}

	localedir = replace_prefix (
		E_DATA_SERVER_PREFIX, get_cp_prefix (), LOCALEDIR);
	libexecdir = replace_prefix (
		E_DATA_SERVER_PREFIX, get_prefix (), CAMEL_LIBEXECDIR);
	providerdir = replace_prefix (
		E_DATA_SERVER_PREFIX, get_prefix (), CAMEL_PROVIDERDIR);

	G_UNLOCK (mutex);
}

#include "camel-win32.h"	/* For prototypes */

#define GETTER(varbl)				\
const gchar *					\
_camel_get_##varbl (void)			\
{						\
	setup ();				\
	return varbl;				\
}

GETTER(localedir)
GETTER(libexecdir)
GETTER(providerdir)
