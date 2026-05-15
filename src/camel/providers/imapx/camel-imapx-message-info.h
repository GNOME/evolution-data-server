/*
 * SPDX-FileCopyrightText: (C) 2016 Red Hat, Inc. (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#ifndef CAMEL_IMAPX_MESSAGE_INFO_H
#define CAMEL_IMAPX_MESSAGE_INFO_H

#include <glib-object.h>

#include <camel/camel.h>

/* Standard GObject macros */
#define CAMEL_TYPE_IMAPX_MESSAGE_INFO \
	(camel_imapx_message_info_get_type ())
#define CAMEL_IMAPX_MESSAGE_INFO(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), CAMEL_TYPE_IMAPX_MESSAGE_INFO, CamelIMAPXMessageInfo))
#define CAMEL_IMAPX_MESSAGE_INFO_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), CAMEL_TYPE_IMAPX_MESSAGE_INFO, CamelIMAPXMessageInfoClass))
#define CAMEL_IS_IMAPX_MESSAGE_INFO(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), CAMEL_TYPE_IMAPX_MESSAGE_INFO))
#define CAMEL_IS_IMAPX_MESSAGE_INFO_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), CAMEL_TYPE_IMAPX_MESSAGE_INFO))
#define CAMEL_IMAPX_MESSAGE_INFO_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), CAMEL_TYPE_IMAPX_MESSAGE_INFO, CamelIMAPXMessageInfoClass))

G_BEGIN_DECLS

typedef struct _CamelIMAPXMessageInfo CamelIMAPXMessageInfo;
typedef struct _CamelIMAPXMessageInfoClass CamelIMAPXMessageInfoClass;
typedef struct _CamelIMAPXMessageInfoPrivate CamelIMAPXMessageInfoPrivate;

struct _CamelIMAPXMessageInfo {
	CamelMessageInfoBase parent;
	CamelIMAPXMessageInfoPrivate *priv;
};

struct _CamelIMAPXMessageInfoClass {
	CamelMessageInfoBaseClass parent_class;

	/* Padding for future expansion */
	gpointer reserved[20];
};

GType		camel_imapx_message_info_get_type	(void);

guint32		camel_imapx_message_info_get_server_flags
							(const CamelIMAPXMessageInfo *imi);
gboolean	camel_imapx_message_info_set_server_flags
							(CamelIMAPXMessageInfo *imi,
							 guint32 server_flags);
const CamelNamedFlags *
		camel_imapx_message_info_get_server_user_flags
							(const CamelIMAPXMessageInfo *imi);
CamelNamedFlags *
		camel_imapx_message_info_dup_server_user_flags
							(const CamelIMAPXMessageInfo *imi);
gboolean	camel_imapx_message_info_take_server_user_flags
							(CamelIMAPXMessageInfo *imi,
							 CamelNamedFlags *server_user_flags);
const CamelNameValueArray *
		camel_imapx_message_info_get_server_user_tags
							(const CamelIMAPXMessageInfo *imi);
CamelNameValueArray *
		camel_imapx_message_info_dup_server_user_tags
							(const CamelIMAPXMessageInfo *imi);
gboolean	camel_imapx_message_info_take_server_user_tags
							(CamelIMAPXMessageInfo *imi,
							 CamelNameValueArray *server_user_tags);

G_END_DECLS

#endif /* CAMEL_IMAPX_MESSAGE_INFO_H */
