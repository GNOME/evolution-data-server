/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* 
 * Authors : 
 *  JP Rosevear <jpr@ximian.com>
 *  Rodrigo Moya <rodrigo@ximian.com>
 *
 * Copyright 2003, Novell, Inc.
 *
 * This program is free software; you can redistribute it and/or 
 * modify it under the terms of version 2 of the GNU General Public 
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <string.h>
#include "e-gw-item.h"
#include "e-gw-connection.h"
#include "e-gw-message.h"

struct _EGwItemPrivate {
	EGwItemType item_type;
	char *container;

	/* properties */
	char *id;
	time_t creation_date;
	time_t start_date;
	time_t end_date;
	time_t due_date;
	gboolean completed;
	char *subject;
	char *message;
	char *classification;
	char *accept_level;
	char *priority;
	char *place;
	GSList *recipient_list;
};

static GObjectClass *parent_class = NULL;

static void
free_recipient (EGwItemRecipient *recipient, gpointer data)
{
	g_free (recipient->email);
	g_free (recipient->display_name);
	g_free (recipient);
}

static void
e_gw_item_dispose (GObject *object)
{
	EGwItem *item = (EGwItem *) object;
	EGwItemPrivate *priv;

	g_return_if_fail (E_IS_GW_ITEM (item));

	priv = item->priv;
	if (priv) {
		if (priv->container) {
			g_free (priv->container);
			priv->container = NULL;
		}

		if (priv->id) {
			g_free (priv->id);
			priv->id = NULL;
		}

		if (priv->subject) {
			g_free (priv->subject);
			priv->subject = NULL;
		}

		if (priv->message) {
			g_free (priv->message);
			priv->message = NULL;
		}

		if (priv->classification) {
			g_free (priv->classification);
			priv->classification = NULL;
		}

		if (priv->accept_level) {
			g_free (priv->accept_level);
			priv->accept_level = NULL;
		}

		if (priv->priority) {
			g_free (priv->priority);
			priv->priority = NULL;
		}

		if (priv->place) {
			g_free (priv->place);
			priv->place = NULL;
		}

		if (priv->recipient_list) {
			g_slist_foreach (priv->recipient_list, (GFunc) free_recipient, NULL);
			priv->recipient_list = NULL;
		}	
	}

	if (parent_class->dispose)
		(* parent_class->dispose) (object);
}

static void
e_gw_item_finalize (GObject *object)
{
	EGwItem *item = (EGwItem *) object;
	EGwItemPrivate *priv;

	g_return_if_fail (E_IS_GW_ITEM (item));

	priv = item->priv;

	/* clean up */
	g_free (priv);
	item->priv = NULL;

	if (parent_class->finalize)
		(* parent_class->finalize) (object);
}

static void
e_gw_item_class_init (EGwItemClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	parent_class = g_type_class_peek_parent (klass);

	object_class->dispose = e_gw_item_dispose;
	object_class->finalize = e_gw_item_finalize;
}

static void
e_gw_item_init (EGwItem *item, EGwItemClass *klass)
{
	EGwItemPrivate *priv;

	/* allocate internal structure */
	priv = g_new0 (EGwItemPrivate, 1);
	priv->item_type = E_GW_ITEM_TYPE_UNKNOWN;
	priv->creation_date = -1;
	priv->start_date = -1;
	priv->end_date = -1;
	priv->due_date = -1;

	item->priv = priv;
}

GType
e_gw_item_get_type (void)
{
	static GType type = 0;

	if (!type) {
		static GTypeInfo info = {
                        sizeof (EGwItemClass),
                        (GBaseInitFunc) NULL,
                        (GBaseFinalizeFunc) NULL,
                        (GClassInitFunc) e_gw_item_class_init,
                        NULL, NULL,
                        sizeof (EGwItem),
                        0,
                        (GInstanceInitFunc) e_gw_item_init
                };
		type = g_type_register_static (G_TYPE_OBJECT, "EGwItem", &info, 0);
	}

	return type;
}

