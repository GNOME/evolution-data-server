/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-win32.c : Win32-specific bits */

/*
 * Authors: Tor Lillqvist <tml@novell.com>
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * This library is free software you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include <errno.h>
#include <io.h>
#include <stdlib.h>
#include <sys/stat.h>

#include <windows.h>

#include <glib/gstdio.h>

#include "camel.h"

static HMODULE hmodule;
G_LOCK_DEFINE_STATIC (mutex);

/* localedir uses system codepage as it is passed to the non-UTF8ified
 * gettext library
 */
static const gchar *localedir = NULL;

/* The others are in UTF-8 */
static const gchar *prefix = NULL;
static const gchar *cp_prefix;
static const gchar *libexecdir;
static const gchar *providerdir;

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
	gchar *full_pfx;
	gchar *cp_pfx;

	G_LOCK (mutex);

	if (prefix != NULL) {
		G_UNLOCK (mutex);
		return;
	}
	/* This requires that the libedataserver DLL is installed in $bindir */
	full_pfx = g_win32_get_package_installation_directory_of_module (hmodule);
	cp_pfx = g_win32_locale_filename_from_utf8 (full_pfx);

	prefix = g_strdup (full_pfx);
	cp_prefix = g_strdup (cp_pfx);

	g_free (full_pfx);
	g_free (cp_pfx);

	localedir = replace_prefix (
		E_DATA_SERVER_PREFIX, cp_prefix, LOCALEDIR);
	libexecdir = replace_prefix (
		E_DATA_SERVER_PREFIX, prefix, CAMEL_LIBEXECDIR);
	providerdir = replace_prefix (
		E_DATA_SERVER_PREFIX, prefix, CAMEL_PROVIDERDIR);

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

/* Silence gcc with a prototype. Yes, this is silly. */
BOOL WINAPI DllMain (HINSTANCE hinstDLL,
		     DWORD     fdwReason,
		     LPVOID    lpvReserved);

/* Minimal DllMain that just tucks away the DLL's HMODULE */
BOOL WINAPI
DllMain (HINSTANCE hinstDLL,
         DWORD fdwReason,
         LPVOID lpvReserved)
{
	switch (fdwReason) {
	case DLL_PROCESS_ATTACH:
		hmodule = hinstDLL;
		break;
	}
	return TRUE;
}
