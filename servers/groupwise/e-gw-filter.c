/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Authors :
 * Sivaiah Nallagatla <snallagatla@novell.com>
 *
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

#include "e-gw-filter.h"
#include "e-gw-message.h"

G_DEFINE_TYPE (EGwFilter, e_gw_filter, G_TYPE_OBJECT)

static GObjectClass *parent_class = NULL;

struct _FilterComponent {
	gint operation;
	gchar *field_name;
	gchar *field_value;
	gint num_of_conditions;
};

typedef struct _FilterComponent  FilterComponent;

struct _EGwFilterPrivate {
	GSList *component_list;
	gint  filter_group_type; /* stores, whether all condtions are to be met or any one of them*/

};

void
e_gw_filter_add_filter_component (EGwFilter *filter, EGwFilterOpType operation, const gchar *field_name, const gchar *field_value)
{
	FilterComponent *component;

	g_return_if_fail (E_IS_GW_FILTER (filter));
	g_return_if_fail (field_name != NULL);
	g_return_if_fail (field_value != NULL);

	component = g_new0 (FilterComponent, 1);
	component->operation = operation;
	component->field_name = g_strdup (field_name);
	component->field_value = g_strdup (field_value);
	filter->priv->component_list = g_slist_prepend (filter->priv->component_list, component);

}

void
e_gw_filter_group_conditions (EGwFilter *filter, EGwFilterOpType operation, gint num_of_condtions)
{
	FilterComponent *component;

	g_return_if_fail (E_IS_GW_FILTER (filter));
	g_return_if_fail ( operation == E_GW_FILTER_OP_AND || operation == E_GW_FILTER_OP_OR ||
			  operation == E_GW_FILTER_OP_NOT);
	component = g_new0 (FilterComponent, 1);
	component->operation = operation;
	component->num_of_conditions =  num_of_condtions;
	filter->priv->component_list = g_slist_prepend (filter->priv->component_list, component);
}

static void
append_child_component (FilterComponent* filter_component, SoupSoapMessage *msg)
{

	const gchar *operation_name;

	g_return_if_fail (SOUP_IS_SOAP_MESSAGE (msg));
	soup_soap_message_start_element (msg, "element", NULL, NULL);
	operation_name = NULL;

	switch (filter_component->operation) {

		case E_GW_FILTER_OP_EQUAL :
			operation_name = "eq";
			break;
		case E_GW_FILTER_OP_NOTEQUAL :
			operation_name = "ne";
			break;
		case E_GW_FILTER_OP_GREATERTHAN :
			operation_name = "gt";
			break;
		case E_GW_FILTER_OP_LESSTHAN :
			operation_name = "lt";
			break;
		case E_GW_FILTER_OP_GREATERTHAN_OR_EQUAL :
			operation_name = "gte";
			break;
		case E_GW_FILTER_OP_LESSTHAN_OR_EQUAL :
			operation_name = "lte";
			break;
		case E_GW_FILTER_OP_CONTAINS :
			operation_name = "contains";
			break;
		case E_GW_FILTER_OP_CONTAINSWORD :
			operation_name = "containsWord";
			break;
		case E_GW_FILTER_OP_BEGINS :
			operation_name = "begins";
			break;
		case E_GW_FILTER_OP_EXISTS :
			operation_name = "exists";
			break;
		case E_GW_FILTER_OP_NOTEXISTS :
			operation_name = "notExist";
			break;

	}

	if (operation_name != NULL) {

			e_gw_message_write_string_parameter (msg, "op", NULL, operation_name);
			e_gw_message_write_string_parameter (msg, "field", NULL, filter_component->field_name);
			e_gw_message_write_string_parameter (msg, "value", NULL, filter_component->field_value);
	}

	soup_soap_message_end_element (msg);
}

