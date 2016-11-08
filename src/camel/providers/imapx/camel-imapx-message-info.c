/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2016 Red Hat, Inc. (www.redhat.com)
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>

#include "camel/camel.h"
#include "camel-imapx-summary.h"

#include "camel-imapx-message-info.h"

struct _CamelIMAPXMessageInfoPrivate {
	guint32 server_flags;
	CamelNamedFlags *server_user_flags;
	CamelNameValueArray *server_user_tags;
};

enum {
	PROP_0,
	PROP_SERVER_FLAGS,
	PROP_SERVER_USER_FLAGS,
	PROP_SERVER_USER_TAGS
};

G_DEFINE_TYPE (CamelIMAPXMessageInfo, camel_imapx_message_info, CAMEL_TYPE_MESSAGE_INFO_BASE)

static CamelMessageInfo *
imapx_message_info_clone (const CamelMessageInfo *mi,
			  CamelFolderSummary *assign_summary)
{
	CamelMessageInfo *result;

	g_return_val_if_fail (CAMEL_IS_IMAPX_MESSAGE_INFO (mi), NULL);

	result = CAMEL_MESSAGE_INFO_CLASS (camel_imapx_message_info_parent_class)->clone (mi, assign_summary);
	if (!result)
		return NULL;

	if (CAMEL_IS_IMAPX_MESSAGE_INFO (result)) {
		CamelIMAPXMessageInfo *imi, *imi_result;

		imi = CAMEL_IMAPX_MESSAGE_INFO (mi);
		imi_result = CAMEL_IMAPX_MESSAGE_INFO (result);

		camel_imapx_message_info_set_server_flags (imi_result, camel_imapx_message_info_get_server_flags (imi));
		camel_imapx_message_info_take_server_user_flags (imi_result, camel_imapx_message_info_dup_server_user_flags (imi));
		camel_imapx_message_info_take_server_user_tags (imi_result, camel_imapx_message_info_dup_server_user_tags (imi));
	}

	return result;
}

static gboolean
imapx_message_info_load (CamelMessageInfo *mi,
			 const CamelMIRecord *record,
			 /* const */ gchar **bdata_ptr)
{
	CamelIMAPXMessageInfo *imi;

	g_return_val_if_fail (CAMEL_IS_IMAPX_MESSAGE_INFO (mi), FALSE);
	g_return_val_if_fail (record != NULL, FALSE);
	g_return_val_if_fail (bdata_ptr != NULL, FALSE);

	if (!CAMEL_MESSAGE_INFO_CLASS (camel_imapx_message_info_parent_class)->load ||
	    !CAMEL_MESSAGE_INFO_CLASS (camel_imapx_message_info_parent_class)->load (mi, record, bdata_ptr))
		return FALSE;

	imi = CAMEL_IMAPX_MESSAGE_INFO (mi);

	camel_imapx_message_info_set_server_flags (imi, camel_util_bdata_get_number (bdata_ptr, 0));

	/* Reset server-side information, which is not saved into the summary. */
	camel_imapx_message_info_take_server_user_flags (imi, NULL);
	camel_imapx_message_info_take_server_user_tags (imi, NULL);

	return TRUE;
}

static gboolean
imapx_message_info_save (const CamelMessageInfo *mi,
			 CamelMIRecord *record,
			 GString *bdata_str)
{
	CamelIMAPXMessageInfo *imi;

	g_return_val_if_fail (CAMEL_IS_IMAPX_MESSAGE_INFO (mi), FALSE);
	g_return_val_if_fail (record != NULL, FALSE);
	g_return_val_if_fail (bdata_str != NULL, FALSE);

	if (!CAMEL_MESSAGE_INFO_CLASS (camel_imapx_message_info_parent_class)->save ||
	    !CAMEL_MESSAGE_INFO_CLASS (camel_imapx_message_info_parent_class)->save (mi, record, bdata_str))
		return FALSE;

	imi = CAMEL_IMAPX_MESSAGE_INFO (mi);

	camel_util_bdata_put_number (bdata_str, camel_imapx_message_info_get_server_flags (imi));

	return TRUE;
}

