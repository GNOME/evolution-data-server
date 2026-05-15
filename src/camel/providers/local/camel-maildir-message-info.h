/*
 * SPDX-FileCopyrightText: (C) 2016 Red Hat, Inc. (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#ifndef CAMEL_MAILDIR_MESSAGE_INFO_H
#define CAMEL_MAILDIR_MESSAGE_INFO_H

#include <glib-object.h>

#include <camel/camel.h>

/* Standard GObject macros */
#define CAMEL_TYPE_MAILDIR_MESSAGE_INFO \
	(camel_maildir_message_info_get_type ())
#define CAMEL_MAILDIR_MESSAGE_INFO(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), CAMEL_TYPE_MAILDIR_MESSAGE_INFO, CamelMaildirMessageInfo))
#define CAMEL_MAILDIR_MESSAGE_INFO_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), CAMEL_TYPE_MAILDIR_MESSAGE_INFO, CamelMaildirMessageInfoClass))
#define CAMEL_IS_MAILDIR_MESSAGE_INFO(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), CAMEL_TYPE_MAILDIR_MESSAGE_INFO))
#define CAMEL_IS_MAILDIR_MESSAGE_INFO_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), CAMEL_TYPE_MAILDIR_MESSAGE_INFO))
#define CAMEL_MAILDIR_MESSAGE_INFO_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), CAMEL_TYPE_MAILDIR_MESSAGE_INFO, CamelMaildirMessageInfoClass))

G_BEGIN_DECLS

typedef struct _CamelMaildirMessageInfo CamelMaildirMessageInfo;
typedef struct _CamelMaildirMessageInfoClass CamelMaildirMessageInfoClass;
typedef struct _CamelMaildirMessageInfoPrivate CamelMaildirMessageInfoPrivate;

struct _CamelMaildirMessageInfo {
	CamelMessageInfoBase parent;
	CamelMaildirMessageInfoPrivate *priv;
};

struct _CamelMaildirMessageInfoClass {
	CamelMessageInfoBaseClass parent_class;

	/* Padding for future expansion */
	gpointer reserved[20];
};

GType		camel_maildir_message_info_get_type	(void);

const gchar *	camel_maildir_message_info_get_filename	(const CamelMaildirMessageInfo *mmi);
gchar *		camel_maildir_message_info_dup_filename	(const CamelMaildirMessageInfo *mmi);
gboolean	camel_maildir_message_info_set_filename	(CamelMaildirMessageInfo *mmi,
							 const gchar *filename);
gboolean	camel_maildir_message_info_take_filename
							(CamelMaildirMessageInfo *mmi,
							 gchar *filename);

G_END_DECLS

#endif /* CAMEL_MAILDIR_MESSAGE_INFO_H */
