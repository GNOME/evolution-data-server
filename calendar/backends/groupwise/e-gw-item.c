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
#include "e-gw-item.h"
#include "e-gw-message.h"

struct _EGwItemPrivate {
	EGwItemType item_type;
	char *container;
	gpointer item_data;
};

static GObjectClass *parent_class = NULL;

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
e_gw_item_new_appointment (const char *container, ECalComponent *comp)
{
	EGwItem *item;

	g_return_val_if_fail (E_IS_CAL_COMPONENT (comp), NULL);

	item = g_object_new (E_TYPE_GW_ITEM, NULL);
	item->priv->item_type = E_GW_ITEM_TYPE_APPOINTMENT;
	item->priv->container = g_strdup (container);
	item->priv->item_data = comp;

	return item;
}

static void
append_appointment_properties (EGwItem *item, SoupSoapMessage *msg)
{
	ECalComponent *comp;
	ECalComponentText text;
	EGwItemPrivate *priv = item->priv;

	comp = E_CAL_COMPONENT (priv->item_data);

	/* subject property */
	e_cal_component_get_summary (comp, &text);
	if (text.value)
		e_gw_message_write_string_parameter (msg, "subject", NULL, text.value);
}

gboolean
e_gw_item_append_to_soap_message (EGwItem *item, SoupSoapMessage *msg)
{
	EGwItemPrivate *priv;

	g_return_val_if_fail (E_IS_GW_ITEM (item), FALSE);
	g_return_val_if_fail (SOUP_IS_SOAP_MESSAGE (msg), FALSE);

	priv = item->priv;

	soup_soap_message_start_element (msg, "item", "types", NULL);

	switch (priv->item_type) {
	case E_GW_ITEM_TYPE_APPOINTMENT :
		soup_soap_message_add_attribute (msg, "type", "Appointment", "xsi", NULL);
		append_appointment_properties (item, msg);
		break;
	default :
		g_warning (G_STRLOC ": Unknown type for item");
		return FALSE;
	}

	soup_soap_message_end_element (msg);

	return TRUE;
}