static void
imapx_message_info_set_property (GObject *object,
				 guint property_id,
				 const GValue *value,
				 GParamSpec *pspec)
{
	CamelIMAPXMessageInfo *imi = CAMEL_IMAPX_MESSAGE_INFO (object);

	switch (property_id) {
	case PROP_SERVER_FLAGS:
		camel_imapx_message_info_set_server_flags (imi, g_value_get_uint (value));
		return;

	case PROP_SERVER_USER_FLAGS:
		camel_imapx_message_info_take_server_user_flags (imi, g_value_dup_boxed (value));
		return;

	case PROP_SERVER_USER_TAGS:
		camel_imapx_message_info_take_server_user_tags (imi, g_value_dup_boxed (value));
		return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
imapx_message_info_get_property (GObject *object,
				 guint property_id,
				 GValue *value,
				 GParamSpec *pspec)
{
	CamelIMAPXMessageInfo *imi = CAMEL_IMAPX_MESSAGE_INFO (object);

	switch (property_id) {
	case PROP_SERVER_FLAGS:
		g_value_set_uint (value, camel_imapx_message_info_get_server_flags (imi));
		return;

	case PROP_SERVER_USER_FLAGS:
		g_value_take_boxed (value, camel_imapx_message_info_dup_server_user_flags (imi));
		return;

	case PROP_SERVER_USER_TAGS:
		g_value_take_boxed (value, camel_imapx_message_info_dup_server_user_tags (imi));
		return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
imapx_message_info_dispose (GObject *object)
{
	CamelIMAPXMessageInfo *imi = CAMEL_IMAPX_MESSAGE_INFO (object);

	camel_named_flags_free (imi->priv->server_user_flags);
	imi->priv->server_user_flags = NULL;

	camel_name_value_array_free (imi->priv->server_user_tags);
	imi->priv->server_user_tags = NULL;

	/* Chain up to parent's method. */
	G_OBJECT_CLASS (camel_imapx_message_info_parent_class)->dispose (object);
}

static void
camel_imapx_message_info_class_init (CamelIMAPXMessageInfoClass *class)
{
	CamelMessageInfoClass *mi_class;
	GObjectClass *object_class;

	g_type_class_add_private (class, sizeof (CamelIMAPXMessageInfoPrivate));

	mi_class = CAMEL_MESSAGE_INFO_CLASS (class);
	mi_class->clone = imapx_message_info_clone;
	mi_class->load = imapx_message_info_load;
	mi_class->save = imapx_message_info_save;

	object_class = G_OBJECT_CLASS (class);
	object_class->set_property = imapx_message_info_set_property;
	object_class->get_property = imapx_message_info_get_property;
	object_class->dispose = imapx_message_info_dispose;

	/**
	 * CamelIMAPXMessageInfo:server-flags
	 *
	 * Bit-or of #CamelMessageFlags of the flags stored on the server.
	 *
	 * Since: 3.24
	 **/
	g_object_class_install_property (
		object_class,
		PROP_SERVER_FLAGS,
		g_param_spec_uint (
			"server-flags",
			"Server Flags",
			NULL,
			0, G_MAXUINT32, 0,
			G_PARAM_READWRITE));

	/**
	 * CamelIMAPXMessageInfo:server-user-flags
	 *
	 * User flags for the associated message, as stored on the server.
	 * Can be %NULL.
	 *
	 * Since: 3.24
	 **/
	g_object_class_install_property (
		object_class,
		PROP_SERVER_USER_FLAGS,
		g_param_spec_boxed (
			"server-user-flags",
			"Server User Flags",
			NULL,
			CAMEL_TYPE_NAMED_FLAGS,
			G_PARAM_READWRITE));

	/**
	 * CamelIMAPXMessageInfo:server-user-tags
	 *
	 * User tags for the associated message, as stored on the server.
	 * Can be %NULL.
	 *
	 * Since: 3.24
	 **/
	g_object_class_install_property (
		object_class,
		PROP_SERVER_USER_TAGS,
		g_param_spec_boxed (
			"server-user-tags",
			"Server User tags",
			NULL,
			CAMEL_TYPE_NAME_VALUE_ARRAY,
			G_PARAM_READWRITE));
}

static void
camel_imapx_message_info_init (CamelIMAPXMessageInfo *imi)
{
	imi->priv = G_TYPE_INSTANCE_GET_PRIVATE (imi, CAMEL_TYPE_IMAPX_MESSAGE_INFO, CamelIMAPXMessageInfoPrivate);
}

guint32
camel_imapx_message_info_get_server_flags (const CamelIMAPXMessageInfo *imi)
{
	CamelMessageInfo *mi;
	guint32 result;

	g_return_val_if_fail (CAMEL_IS_IMAPX_MESSAGE_INFO (imi), 0);

	mi = CAMEL_MESSAGE_INFO (imi);

	camel_message_info_property_lock (mi);
	result = imi->priv->server_flags;
	camel_message_info_property_unlock (mi);

	return result;
}

gboolean
camel_imapx_message_info_set_server_flags (CamelIMAPXMessageInfo *imi,
					   guint32 server_flags)
{
	CamelMessageInfo *mi;
	gboolean changed;

	g_return_val_if_fail (CAMEL_IS_IMAPX_MESSAGE_INFO (imi), FALSE);

	mi = CAMEL_MESSAGE_INFO (imi);

	camel_message_info_property_lock (mi);

	changed = imi->priv->server_flags != server_flags;
	if (changed)
		imi->priv->server_flags = server_flags;

	camel_message_info_property_unlock (mi);

	if (changed && !camel_message_info_get_abort_notifications (mi)) {
		g_object_notify (G_OBJECT (imi), "server-flags");
		camel_message_info_set_dirty (mi, TRUE);
	}

	return changed;
}

const CamelNamedFlags *
camel_imapx_message_info_get_server_user_flags (const CamelIMAPXMessageInfo *imi)
{
	CamelMessageInfo *mi;
	const CamelNamedFlags *result;

	g_return_val_if_fail (CAMEL_IS_IMAPX_MESSAGE_INFO (imi), NULL);

	mi = CAMEL_MESSAGE_INFO (imi);

	camel_message_info_property_lock (mi);
	result = imi->priv->server_user_flags;
	camel_message_info_property_unlock (mi);

	return result;
}

CamelNamedFlags *
camel_imapx_message_info_dup_server_user_flags (const CamelIMAPXMessageInfo *imi)
{
	CamelMessageInfo *mi;
	CamelNamedFlags *result;

	g_return_val_if_fail (CAMEL_IS_IMAPX_MESSAGE_INFO (imi), NULL);

	mi = CAMEL_MESSAGE_INFO (imi);

	camel_message_info_property_lock (mi);
	result = camel_named_flags_copy (imi->priv->server_user_flags);
	camel_message_info_property_unlock (mi);

	return result;
}

gboolean
camel_imapx_message_info_take_server_user_flags (CamelIMAPXMessageInfo *imi,
						 CamelNamedFlags *server_user_flags)
{
	CamelMessageInfo *mi;
	gboolean changed;

	g_return_val_if_fail (CAMEL_IS_IMAPX_MESSAGE_INFO (imi), FALSE);

	mi = CAMEL_MESSAGE_INFO (imi);

	camel_message_info_property_lock (mi);

	changed = !camel_named_flags_equal (imi->priv->server_user_flags, server_user_flags);

	if (changed) {
		camel_named_flags_free (imi->priv->server_user_flags);
		imi->priv->server_user_flags = server_user_flags;
	} else {
		camel_named_flags_free (server_user_flags);
	}

	camel_message_info_property_unlock (mi);

	if (changed && !camel_message_info_get_abort_notifications (mi)) {
		g_object_notify (G_OBJECT (imi), "server-user-flags");
		camel_message_info_set_dirty (mi, TRUE);
	}

	return changed;
}

const CamelNameValueArray *
camel_imapx_message_info_get_server_user_tags (const CamelIMAPXMessageInfo *imi)
{
	CamelMessageInfo *mi;
	const CamelNameValueArray *result;

	g_return_val_if_fail (CAMEL_IS_IMAPX_MESSAGE_INFO (imi), NULL);

	mi = CAMEL_MESSAGE_INFO (imi);

	camel_message_info_property_lock (mi);
	result = imi->priv->server_user_tags;
	camel_message_info_property_unlock (mi);

	return result;
}

CamelNameValueArray *
camel_imapx_message_info_dup_server_user_tags (const CamelIMAPXMessageInfo *imi)
{
	CamelMessageInfo *mi;
	CamelNameValueArray *result;

	g_return_val_if_fail (CAMEL_IS_IMAPX_MESSAGE_INFO (imi), NULL);

	mi = CAMEL_MESSAGE_INFO (imi);

	camel_message_info_property_lock (mi);
	result = camel_name_value_array_copy (imi->priv->server_user_tags);
	camel_message_info_property_unlock (mi);

	return result;
}

gboolean
camel_imapx_message_info_take_server_user_tags (CamelIMAPXMessageInfo *imi,
						CamelNameValueArray *server_user_tags)
{
	CamelMessageInfo *mi;
	gboolean changed;

	g_return_val_if_fail (CAMEL_IS_IMAPX_MESSAGE_INFO (imi), FALSE);

	mi = CAMEL_MESSAGE_INFO (imi);

	camel_message_info_property_lock (mi);

	changed = !camel_name_value_array_equal (imi->priv->server_user_tags, server_user_tags, CAMEL_COMPARE_CASE_SENSITIVE);

	if (changed) {
		camel_name_value_array_free (imi->priv->server_user_tags);
		imi->priv->server_user_tags = server_user_tags;
	} else {
		camel_name_value_array_free (server_user_tags);
	}

	camel_message_info_property_unlock (mi);

	if (changed && !camel_message_info_get_abort_notifications (mi)) {
		g_object_notify (G_OBJECT (imi), "server-user-tags");
		camel_message_info_set_dirty (mi, TRUE);
	}

	return changed;
}
