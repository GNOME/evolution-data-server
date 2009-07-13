/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-url.h : utility functions to parse URLs */

/*
 * Authors:
 *  Bertrand Guiheneuf <bertrand@helixcode.com>
 *  Dan Winship <danw@ximian.com>
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

#ifndef CAMEL_URL_H
#define CAMEL_URL_H 1

#include <glib.h>
#include <camel/camel-types.h>

G_BEGIN_DECLS

/* if this changes, remember to change camel_url_copy */
typedef struct _CamelURL {
	gchar  *protocol;
	gchar  *user;
	gchar  *authmech;
	gchar  *passwd;
	gchar  *host;
	gint    port;
	gchar  *path;
	GData *params;
	gchar  *query;
	gchar  *fragment;
} CamelURL;

#define CAMEL_URL_HIDE_PASSWORD	(1 << 0)
#define CAMEL_URL_HIDE_PARAMS	(1 << 1)
#define CAMEL_URL_HIDE_AUTH	(1 << 2)

#define CAMEL_URL_HIDE_ALL (CAMEL_URL_HIDE_PASSWORD | CAMEL_URL_HIDE_PARAMS | CAMEL_URL_HIDE_AUTH)

CamelURL *camel_url_new_with_base (CamelURL *base, const gchar *url_string);
CamelURL *camel_url_new (const gchar *url_string, CamelException *ex);
gchar *camel_url_to_string (CamelURL *url, guint32 flags);
void camel_url_free (CamelURL *url);

gchar *camel_url_encode (const gchar *part, const gchar *escape_extra);
void camel_url_decode (gchar *part);
gchar *camel_url_decode_path (const gchar *path);

/* for editing url's */
void camel_url_set_protocol (CamelURL *url, const gchar *protocol);
void camel_url_set_user (CamelURL *url, const gchar *user);
void camel_url_set_authmech (CamelURL *url, const gchar *authmech);
void camel_url_set_passwd (CamelURL *url, const gchar *passwd);
void camel_url_set_host (CamelURL *url, const gchar *host);
void camel_url_set_port (CamelURL *url, gint port);
void camel_url_set_path (CamelURL *url, const gchar *path);
void camel_url_set_param (CamelURL *url, const gchar *name, const gchar *value);
void camel_url_set_query (CamelURL *url, const gchar *query);
void camel_url_set_fragment (CamelURL *url, const gchar *fragment);

const gchar *camel_url_get_param (CamelURL *url, const gchar *name);

/* for putting url's into hash tables */
guint camel_url_hash (gconstpointer v);
gint camel_url_equal(gconstpointer v, gconstpointer v2);
CamelURL *camel_url_copy(const CamelURL *in);

G_END_DECLS

#endif /* URL_UTIL_H */