static GSList*
append_complex_component (GSList *component_list, SoupSoapMessage *msg)
{
	FilterComponent *filter_component;
	gint num_of_condtions;
	gint i;

	filter_component = (FilterComponent* )component_list->data;
	if (filter_component->operation == E_GW_FILTER_OP_AND || filter_component->operation == E_GW_FILTER_OP_OR
	    ||  filter_component->operation == E_GW_FILTER_OP_NOT ) {

		soup_soap_message_start_element (msg, "group", NULL, NULL);
		if (filter_component->operation == E_GW_FILTER_OP_AND)
			e_gw_message_write_string_parameter (msg, "op", NULL, "and");
		else if (filter_component->operation == E_GW_FILTER_OP_OR)
			e_gw_message_write_string_parameter (msg, "op", NULL, "or");
		else
			e_gw_message_write_string_parameter (msg, "op", NULL, "not");
	}
	num_of_condtions = filter_component->num_of_conditions;
	for ( i = 0; i < num_of_condtions && component_list; i++) {
		component_list = g_slist_next (component_list);
		filter_component = (FilterComponent *)component_list->data;
		if (filter_component->operation == E_GW_FILTER_OP_AND || filter_component->operation == E_GW_FILTER_OP_OR
		    || filter_component->operation == E_GW_FILTER_OP_NOT ) {
			component_list = append_complex_component (component_list, msg);
		}
		else
			append_child_component (filter_component, msg);

	}
	soup_soap_message_end_element (msg);

	return component_list;
}

void
e_gw_filter_append_to_soap_message (EGwFilter *filter, SoupSoapMessage *msg)
{
	EGwFilterPrivate *priv;
	GSList *component_list;
	FilterComponent *filter_component;

	g_return_if_fail (E_IS_GW_FILTER (filter));
	g_return_if_fail (SOUP_IS_SOAP_MESSAGE (msg));

	priv = filter->priv;
	component_list = priv->component_list;

	soup_soap_message_start_element (msg, "filter", NULL, NULL);
	for (; component_list != NULL; component_list = g_slist_next (component_list)) {
		filter_component = (FilterComponent *) (component_list->data);
		if (filter_component->operation == E_GW_FILTER_OP_AND || filter_component->operation == E_GW_FILTER_OP_OR
		    || filter_component->operation == E_GW_FILTER_OP_NOT) {
			component_list = append_complex_component (component_list, msg);
		}
		else
			append_child_component (filter_component, msg);
	soup_soap_message_end_element (msg); /* end filter */

	}
}

static void
e_gw_filter_finalize (GObject *object)
{
	EGwFilter *filter;
	EGwFilterPrivate *priv;
	GSList *filter_components;
	FilterComponent *component;

	filter = E_GW_FILTER (object);
	priv = filter->priv;
	filter_components = priv->component_list;
	for (; filter_components != NULL; filter_components = g_slist_next (filter_components)) {
		component = filter_components->data;
		g_free (component->field_name);
		g_free (component->field_value);
		g_free (filter_components->data);
	}

	g_slist_free (priv->component_list);
	g_free (priv);
	filter->priv = NULL;
	if (parent_class->finalize)
		(* parent_class->finalize) (object);
}

static void
e_gw_filter_dispose (GObject *object)
{

	if (parent_class->dispose)
		(* parent_class->dispose) (object);

}

static void
e_gw_filter_init (EGwFilter *filter)
{
	EGwFilterPrivate *priv;

	priv = g_new0(EGwFilterPrivate, 1);
	priv->filter_group_type = E_GW_FILTER_OP_AND; /*by default all condtions are to be met*/
	priv->component_list = NULL;
	filter->priv = priv;
}

static void
e_gw_filter_class_init (EGwFilterClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	parent_class = g_type_class_peek_parent (klass);
	object_class->dispose = e_gw_filter_dispose;
	object_class->finalize = e_gw_filter_finalize;
}

EGwFilter *
e_gw_filter_new (void)
{
	return g_object_new (E_TYPE_GW_FILTER, NULL);
}
