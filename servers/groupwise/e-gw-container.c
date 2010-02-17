/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Authors :
 *  JP Rosevear <jpr@ximian.com>
 *  Rodrigo Moya <rodrigo@ximian.com>
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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <string.h>
#include "e-gw-container.h"
#include "e-gw-message.h"

G_DEFINE_TYPE (EGwContainer, e_gw_container, G_TYPE_OBJECT)

struct _EGwContainerPrivate {
	gchar *name;
	gchar *id;
	gchar *parent;
	guint32 unread;
	guint32 total;
	gint sequence;
	gchar *owner;
	GList *user_list;
	gchar *modified;
	EGwContainerType type;
	gboolean is_root;
	gboolean is_writable;
	gboolean is_frequent_contacts; /*indicates  whether this folder is frequent contacts or not */
	gboolean is_shared_by_me;
	gboolean is_shared_to_me;
	gboolean is_system_folder;
};

static GObjectClass *parent_class = NULL;

static void e_gw_container_set_sequence (EGwContainer *container, gint sequence);
static void e_gw_container_set_modified (EGwContainer *container, const gchar *modified);
static void e_gw_container_set_owner(EGwContainer *container, const gchar *owner);
static void e_gw_container_set_is_shared_by_me (EGwContainer *container, gboolean is_shared_by_me);
static void e_gw_container_set_is_shared_to_me (EGwContainer *container, gboolean is_shared_to_me);

static void
free_node(EShUsers *user)
{
	if (user) {
		g_free (user->email);
		g_free (user);
	}
	return;
}

static void
e_gw_container_dispose (GObject *object)
{
	EGwContainer *container = (EGwContainer *) object;

	g_return_if_fail (E_IS_GW_CONTAINER (container));

	if (parent_class->dispose)
		(* parent_class->dispose) (object);
}

static void
e_gw_container_finalize (GObject *object)
{
	EGwContainer *container = (EGwContainer *) object;
	EGwContainerPrivate *priv;

	g_return_if_fail (E_IS_GW_CONTAINER (container));

	priv = container->priv;
	if (priv) {
		if (priv->name) {
			g_free (priv->name);
			priv->name = NULL;
		}

		if (priv->id) {
			g_free (priv->id);
			priv->id = NULL;
		}

		if (priv->parent) {
			g_free (priv->parent);
			priv->parent = NULL;
		}

		if (priv->owner) {
			g_free (priv->owner);
			priv->owner = NULL;
		}

		if (priv->modified) {
			g_free (priv->modified);
			priv->modified = NULL;
		}

		if (priv->user_list) {
			g_list_foreach (priv->user_list,(GFunc) free_node, NULL);
			g_list_free (priv->user_list);
			priv->user_list = NULL;
		}

		g_free (priv);
		container->priv = NULL;
	}

	if (parent_class->finalize)
		(* parent_class->finalize) (object);
}

static void
e_gw_container_class_init (EGwContainerClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	parent_class = g_type_class_peek_parent (klass);

	object_class->dispose = e_gw_container_dispose;
	object_class->finalize = e_gw_container_finalize;
}

static void
e_gw_container_init (EGwContainer *container)
{
	EGwContainerPrivate *priv;

	/* allocate internal structure */
	priv = g_new0 (EGwContainerPrivate, 1);
	priv->is_writable = TRUE;
	priv->is_frequent_contacts = FALSE;
	container->priv = priv;
}

EGwContainer *
e_gw_container_new_from_soap_parameter (SoupSoapParameter *param)
{
	EGwContainer *container;

	g_return_val_if_fail (param != NULL, NULL);

	container = g_object_new (E_TYPE_GW_CONTAINER, NULL);
	if (!e_gw_container_set_from_soap_parameter (container, param)) {
		g_object_unref (container);
		return NULL;
	}

	return container;
}

