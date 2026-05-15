/*
 * SPDX-FileCopyrightText: (C) 1999-2008 Novell, Inc. (www.novell.com)
 * SPDX-License-Identifier: LGPL-2.0-or-later
 * SPDX-FileContributor: Bertrand Guiheneuf <bertrand@helixcode.com>
 * SPDX-FileContributor: Dan Winship <danw@ximian.com>
 */

#if !defined (__CAMEL_H_INSIDE__) && !defined (CAMEL_COMPILATION)
#error "Only <camel/camel.h> can be included directly."
#endif

#ifndef CAMEL_URL_H
#define CAMEL_URL_H

#include <glib.h>
#include <glib-object.h>

#define CAMEL_TYPE_URL (camel_url_get_type ())

G_BEGIN_DECLS

typedef struct _CamelURL CamelURL;

/* if this changes, remember to change camel_url_copy */
struct _CamelURL {
	gchar  *protocol;
	gchar  *user;
	gchar  *authmech;
	gchar  *host;
	gint    port;
	gchar  *path;
	GData *params;
	gchar  *query;
	gchar  *fragment;
};

typedef enum {
	CAMEL_URL_HIDE_PARAMS = 1 << 0,
	CAMEL_URL_HIDE_AUTH = 1 << 1
} CamelURLFlags;

#define CAMEL_URL_HIDE_ALL \
	(CAMEL_URL_HIDE_PARAMS | CAMEL_URL_HIDE_AUTH)

GType		camel_url_get_type		(void) G_GNUC_CONST;
CamelURL *	camel_url_new_with_base		(CamelURL *base,
						 const gchar *url_string);
CamelURL *	camel_url_new			(const gchar *url_string,
						 GError **error);
gchar *		camel_url_to_string		(CamelURL *url,
						 CamelURLFlags flags);
guint		camel_url_hash			(const CamelURL *u);
gboolean		camel_url_equal			(const CamelURL *u,
						 const CamelURL *u2);
CamelURL *	camel_url_copy			(CamelURL *in);
void		camel_url_free			(CamelURL *url);

gchar *		camel_url_encode		(const gchar *part,
						 const gchar *escape_extra);
void		camel_url_decode		(gchar *part);
gchar *		camel_url_decode_path		(const gchar *path);

/* for editing url's */
void		camel_url_set_protocol		(CamelURL *url,
						 const gchar *protocol);
void		camel_url_set_user		(CamelURL *url,
						 const gchar *user);
void		camel_url_set_authmech		(CamelURL *url,
						 const gchar *authmech);
void		camel_url_set_host		(CamelURL *url,
						 const gchar *host);
void		camel_url_set_port		(CamelURL *url,
						 gint port);
void		camel_url_set_path		(CamelURL *url,
						 const gchar *path);
void		camel_url_set_param		(CamelURL *url,
						 const gchar *name,
						 const gchar *value);
void		camel_url_set_query		(CamelURL *url,
						 const gchar *query);
void		camel_url_set_fragment		(CamelURL *url,
						 const gchar *fragment);

const gchar *	camel_url_get_param		(CamelURL *url,
						 const gchar *name);

G_END_DECLS

#endif /* URL_UTIL_H */