EGwItem *
e_gw_item_new_empty (void)
{
	return g_object_new (E_TYPE_GW_ITEM, NULL);
}

static void 
set_recipient_list_from_soap_parameter (GSList *list, SoupSoapParameter *param)
{
        SoupSoapParameter *param_recipient;
        char *email, *cn;
	EGwItemRecipient *recipient;

        for (param_recipient = soup_soap_parameter_get_first_child_by_name (param, "recipient");
	     param_recipient != NULL;
	     param_recipient = soup_soap_parameter_get_next_child_by_name (param, "recipient")) {
                SoupSoapParameter *subparam;

		recipient = g_new0 (EGwItemRecipient, 1);	
                subparam = soup_soap_parameter_get_first_child_by_name (param_recipient, "email");
                if (subparam) {
                        email = soup_soap_parameter_get_string_value (subparam);
                        if (email)
                                recipient->email = email;
                }        
                subparam = soup_soap_parameter_get_first_child_by_name (param_recipient, "displayName");
                if (subparam) {
                        cn = soup_soap_parameter_get_string_value (subparam);
                        if (cn)
                                recipient->display_name = cn;
                }
                
                subparam = soup_soap_parameter_get_first_child_by_name (param_recipient, "distType");
                if (subparam) {
                        const char *dist_type;
                        dist_type = soup_soap_parameter_get_string_value (subparam);
                        if (!strcmp (dist_type, "TO")) 
                                recipient->type = E_GW_ITEM_RECIPIENT_TO;
                        else if (!strcmp (dist_type, "CC"))
                                recipient->type = E_GW_ITEM_RECIPIENT_CC;
                        else
				recipient->type = E_GW_ITEM_RECIPIENT_NONE;
                }

                list = g_slist_append (list, recipient);
        }        
}

