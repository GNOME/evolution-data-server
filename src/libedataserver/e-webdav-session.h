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

#if !defined (__LIBEDATASERVER_H_INSIDE__) && !defined (LIBEDATASERVER_COMPILATION)
#error "Only <libedataserver/libedataserver.h> should be included directly."
#endif

#ifndef E_WEBDAV_SESSION_H
#define E_WEBDAV_SESSION_H

#include <glib.h>

#include <libedataserver/e-data-server-util.h>
#include <libedataserver/e-soup-session.h>
#include <libedataserver/e-source.h>

/* Standard GObject macros */
#define E_TYPE_WEBDAV_SESSION \
	(e_webdav_session_get_type ())
#define E_WEBDAV_SESSION(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_WEBDAV_SESSION, EWebDAVSession))
#define E_WEBDAV_SESSION_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), E_TYPE_WEBDAV_SESSION, EWebDAVSessionClass))
#define E_IS_WEBDAV_SESSION(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_WEBDAV_SESSION))
#define E_IS_WEBDAV_SESSION_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), E_TYPE_WEBDAV_SESSION))
#define E_WEBDAV_SESSION_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), E_TYPE_WEBDAV_SESSION, EWebDAVSessionClass))

G_BEGIN_DECLS

typedef struct _EWebDAVSession EWebDAVSession;
typedef struct _EWebDAVSessionClass EWebDAVSessionClass;
typedef struct _EWebDAVSessionPrivate EWebDAVSessionPrivate;

/**
 * EWebDAVSession:
 *
 * Contains only private data that should be read and manipulated using the
 * functions below.
 *
 * Since: 3.26
 **/
struct _EWebDAVSession {
	/*< private >*/
	ESoupSession parent;
	EWebDAVSessionPrivate *priv;
};

struct _EWebDAVSessionClass {
	ESoupSessionClass parent_class;

	/* Padding for future expansion */
	gpointer reserved[10];
};

GType		e_webdav_session_get_type		(void) G_GNUC_CONST;

EWebDAVSession *e_webdav_session_new			(ESource *source);

G_END_DECLS

#endif /* E_WEBDAV_SESSION_H */
