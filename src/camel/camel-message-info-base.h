/*
 * SPDX-FileCopyrightText: (C) 2016 Red Hat, Inc. (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#if !defined (__CAMEL_H_INSIDE__) && !defined (CAMEL_COMPILATION)
#error "Only <camel/camel.h> can be included directly."
#endif

#ifndef CAMEL_MESSAGE_INFO_BASE_H
#define CAMEL_MESSAGE_INFO_BASE_H

#include <glib-object.h>

#include <camel/camel-message-info.h>

/* Standard GObject macros */
#define CAMEL_TYPE_MESSAGE_INFO_BASE \
	(camel_message_info_base_get_type ())
#define CAMEL_MESSAGE_INFO_BASE(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), CAMEL_TYPE_MESSAGE_INFO_BASE, CamelMessageInfoBase))
#define CAMEL_MESSAGE_INFO_BASE_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), CAMEL_TYPE_MESSAGE_INFO_BASE, CamelMessageInfoBaseClass))
#define CAMEL_IS_MESSAGE_INFO_BASE(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), CAMEL_TYPE_MESSAGE_INFO_BASE))
#define CAMEL_IS_MESSAGE_INFO_BASE_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), CAMEL_TYPE_MESSAGE_INFO_BASE))
#define CAMEL_MESSAGE_INFO_BASE_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), CAMEL_TYPE_MESSAGE_INFO_BASE, CamelMessageInfoBaseClass))

G_BEGIN_DECLS

typedef struct _CamelMessageInfoBase CamelMessageInfoBase;
typedef struct _CamelMessageInfoBaseClass CamelMessageInfoBaseClass;
typedef struct _CamelMessageInfoBasePrivate CamelMessageInfoBasePrivate;

struct _CamelMessageInfoBase {
	CamelMessageInfo parent;
	CamelMessageInfoBasePrivate *priv;
};

struct _CamelMessageInfoBaseClass {
	CamelMessageInfoClass parent_class;

	/* Padding for future expansion */
	gpointer reserved[20];
};

GType		camel_message_info_base_get_type	(void);

G_END_DECLS

#endif /* CAMEL_MESSAGE_INFO_BASE_H */