gboolean
e_gw_container_set_from_soap_parameter (EGwContainer *container, SoupSoapParameter *param)
{
	gchar *value;
	gint int_value;
	gint rights = 0;
	gboolean byme = FALSE;
	gboolean tome = FALSE;
	SoupSoapParameter *subparam;
	SoupSoapParameter *entry_subparam;
	SoupSoapParameter *email_rt_subparam;
	SoupSoapParameter *rights_subparam;

	g_return_val_if_fail (E_IS_GW_CONTAINER (container), FALSE);
	g_return_val_if_fail (param != NULL, FALSE);

	/* retrieve the name */
	subparam = soup_soap_parameter_get_first_child_by_name (param, "name");
	if (!subparam) {
			/* GroupWise 7.X servers does not return the name field.
			This is not an issue with Bonsai 8.X . So, keep this code for
			working well with the broken GW 7.X series */
			e_gw_container_set_name (container, "");
	} else {
			value = soup_soap_parameter_get_string_value (subparam);
			e_gw_container_set_name (container, (const gchar *) value);
			g_free (value);
	}

	/* retrieve the ID */
	subparam = soup_soap_parameter_get_first_child_by_name (param, "id");
	if (!subparam) {
			e_gw_container_set_id (container, "");
	} else {
			value = soup_soap_parameter_get_string_value (subparam);
			e_gw_container_set_id (container, (const gchar *) value);
			g_free (value);
	}

	/* retrieve the parent container id */
	subparam = soup_soap_parameter_get_first_child_by_name (param, "parent");
	if (!subparam) {
		e_gw_container_set_parent_id (container, "");
		container->priv->is_root = TRUE;
	} else {

		value = soup_soap_parameter_get_string_value (subparam);
		e_gw_container_set_parent_id (container, (const gchar *) value);
		g_free (value);
	}

	/*retrieve the folder type*/
	subparam = soup_soap_parameter_get_first_child_by_name (param, "folderType");
	if (!subparam)
		container->priv->type = E_GW_CONTAINER_TYPE_FOLDER;
	else {
		value = soup_soap_parameter_get_string_value (subparam);
		if (!strcmp (value, "Root"))
			container->priv->type = E_GW_CONTAINER_TYPE_ROOT;
		else if (!strcmp (value, "Mailbox"))
			container->priv->type = E_GW_CONTAINER_TYPE_INBOX;
		else if (!strcmp (value, "SentItems"))
			container->priv->type = E_GW_CONTAINER_TYPE_SENT;
		else if (!strcmp (value, "Calendar"))
			container->priv->type = E_GW_CONTAINER_TYPE_CALENDAR;
		else if (!strcmp (value, "Contacts"))
			container->priv->type = E_GW_CONTAINER_TYPE_CONTACTS;
		else if (!strcmp (value, "Draft"))
			container->priv->type = E_GW_CONTAINER_TYPE_DRAFT;
		else if (!strcmp (value, "Trash"))
			container->priv->type = E_GW_CONTAINER_TYPE_TRASH;
		else if (!strcmp (value, "JunkMail"))
			container->priv->type = E_GW_CONTAINER_TYPE_JUNK;
		g_free (value);
	}

	subparam = soup_soap_parameter_get_first_child_by_name (param, "isSystemFolder");
	if (subparam) {
		value = soup_soap_parameter_get_string_value (subparam);
		if (!strcmp (value, "1"))
			container->priv->is_system_folder = TRUE;
		g_free (value);
	}

	/* retrive the unread and total count */
	subparam = soup_soap_parameter_get_first_child_by_name (param, "hasUnread");
	if (!subparam) {
		container->priv->unread = 0;
	} else {
		subparam = soup_soap_parameter_get_first_child_by_name (param, "unreadCount");
		if (subparam) {
			value = soup_soap_parameter_get_string_value (subparam);
			if (value)
				container->priv->unread = atoi(value);
			else
				container->priv->unread = 0; /*XXX:should it be 0?*/

			g_free (value);
		}
	}

	subparam = soup_soap_parameter_get_first_child_by_name (param, "count");
	if (subparam) {
		value = soup_soap_parameter_get_string_value (subparam);
		if (value)
			container->priv->total = atoi(value);
		g_free (value);
	}
	/* Is shared by me*/
	subparam = soup_soap_parameter_get_first_child_by_name (param, "isSharedByMe");
	if (!subparam) {
		e_gw_container_set_is_shared_by_me(container, FALSE);

	} else {
		value = soup_soap_parameter_get_string_value (subparam);
		if (value) {
			e_gw_container_set_is_shared_by_me (container, TRUE);
			byme = TRUE;
		} else {
			e_gw_container_set_is_shared_by_me (container, FALSE);
			byme = FALSE;
		}

		g_free (value);
	}
	/* is shared to me*/
	subparam = soup_soap_parameter_get_first_child_by_name (param, "isSharedToMe");

	if (!subparam) {
		e_gw_container_set_is_shared_to_me (container, FALSE);

	} else {
		value = soup_soap_parameter_get_string_value (subparam);
		if (value) {
			e_gw_container_set_is_shared_to_me (container, TRUE);
			tome = TRUE;
		} else {
			e_gw_container_set_is_shared_to_me (container, FALSE);
			tome = FALSE;
		}

		g_free (value);
	}
	/*Retrieve email add of the sharing person*/
	if (tome || byme) {
		subparam = soup_soap_parameter_get_first_child_by_name (param, "acl");
		if (!subparam)
			g_warning (G_STRLOC ": No ACL");

		else {
			for (entry_subparam = soup_soap_parameter_get_first_child_by_name (subparam, "entry");
					entry_subparam != NULL;
					entry_subparam = soup_soap_parameter_get_next_child_by_name (entry_subparam, "entry")) {

				EShUsers *user = g_new0(EShUsers , 1);
				email_rt_subparam = soup_soap_parameter_get_first_child_by_name (entry_subparam, "email");

				if (!email_rt_subparam) {
					g_warning (G_STRLOC ":Email Tag Not Available");
				} else {
					value = soup_soap_parameter_get_string_value (email_rt_subparam);
					if (value) {
						user->email = value;
					}
					/* Retrieve Rights*/
					email_rt_subparam = soup_soap_parameter_get_first_child_by_name (entry_subparam, "rights");

					if (!email_rt_subparam)
						g_warning (G_STRLOC ": User without any Rights");
					else {
						rights = 0;
						rights_subparam = soup_soap_parameter_get_first_child_by_name (email_rt_subparam, "add");
						if (rights_subparam)
							rights = rights | 0x1;

						rights_subparam = soup_soap_parameter_get_first_child_by_name (email_rt_subparam, "edit");
						if (rights_subparam)
							rights = rights | 0x2;

						rights_subparam = soup_soap_parameter_get_first_child_by_name (email_rt_subparam, "delete");
						if (rights_subparam)
							rights = rights | 0x4;

						user->rights = rights;
					}

					container->priv->user_list = g_list_append (container->priv->user_list, user);

				}

			}

		}

		/*Retrieve owner*/
		subparam = soup_soap_parameter_get_first_child_by_name (param, "owner");
		if (subparam) {
			value = soup_soap_parameter_get_string_value (subparam);
			e_gw_container_set_owner (container, value);
			g_free (value);
		}
	}

		/* shared folder*/
		/*Retrieve When Modified last*/
		subparam = soup_soap_parameter_get_first_child_by_name (param, "modified");

		if (subparam) {
			value = soup_soap_parameter_get_string_value (subparam);
			e_gw_container_set_modified (container, (const gchar *) value);
			g_free (value);
		}

		/*retrieve sequence*/
		subparam = soup_soap_parameter_get_first_child_by_name (param, "sequence");

		if (subparam) {
			int_value = soup_soap_parameter_get_int_value (subparam);
			e_gw_container_set_sequence (container, int_value);
		}

		return TRUE;
}

