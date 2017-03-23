/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2017 Red Hat, Inc. (www.redhat.com)
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
 */

/**
 * SECTION: e-webdav-session
 * @include: libedataserver/libedataserver.h
 * @short_description: A WebDAV, CalDAV and CardDAV session
 *
 * The #EWebDAVSession is a class to work with WebDAV (RFC 4918),
 * CalDAV (RFC 4791) or CardDAV (RFC 6352) servers, providing API
 * for common requests/responses, on top of an #ESoupSession.
 **/

#include "evolution-data-server-config.h"

#include "e-source-webdav.h"

#include "e-webdav-session.h"

struct _EWebDAVSessionPrivate {
	gboolean dummy;
};

G_DEFINE_TYPE (EWebDAVSession, e_webdav_session, E_TYPE_SOUP_SESSION)

static void
e_webdav_session_class_init (EWebDAVSessionClass *klass)
{
	g_type_class_add_private (klass, sizeof (EWebDAVSessionPrivate));
}

static void
e_webdav_session_init (EWebDAVSession *webdav)
{
	webdav->priv = G_TYPE_INSTANCE_GET_PRIVATE (webdav, E_TYPE_WEBDAV_SESSION, EWebDAVSessionPrivate);
}

/**
 * e_webdav_session_new:
 * @source: an #ESource
 *
 * Creates a new #EWebDAVSession associated with given @source. It's
 * a user's error to try to create the #EWebDAVSession for a source
 * which doesn't have #ESourceWebdav extension properly defined.
 *
 * Returns: (transfer full): a new #EWebDAVSession; free it with g_object_unref(),
 *    when no longer needed.
 *
 * Since: 3.26
 **/
EWebDAVSession *
e_webdav_session_new (ESource *source)
{
	g_return_val_if_fail (E_IS_SOURCE (source), NULL);
	g_return_val_if_fail (e_source_has_extension (source, E_SOURCE_EXTENSION_WEBDAV_BACKEND), NULL);

	return g_object_new (E_TYPE_WEBDAV_SESSION,
		"source", source,
		NULL);
}