EGwItem *
e_gw_item_new_from_soap_parameter (const char *container, SoupSoapParameter *param)
{
	EGwItem *item;
	char *item_type;
	SoupSoapParameter *subparam, *child;
	
	g_return_val_if_fail (param != NULL, NULL);

	if (strcmp (soup_soap_parameter_get_name (param), "item") != 0) {
		g_warning (G_STRLOC ": Invalid SOAP parameter %s", soup_soap_parameter_get_name (param));
		return NULL;
	}

	item = g_object_new (E_TYPE_GW_ITEM, NULL);
	item_type = soup_soap_parameter_get_property (param, "type");
	if (!g_ascii_strcasecmp (item_type, "Appointment"))
		item->priv->item_type = E_GW_ITEM_TYPE_APPOINTMENT;
	else if (!g_ascii_strcasecmp (item_type, "Task"))
		item->priv->item_type = E_GW_ITEM_TYPE_TASK;
	else {
		g_free (item_type);
		g_object_unref (item);
		return NULL;
	}

	g_free (item_type);

	item->priv->container = g_strdup (container);

	/* If the parameter consists of changes - populate deltas */
	subparam = soup_soap_parameter_get_first_child_by_name (param, "changes");
	if (subparam) {
		SoupSoapParameter *changes = subparam;
		subparam = soup_soap_parameter_get_first_child_by_name (changes, "add");
		if (!subparam)
			subparam = soup_soap_parameter_get_first_child_by_name (changes, "delete");
		if (!subparam)
			subparam = soup_soap_parameter_get_first_child_by_name (changes, "update");
	}
	else subparam = param; /* The item is a complete one, not a delta  */
	
	/* now add all properties to the private structure */
	for (child = soup_soap_parameter_get_first_child (subparam);
	     child != NULL;
	     child = soup_soap_parameter_get_next_child (child)) {
		const char *name;
		char *value;

		name = soup_soap_parameter_get_name (child);

		if (!g_ascii_strcasecmp (name, "acceptLevel"))
			item->priv->accept_level = soup_soap_parameter_get_string_value (child);

		else if (!g_ascii_strcasecmp (name, "class")) {
			item->priv->classification = soup_soap_parameter_get_string_value (child);

		} else if (!g_ascii_strcasecmp (name, "completed")) {
			value = soup_soap_parameter_get_string_value (child);
			if (!g_ascii_strcasecmp (value, "true"))
				item->priv->completed = TRUE;
			else
				item->priv->completed = FALSE;
			g_free (value);

		} else if (!g_ascii_strcasecmp (name, "created")) {
			value = soup_soap_parameter_get_string_value (child);
			item->priv->creation_date = e_gw_connection_get_date_from_string (value);
			g_free (value);

		} else if (!g_ascii_strcasecmp (name, "distribution")) {
			SoupSoapParameter *tp;

			tp = soup_soap_parameter_get_first_child_by_name (child, "recipients");
			if (tp) {
				g_slist_foreach (item->priv->recipient_list, (GFunc) free_recipient, NULL);
				item->priv->recipient_list = NULL;
				set_recipient_list_from_soap_parameter (item->priv->recipient_list, tp);
			}

		} else if (!g_ascii_strcasecmp (name, "dueDate")) {
			value = soup_soap_parameter_get_string_value (child);
			item->priv->due_date = e_gw_connection_get_date_from_string (value);
			g_free (value);

		} else if (!g_ascii_strcasecmp (name, "endDate")) {
			value = soup_soap_parameter_get_string_value (child);
			item->priv->end_date = e_gw_connection_get_date_from_string (value);
			g_free (value);

		} else if (!g_ascii_strcasecmp (name, "id"))
			item->priv->id = soup_soap_parameter_get_string_value (child);

		else if (!g_ascii_strcasecmp (name, "message"))
			item->priv->message = soup_soap_parameter_get_string_value (child);

		else if (!g_ascii_strcasecmp (name, "place"))
			item->priv->place = soup_soap_parameter_get_string_value (child);

		else if (!g_ascii_strcasecmp (name, "priority"))
			item->priv->priority = soup_soap_parameter_get_string_value (child);

		else if (!g_ascii_strcasecmp (name, "startDate")) {
			value = soup_soap_parameter_get_string_value (child);
			item->priv->start_date = e_gw_connection_get_date_from_string (value);
			g_free (value);

		} else if (!g_ascii_strcasecmp (name, "subject"))
			item->priv->subject = soup_soap_parameter_get_string_value (child);
	}

	return item;
}

EGwItemType
e_gw_item_get_item_type (EGwItem *item)
{
	g_return_val_if_fail (E_IS_GW_ITEM (item), E_GW_ITEM_TYPE_UNKNOWN);

	return item->priv->item_type;
}

void
e_gw_item_set_item_type (EGwItem *item, EGwItemType new_type)
{
	g_return_if_fail (E_IS_GW_ITEM (item));

	item->priv->item_type = new_type;
}

const char *
e_gw_item_get_container_id (EGwItem *item)
{
	g_return_val_if_fail (E_IS_GW_ITEM (item), NULL);

	return (const char *) item->priv->container;
}

void
e_gw_item_set_container_id (EGwItem *item, const char *new_id)
{
	g_return_if_fail (E_IS_GW_ITEM (item));

	if (item->priv->container)
		g_free (item->priv->container);
	item->priv->container = g_strdup (new_id);
}

const char *
e_gw_item_get_id (EGwItem *item)
{
	g_return_val_if_fail (E_IS_GW_ITEM (item), NULL);

	return (const char *) item->priv->id;
}

void
e_gw_item_set_id (EGwItem *item, const char *new_id)
{
	g_return_if_fail (E_IS_GW_ITEM (item));

	if (item->priv->id)
		g_free (item->priv->id);
	item->priv->id = g_strdup (new_id);
}

time_t
e_gw_item_get_creation_date (EGwItem *item)
{
	g_return_val_if_fail (E_IS_GW_ITEM (item), -1);

	return item->priv->creation_date;
}