void
e_gw_container_get_user_list (EGwContainer *container, GList **user_list)
{
	g_return_if_fail (E_GW_CONTAINER (container));

	*user_list = container->priv->user_list;

}

gint
e_gw_container_get_sequence (EGwContainer *container)
{
	g_return_val_if_fail (E_IS_GW_CONTAINER (container), 0);

	return (gint)container->priv->sequence;
}

static  void
e_gw_container_set_sequence (EGwContainer *container, gint sequence)
{
	g_return_if_fail (E_IS_GW_CONTAINER (container));
	container->priv->sequence = sequence;
}

const gchar *
e_gw_container_get_modified (EGwContainer *container)
{
	g_return_val_if_fail (E_IS_GW_CONTAINER (container), NULL);

	return (const gchar *) container->priv->modified;
}

static void
e_gw_container_set_modified (EGwContainer *container, const gchar *modified)
{
	EGwContainerPrivate *priv;

	g_return_if_fail (E_IS_GW_CONTAINER (container));
	g_return_if_fail (modified != NULL);

	priv = container->priv;

	if (priv->modified)
		g_free (priv->modified);
	priv->modified = g_strdup (modified);
}

static void
e_gw_container_set_owner(EGwContainer *container, const gchar *owner)
{
	EGwContainerPrivate *priv;

	g_return_if_fail (E_IS_GW_CONTAINER(container));
	g_return_if_fail (owner!=NULL);

	priv = container->priv;
	if (priv->owner)
		g_free (container->priv->owner);
	container->priv->owner = g_strdup (owner);
}

