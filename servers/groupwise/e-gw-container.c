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
#include "e-gw-container.h"

struct _EGwContainerPrivate {
	char *name;
	char *id;
	gboolean is_writable;
};

static GObjectClass *parent_class = NULL;

static void
e_gw_container_dispose (GObject *object)
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
	}

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
e_gw_container_init (EGwContainer *container, EGwContainerClass *klass)
{
	EGwContainerPrivate *priv;

	/* allocate internal structure */
	priv = g_new0 (EGwContainerPrivate, 1);

	container->priv = priv;
}

GType
e_gw_container_get_type (void)
{
	static GType type = 0;

	if (!type) {
		static GTypeInfo info = {
                        sizeof (EGwContainerClass),
                        (GBaseInitFunc) NULL,
                        (GBaseFinalizeFunc) NULL,
                        (GClassInitFunc) e_gw_container_class_init,
                        NULL, NULL,
                        sizeof (EGwContainer),
                        0,
                        (GInstanceInitFunc) e_gw_container_init
                };
		type = g_type_register_static (G_TYPE_OBJECT, "EGwContainer", &info, 0);
	}

	return type;
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
	char *value;
	SoupSoapParameter *subparam;

	g_return_val_if_fail (E_IS_GW_CONTAINER (container), FALSE);
	g_return_val_if_fail (param != NULL, FALSE);


	/* retrieve the name */
	subparam = soup_soap_parameter_get_first_child_by_name (param, "name");
	if (!subparam) {
		g_warning (G_STRLOC ": found container with no name");
		return FALSE;
	}

	value = soup_soap_parameter_get_string_value (subparam);
	e_gw_container_set_name (container, (const char *) value);
	g_free (value);

	/* retrieve the ID */
	subparam = soup_soap_parameter_get_first_child_by_name (param, "id");
	if (!subparam) {
		g_warning (G_STRLOC ": found container with no ID");
		return FALSE;
	}

	value = soup_soap_parameter_get_string_value (subparam);
	e_gw_container_set_id (container, (const char *) value);
	g_free (value);

	return TRUE;
}

const char *
e_gw_container_get_name (EGwContainer *container)
{
	g_return_val_if_fail (E_IS_GW_CONTAINER (container), NULL);

	return (const char *) container->priv->name;
}

void
e_gw_container_set_name (EGwContainer *container, const char *new_name)
{
	EGwContainerPrivate *priv;

	g_return_if_fail (E_IS_GW_CONTAINER (container));
	g_return_if_fail (new_name != NULL);

	priv = container->priv;

	if (priv->name)
		g_free (priv->name);
	priv->name = g_strdup (new_name);
}

const char *
e_gw_container_get_id (EGwContainer *container)
{
	g_return_val_if_fail (E_IS_GW_CONTAINER (container), NULL);

	return (const char *) container->priv->id;
}

void
e_gw_container_set_id (EGwContainer *container, const char *new_id)
{
	EGwContainerPrivate *priv;

	g_return_if_fail (E_IS_GW_CONTAINER (container));
	g_return_if_fail (new_id != NULL);

	priv = container->priv;

	if (priv->id)
		g_free (priv->id);
	priv->id = g_strdup (new_id);
}

gboolean 
e_gw_container_get_is_writable (EGwContainer *container)
{
	g_return_if_fail (E_IS_GW_CONTAINER (container));
	
	return container->priv->is_writable;

}

void 
e_gw_container_set_is_writable (EGwContainer *container, gboolean is_writable)
{
	g_return_if_fail (E_IS_GW_CONTAINER (container));
	
	container->priv->is_writable = is_writable;
}