void
e_gw_item_set_creation_date (EGwItem *item, time_t new_date)
{
	g_return_if_fail (E_IS_GW_ITEM (item));

	item->priv->creation_date = new_date;
}

time_t
e_gw_item_get_start_date (EGwItem *item)
{
	g_return_val_if_fail (E_IS_GW_ITEM (item), -1);

	return item->priv->start_date;
}

void
e_gw_item_set_start_date (EGwItem *item, time_t new_date)
{
	g_return_if_fail (E_IS_GW_ITEM (item));

	item->priv->start_date = new_date;
}

time_t
e_gw_item_get_end_date (EGwItem *item)
{
	g_return_val_if_fail (E_IS_GW_ITEM (item), -1);

	return item->priv->end_date;
}

void
e_gw_item_set_end_date (EGwItem *item, time_t new_date)
{
	g_return_if_fail (E_IS_GW_ITEM (item));

	item->priv->end_date = new_date;
}

time_t
e_gw_item_get_due_date (EGwItem *item)
{
	g_return_val_if_fail (E_IS_GW_ITEM (item), -1);

	return item->priv->due_date;
}

void
e_gw_item_set_due_date (EGwItem *item, time_t new_date)
{
	g_return_if_fail (E_IS_GW_ITEM (item));

	item->priv->due_date = new_date;
}

const char *
e_gw_item_get_subject (EGwItem *item)
{
	g_return_val_if_fail (E_IS_GW_ITEM (item), NULL);

	return (const char *) item->priv->subject;
}

void
e_gw_item_set_subject (EGwItem *item, const char *new_subject)
{
	g_return_if_fail (E_IS_GW_ITEM (item));

	if (item->priv->subject)
		g_free (item->priv->subject);
	item->priv->subject = g_strdup (new_subject);
}

const char *
e_gw_item_get_message (EGwItem *item)
{
	g_return_val_if_fail (E_IS_GW_ITEM (item), NULL);

	return (const char *) item->priv->message;
}

void
e_gw_item_set_message (EGwItem *item, const char *new_message)
{
	g_return_if_fail (E_IS_GW_ITEM (item));

	if (item->priv->message)
		g_free (item->priv->message);
	item->priv->message = g_strdup (new_message);
}

const char *
e_gw_item_get_place (EGwItem *item)
{
	g_return_val_if_fail (E_IS_GW_ITEM (item), NULL);

	return item->priv->place;
}

void
e_gw_item_set_place (EGwItem *item, const char *new_place)
{
	g_return_if_fail (E_IS_GW_ITEM (item));

	if (item->priv->place)
		g_free (item->priv->place);
	item->priv->place = g_strdup (new_place);
}

const char *
e_gw_item_get_classification (EGwItem *item)
{
	g_return_val_if_fail (E_IS_GW_ITEM (item), NULL);

	return (const char *) item->priv->classification;
}

void
e_gw_item_set_classification (EGwItem *item, const char *new_class)
{
	g_return_if_fail (E_IS_GW_ITEM (item));

	if (item->priv->classification)
		g_free (item->priv->classification);
	item->priv->classification = g_strdup (new_class);
}

gboolean
e_gw_item_get_completed (EGwItem *item)
{
	g_return_val_if_fail (E_IS_GW_ITEM (item), FALSE);

	return item->priv->completed;
}

void
e_gw_item_set_completed (EGwItem *item, gboolean new_completed)
{
	g_return_if_fail (E_IS_GW_ITEM (item));

	item->priv->completed = new_completed;
}

const char *
e_gw_item_get_accept_level (EGwItem *item)
{
	g_return_val_if_fail (E_IS_GW_ITEM (item), NULL);

	return (const char *) item->priv->accept_level;
}

void
e_gw_item_set_accept_level (EGwItem *item, const char *new_level)
{
	g_return_if_fail (E_IS_GW_ITEM (item));

	if (item->priv->accept_level)
		g_free (item->priv->accept_level);
	item->priv->accept_level = g_strdup (new_level);
}