const gchar *
e_gw_container_get_owner (EGwContainer *container)
{
	g_return_val_if_fail (E_GW_CONTAINER (container), NULL);

	return (const gchar *) container->priv->owner;
}

gint
e_gw_container_get_rights (EGwContainer *container, gchar *email)
{
	GList *user_list = NULL;
	GList *node = NULL;
	EShUsers *user = NULL;

	g_return_val_if_fail (E_IS_GW_CONTAINER (container), 0);

	user_list = container->priv->user_list;

	for (node = user_list; node != NULL; node = node->next) {
		user = node->data;
		if ( !strcmp (user->email, email))
			return user->rights;
	}

	return 0;
}

gboolean
e_gw_container_get_is_shared_by_me (EGwContainer *container)
{
	g_return_val_if_fail (E_IS_GW_CONTAINER (container), FALSE);

	return (gboolean) container->priv->is_shared_by_me;
}

static void
e_gw_container_set_is_shared_by_me (EGwContainer *container, gboolean is_shared_by_me)
{
	g_return_if_fail (E_IS_GW_CONTAINER (container));

	container->priv->is_shared_by_me = is_shared_by_me;
}

gboolean
e_gw_container_get_is_shared_to_me (EGwContainer *container)
{
	g_return_val_if_fail (E_IS_GW_CONTAINER (container), FALSE);

	return (gboolean) container->priv->is_shared_to_me;
}

static void
e_gw_container_set_is_shared_to_me (EGwContainer *container, gboolean is_shared_to_me)
{
	g_return_if_fail (E_IS_GW_CONTAINER (container));

	container->priv->is_shared_to_me = is_shared_to_me;
}

gboolean
e_gw_container_get_is_system_folder (EGwContainer *container)
{
	g_return_val_if_fail (E_IS_GW_CONTAINER (container), FALSE);

	return container->priv->is_system_folder;
}

void
e_gw_container_set_is_system_folder (EGwContainer *container, gboolean is_system_folder)
{
	g_return_if_fail (E_IS_GW_CONTAINER (container));

	container->priv->is_system_folder = is_system_folder;
}

const gchar *
e_gw_container_get_name (EGwContainer *container)
{
	g_return_val_if_fail (E_IS_GW_CONTAINER (container), NULL);

	return (const gchar *) container->priv->name;
}

