/*
 * Copyright (C) 2015 Red Hat, Inc. (www.redhat.com)
 *
 * This library is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library. If not, see <http://www.gnu.org/licenses/>.
 *
 */

#if !defined (__LIBEDATASERVER_H_INSIDE__) && !defined (LIBEDATASERVER_COMPILATION)
#error "Only <libedataserver/libedataserver.h> should be included directly."
#endif

#ifndef E_WEBDAV_DISCOVER_H
#define E_WEBDAV_DISCOVER_H

#include <glib.h>

#include <libedataserver/e-source.h>

G_BEGIN_DECLS

typedef enum {
	E_WEBDAV_DISCOVER_SUPPORTS_NONE		= 0,
	E_WEBDAV_DISCOVER_SUPPORTS_CONTACTS	= 1 << 0,
	E_WEBDAV_DISCOVER_SUPPORTS_EVENTS	= 1 << 1,
	E_WEBDAV_DISCOVER_SUPPORTS_MEMOS	= 1 << 2,
	E_WEBDAV_DISCOVER_SUPPORTS_TASKS	= 1 << 3
} EWebDAVDiscoverSupports;

typedef struct _EWebDAVDiscoveredSource {
	gchar *href;
	guint32 supports;
	gchar *display_name;
	gchar *description;
	gchar *color;
} EWebDAVDiscoveredSource;

void		e_webdav_discover_free_discovered_sources
							(GSList *discovered_sources);

void		e_webdav_discover_sources		(ESource *source,
							 const gchar *url_use_path,
							 guint32 only_supports,
							 const ENamedParameters *credentials,
							 GCancellable *cancellable,
							 GAsyncReadyCallback callback,
							 gpointer user_data);

gboolean	e_webdav_discover_sources_finish	(ESource *source,
							 GAsyncResult *result,
							 gchar **out_certificate_pem,
							 GTlsCertificateFlags *out_certificate_errors,
							 GSList **out_discovered_sources,
							 GSList **out_calendar_user_addresses,
							 GError **error);

gboolean	e_webdav_discover_sources_sync		(ESource *source,
							 const gchar *url_use_path,
							 guint32 only_supports,
							 const ENamedParameters *credentials,
							 gchar **out_certificate_pem,
							 GTlsCertificateFlags *out_certificate_errors,
							 GSList **out_discovered_sources,
							 GSList **out_calendar_user_addresses,
							 GCancellable *cancellable,
							 GError **error);

G_END_DECLS

#endif /* E_WEBDAV_DISCOVER_H */