const char *
e_gw_item_get_priority (EGwItem *item)
{
	g_return_val_if_fail (E_IS_GW_ITEM (item), NULL);

	return (const char *) item->priv->priority;
}

void
e_gw_item_set_priority (EGwItem *item, const char *new_priority)
{
	g_return_if_fail (E_IS_GW_ITEM (item));

	if (item->priv->priority)
		g_free (item->priv->priority);
	item->priv->priority = g_strdup (new_priority);
}

static char *
timet_to_string (time_t t)
{
	gchar *ret;

	ret = g_malloc (17); /* 4+2+2+1+2+2+2+1 + 1 */
	strftime (ret, 17, "%Y%m%dT%H%M%SZ", gmtime (&t));

	return ret;
}

gboolean
e_gw_item_append_to_soap_message (EGwItem *item, SoupSoapMessage *msg)
{
	EGwItemPrivate *priv;
	char *dtstring;

	g_return_val_if_fail (E_IS_GW_ITEM (item), FALSE);
	g_return_val_if_fail (SOUP_IS_SOAP_MESSAGE (msg), FALSE);

	priv = item->priv;

	soup_soap_message_start_element (msg, "item", "types", NULL);

	switch (priv->item_type) {
	case E_GW_ITEM_TYPE_APPOINTMENT :
		soup_soap_message_add_attribute (msg, "type", "Appointment", "xsi", NULL);

		e_gw_message_write_string_parameter (msg, "acceptLevel", NULL, priv->accept_level ? priv->accept_level : "");
		e_gw_message_write_string_parameter (msg, "place", NULL, priv->place ? priv->place : "");
		/* FIXME: distribution */
		break;
	case E_GW_ITEM_TYPE_TASK :
		soup_soap_message_add_attribute (msg, "type", "Task", "xsi", NULL);

		if (priv->due_date != -1) {
			dtstring = timet_to_string (priv->due_date);
			e_gw_message_write_string_parameter (msg, "dueDate", NULL, dtstring);
			g_free (dtstring);
		} else
			e_gw_message_write_string_parameter (msg, "dueDate", NULL, "");

		if (priv->completed)
			e_gw_message_write_string_parameter (msg, "completed", NULL, "1");
		else
			e_gw_message_write_string_parameter (msg, "completed", NULL, "0");

		e_gw_message_write_string_parameter (msg, "priority", NULL, priv->priority ? priv->priority : "");
		break;
	default :
		g_warning (G_STRLOC ": Unknown type for item");
		return FALSE;
	}

	/* add all properties */
	e_gw_message_write_string_parameter (msg, "id", NULL, priv->id);
	e_gw_message_write_string_parameter (msg, "subject", NULL, priv->subject ? priv->subject : "");
	e_gw_message_write_string_parameter (msg, "message", NULL, priv->message ? priv->message : "");
	if (priv->start_date != -1) {
		dtstring = timet_to_string (priv->start_date);
		e_gw_message_write_string_parameter (msg, "startDate", NULL, dtstring);
		g_free (dtstring);
	}
	if (priv->end_date != -1) {
		dtstring = timet_to_string (priv->end_date);
		e_gw_message_write_string_parameter (msg, "endDate", NULL, dtstring);
		g_free (dtstring);
	} else
		e_gw_message_write_string_parameter (msg, "endDate", NULL, "");
	if (priv->creation_date != -1) {
		dtstring = timet_to_string (priv->creation_date);
		e_gw_message_write_string_parameter (msg, "created", NULL, dtstring);
		g_free (dtstring);
	}

	if (priv->classification)
		e_gw_message_write_string_parameter (msg, "class", NULL, priv->classification);
	else
		e_gw_message_write_string_parameter (msg, "class", NULL, "");

	/* finalize the SOAP element */
	soup_soap_message_end_element (msg);

	return TRUE;
}