void
e_gw_container_set_name (EGwContainer *container, const gchar *new_name)
{
	EGwContainerPrivate *priv;

	g_return_if_fail (E_IS_GW_CONTAINER (container));
	g_return_if_fail (new_name != NULL);

	priv = container->priv;

	if (priv->name)
		g_free (priv->name);
	priv->name = g_strdup (new_name);
}

const gchar *
e_gw_container_get_id (EGwContainer *container)
{
	g_return_val_if_fail (E_IS_GW_CONTAINER (container), NULL);

	return (const gchar *) container->priv->id;
}

void
e_gw_container_set_id (EGwContainer *container, const gchar *new_id)
{
	EGwContainerPrivate *priv;

	g_return_if_fail (E_IS_GW_CONTAINER (container));
	g_return_if_fail (new_id != NULL);

	priv = container->priv;

	if (priv->id)
		g_free (priv->id);
	priv->id = g_strdup (new_id);
}

const gchar *
e_gw_container_get_parent_id (EGwContainer *container)
{
	g_return_val_if_fail (E_IS_GW_CONTAINER (container), NULL);

	return (const gchar *) container->priv->parent;
}

void
e_gw_container_set_parent_id (EGwContainer *container, const gchar *parent_id)
{
	EGwContainerPrivate *priv;

	g_return_if_fail (E_IS_GW_CONTAINER (container));
	g_return_if_fail (parent_id != NULL);

	priv = container->priv;

	if (priv->parent)
		g_free (priv->parent);

	priv->parent = g_strdup (parent_id);
}

guint32
e_gw_container_get_total_count (EGwContainer *container)
{
	g_return_val_if_fail (E_IS_GW_CONTAINER (container), -1);

	return container->priv->total;
}

guint32
e_gw_container_get_unread_count (EGwContainer *container)
{
	g_return_val_if_fail (E_IS_GW_CONTAINER (container), -1);

	return container->priv->unread;

}

gboolean
e_gw_container_get_is_writable (EGwContainer *container)
{
	g_return_val_if_fail (E_IS_GW_CONTAINER (container), FALSE);

	return container->priv->is_writable;

}

void
e_gw_container_set_is_writable (EGwContainer *container, gboolean is_writable)
{
	g_return_if_fail (E_IS_GW_CONTAINER (container));

	container->priv->is_writable = is_writable;
}

gboolean
e_gw_container_get_is_frequent_contacts (EGwContainer *container)
{
	g_return_val_if_fail (E_IS_GW_CONTAINER (container), FALSE);

        return container->priv->is_frequent_contacts;

}

void
e_gw_container_set_is_frequent_contacts (EGwContainer *container, gboolean is_frequent_contacts)
{
        g_return_if_fail (E_IS_GW_CONTAINER (container));

        container->priv->is_frequent_contacts = is_frequent_contacts;
}

gboolean
e_gw_container_is_root (EGwContainer *container)
{
	g_return_val_if_fail (E_IS_GW_CONTAINER (container), FALSE);

	return container->priv->is_root;
}

EGwContainerType
e_gw_container_get_container_type (EGwContainer *container)
{
	g_return_val_if_fail (E_IS_GW_CONTAINER (container), FALSE);
	return container->priv->type;
}

/* flag specifies whether we are adding to acl or deleting one or more entries*/
/* flag = 1 :delete entry
 * flag = 2 :update entry
 * flag = 0 :add to acl
 */
