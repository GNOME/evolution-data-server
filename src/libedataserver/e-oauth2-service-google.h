/*
 * SPDX-FileCopyrightText: (C) 2018 Red Hat, Inc. (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#if !defined (__LIBEDATASERVER_H_INSIDE__) && !defined (LIBEDATASERVER_COMPILATION)
#error "Only <libedataserver/libedataserver.h> should be included directly."
#endif

#ifndef E_OAUTH2_SERVICE_GOOGLE_H
#define E_OAUTH2_SERVICE_GOOGLE_H

#include <libedataserver/e-oauth2-service-base.h>

/* Standard GObject macros */
#define E_TYPE_OAUTH2_SERVICE_GOOGLE \
	(e_oauth2_service_google_get_type ())
#define E_OAUTH2_SERVICE_GOOGLE(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_OAUTH2_SERVICE_GOOGLE, EOAuth2ServiceGoogle))
#define E_OAUTH2_SERVICE_GOOGLE_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), E_TYPE_OAUTH2_SERVICE_GOOGLE, EOAuth2ServiceGoogleClass))
#define E_IS_OAUTH2_SERVICE_GOOGLE(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_OAUTH2_SERVICE_GOOGLE))
#define E_IS_OAUTH2_SERVICE_GOOGLE_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), E_TYPE_OAUTH2_SERVICE_GOOGLE))
#define E_OAUTH2_SERVICE_GOOGLE_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), E_TYPE_OAUTH2_SERVICE_GOOGLE, EOAuth2ServiceGoogleClass))

G_BEGIN_DECLS

typedef struct _EOAuth2ServiceGoogle EOAuth2ServiceGoogle;
typedef struct _EOAuth2ServiceGoogleClass EOAuth2ServiceGoogleClass;

struct _EOAuth2ServiceGoogle {
	EOAuth2ServiceBase parent;
};

struct _EOAuth2ServiceGoogleClass {
	EOAuth2ServiceBaseClass parent_class;
};

GType		e_oauth2_service_google_get_type	(void) G_GNUC_CONST;

G_END_DECLS

#endif /* E_OAUTH2_SERVICE_GOOGLE_H */