void
e_gw_container_form_message (SoupSoapMessage *msg, gchar *id, GList *new_list, const gchar *sub, const gchar *mesg, gint flag)
{
	gboolean add, edit, del;
	gchar *email = NULL;
	GList *node = NULL;
	EShUsers *user = NULL;

	e_gw_message_write_string_parameter (msg, "id", NULL, id);
	soup_soap_message_start_element (msg, "notification", NULL, NULL);
	e_gw_message_write_string_parameter (msg, "subject", NULL, sub);
	e_gw_message_write_string_parameter (msg, "message", NULL, mesg);
	soup_soap_message_end_element (msg);
	soup_soap_message_start_element (msg, "updates", NULL, NULL);

	if (flag == 0) {
		soup_soap_message_start_element (msg, "add", NULL, NULL);
		soup_soap_message_start_element (msg, "acl", NULL, NULL);

		for (node = new_list; node != NULL; node = node->next) {
			user = node->data;
			add=edit=del=FALSE;
			soup_soap_message_start_element (msg, "entry", NULL, NULL);
			e_gw_message_write_string_parameter (msg, "displayName", NULL,"");
			email = g_strdup (user->email);
			if (user->rights & 0x1)
				add = TRUE;
			if (user->rights & 0x2)
				edit = TRUE;
			if (user->rights & 0x4)
				del = TRUE;

			e_gw_message_write_string_parameter (msg, "email", NULL, email);
			soup_soap_message_start_element (msg, "rights", NULL, NULL);
			e_gw_message_write_int_parameter (msg, "read", NULL, 1);
			e_gw_message_write_int_parameter (msg, "add", NULL, add);
			e_gw_message_write_int_parameter (msg, "edit", NULL, edit);
			e_gw_message_write_int_parameter (msg, "delete", NULL, del);

			soup_soap_message_end_element (msg);
			soup_soap_message_end_element (msg);
		}

		soup_soap_message_end_element (msg);
		soup_soap_message_end_element (msg);

	} else	if (flag == 1) {
		soup_soap_message_start_element (msg, "delete", NULL, NULL);
		soup_soap_message_start_element (msg, "acl", NULL, NULL);

		for (node = new_list; node != NULL; node = node->next) {
			user = node->data;
			add = edit = del = FALSE;
			soup_soap_message_start_element (msg, "entry", NULL, NULL);
			e_gw_message_write_string_parameter (msg, "displayName", NULL, "name");
			email = g_strdup (user->email);

			if (user->rights & 0x1)
				add = TRUE;
			if (user->rights & 0x2)
				edit = TRUE;
			if (user->rights & 0x4)
				del = TRUE;

			e_gw_message_write_string_parameter (msg, "email", NULL, email);
			soup_soap_message_start_element(msg, "rights", NULL, NULL);
			e_gw_message_write_int_parameter (msg, "read", NULL, 1);
			e_gw_message_write_int_parameter (msg, "add", NULL, add);
			e_gw_message_write_int_parameter (msg, "edit", NULL, edit);
			e_gw_message_write_int_parameter (msg, "delete", NULL, del);

			soup_soap_message_end_element (msg);
			soup_soap_message_end_element (msg);

		}
		soup_soap_message_end_element (msg);
		soup_soap_message_end_element (msg);

	} else if (flag == 2) {
		soup_soap_message_start_element (msg, "update", NULL, NULL);
		soup_soap_message_start_element (msg, "acl", NULL, NULL);

		for (node = new_list; node != NULL; node = node->next) {
			user = node->data;
			add = edit = del = FALSE;
			soup_soap_message_start_element (msg, "entry", NULL, NULL);
			e_gw_message_write_string_parameter (msg, "displayName",NULL,"");
			email = g_strdup (user->email);
			if (user->rights & 0x1)
				add = TRUE;
			if (user->rights & 0x2)
				edit = TRUE;
			if (user->rights & 0x4)
				del =TRUE;

			e_gw_message_write_string_parameter (msg, "email", NULL, email);
			soup_soap_message_start_element (msg, "rights", NULL, NULL);
			e_gw_message_write_int_parameter (msg, "read", NULL, 1);
			e_gw_message_write_int_parameter (msg, "add", NULL, add);
			e_gw_message_write_int_parameter (msg, "edit", NULL, edit);
			e_gw_message_write_int_parameter (msg, "delete", NULL, del);

			soup_soap_message_end_element (msg);
			soup_soap_message_end_element (msg);
		}

		soup_soap_message_end_element (msg);
		soup_soap_message_end_element (msg);

	}

	soup_soap_message_end_element (msg);
}
