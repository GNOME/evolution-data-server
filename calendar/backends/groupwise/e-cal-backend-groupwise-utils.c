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

#include "e-cal-backend-groupwise-utils.h"

static EGwItem *
set_properties_from_cal_component (EGwItem *item, ECalComponent *comp)
{
	const char *uid, *location;
	ECalComponentDateTime dt;
	ECalComponentClassification classif;
	ECalComponentTransparency transp;
	ECalComponentText text;
	int *priority;
	GSList *slist, *sl;

	/* first set specific properties */
	switch (e_cal_component_get_vtype (comp)) {
	case E_CAL_COMPONENT_EVENT :
		e_gw_item_set_item_type (item, E_GW_ITEM_TYPE_APPOINTMENT);

		/* transparency */
		e_cal_component_get_transparency (comp, &transp);
		if (transp == E_CAL_COMPONENT_TRANSP_OPAQUE)
			e_gw_item_set_accept_level (item, E_GW_ITEM_ACCEPT_LEVEL_BUSY);
		else
			e_gw_item_set_accept_level (item, NULL);

		/* location */
		e_cal_component_get_location (comp, &location);
		e_gw_item_set_place (item, location);

		/* FIXME: attendee list, set_distribution */
		break;

	case E_CAL_COMPONENT_TODO :
		e_gw_item_set_item_type (item, E_GW_ITEM_TYPE_TASK);

		/* due date */
		e_cal_component_get_due (comp, &dt);
		if (dt.value) {
			e_gw_item_set_due_date (item, *dt.value);
			e_cal_component_free_datetime (&dt);
		}

		/* priority */
		priority = NULL;
		e_cal_component_get_priority (comp, &priority);
		if (priority && *priority) {
			if (*priority >= 7)
				e_gw_item_set_priority (item, E_GW_ITEM_PRIORITY_LOW);
			else if (*priority >= 5)
				e_gw_item_set_priority (item, E_GW_ITEM_PRIORITY_STANDARD);
			else if (*priority >= 3)
				e_gw_item_set_priority (item, E_GW_ITEM_PRIORITY_HIGH);
			else
				e_gw_item_set_priority (item, NULL);

			e_cal_component_free_priority (priority);
		}

		/* completed */
		e_cal_component_get_completed (comp, &dt.value);
		if (dt.value) {
			e_gw_item_set_completed (item, TRUE);
			e_cal_component_free_datetime (&dt);
		} else
			e_gw_item_set_completed (item, FALSE);

		break;

	default :
		g_object_unref (item);
		return NULL;
	}

	/* set common properties */
	/* UID */
	e_cal_component_get_uid (comp, &uid);
	e_gw_item_set_id (item, uid);

	/* subject */
	e_cal_component_get_summary (comp, &text);
	e_gw_item_set_subject (item, text.value);

	/* description */
	e_cal_component_get_description_list (comp, &slist);
	if (slist) {
		GString *str = g_string_new ("");

		for (sl = slist; sl != NULL; sl = sl->next) {
			ECalComponentText *pt = sl->data;

			if (pt && pt->value)
				str = g_string_append (str, pt->value);
		}

		e_gw_item_set_message (item, (const char *) str->str);

		g_string_free (str, TRUE);
		e_cal_component_free_text_list (slist);
	}

	/* start date */
	e_cal_component_get_dtstart (comp, &dt);
	if (dt.value) {
		e_gw_item_set_start_date (item, *dt.value);
	} else if (e_gw_item_get_item_type (item) == E_GW_ITEM_TYPE_APPOINTMENT) {
		/* appointments need the start date property */
		g_object_unref (item);
		return NULL;
	}

	/* end date */
	e_cal_component_get_dtend (comp, &dt);
	if (dt.value) {
		e_gw_item_set_end_date (item, *dt.value);
	}

	/* creation date */
	e_cal_component_get_created (comp, &dt.value);
	if (dt.value) {
		e_gw_item_set_creation_date (item, *dt.value);
		e_cal_component_free_datetime (&dt);
	} else {
		struct icaltimetype itt;

		e_cal_component_get_dtstamp (comp, &itt);
		e_gw_item_set_creation_date (item, itt);
	}

	/* classification */
	e_cal_component_get_classification (comp, &classif);
	e_gw_item_set_classification (item, classif);

	return item;
}

EGwItem *
e_gw_item_new_from_cal_component (const char *container, ECalComponent *comp)
{
	EGwItem *item;

	g_return_val_if_fail (E_IS_CAL_COMPONENT (comp), NULL);

	item = e_gw_item_new_empty ();
	e_gw_item_set_container_id (item, container);

	return set_properties_from_cal_component (item, comp);
}

ECalComponent *
e_gw_item_to_cal_component (EGwItem *item)
{
	ECalComponent *comp;
	ECalComponentText text;
	ECalComponentDateTime dt;
	const char *description;
	struct icaltimetype itt;
	int priority;
	EGwItemType item_type;

	g_return_val_if_fail (E_IS_GW_ITEM (item), NULL);

	comp = e_cal_component_new ();

	item_type = e_gw_item_get_item_type (item);

	if (item_type == E_GW_ITEM_TYPE_APPOINTMENT)
		e_cal_component_set_new_vtype (comp, E_CAL_COMPONENT_EVENT);
	else if (item_type == E_GW_ITEM_TYPE_TASK)
		e_cal_component_set_new_vtype (comp, E_CAL_COMPONENT_TODO);
	else {
		g_object_unref (comp);
		return NULL;
	}

	/* set common properties */
	/* UID */
	e_cal_component_set_uid (comp, e_gw_item_get_id (item));

	/* summary */
	text.value = e_gw_item_get_subject (item);
	text.altrep = NULL;
	e_cal_component_set_summary (comp, &text);

	/* description */
	description = e_gw_item_get_message (item);
	if (description) {
		GSList l;

		text.value = description;
		text.altrep = NULL;
		l.data = &text;
		l.next = NULL;

		e_cal_component_set_description_list (comp, &l);
	}

	/* creation date */
	itt = e_gw_item_get_creation_date (item);
	e_cal_component_set_created (comp, &itt);
	e_cal_component_set_dtstamp (comp, &itt);

	/* start date */
	itt = e_gw_item_get_start_date (item);
	dt.value = &itt;
	e_cal_component_set_dtstart (comp, &dt);

	/* end date */
	itt = e_gw_item_get_end_date (item);
	dt.value = &itt;
	e_cal_component_set_dtend (comp, &dt);

	/* classification */
	e_cal_component_set_classification (comp, e_gw_item_get_classification (item));

	/* set specific properties */
	switch (item_type) {
	case E_GW_ITEM_TYPE_APPOINTMENT :
		/* transparency */
		description = e_gw_item_get_accept_level (item);
		if (description &&
		    (!strcmp (description, E_GW_ITEM_ACCEPT_LEVEL_BUSY) ||
		     !strcmp (description, E_GW_ITEM_ACCEPT_LEVEL_OUT_OF_OFFICE)))
			e_cal_component_set_transparency (comp, E_CAL_COMPONENT_TRANSP_OPAQUE);
		else
			e_cal_component_set_transparency (comp, E_CAL_COMPONENT_TRANSP_TRANSPARENT);

		/* location */
		e_cal_component_set_location (comp, e_gw_item_get_place (item));

		/* FIXME: attendee list, get_distribution */
		break;
	case E_GW_ITEM_TYPE_TASK :
		/* due date */
		itt = e_gw_item_get_due_date (item);
		dt.value = &itt;
		e_cal_component_set_due (comp, &dt);
		break;

		/* priority */
		description = e_gw_item_get_priority (item);
		if (description) {
			if (!strcmp (description, E_GW_ITEM_PRIORITY_STANDARD))
				priority = 5;
			else if (!strcmp (description, E_GW_ITEM_PRIORITY_HIGH))
				priority = 3;
			else
				priority = 7;
		} else
			priority = 7;

		e_cal_component_set_priority (comp, &priority);

		/* FIXME: EGwItem's completed is a boolean */
		break;
	default :
		return NULL;
	}

	return comp;
}

EGwConnectionStatus
e_gw_connection_send_appointment (EGwConnection *cnc, const char *container, ECalComponent *comp)
{
	EGwItem *item;
	EGwConnectionStatus status;

	g_return_val_if_fail (E_IS_GW_CONNECTION (cnc), E_GW_CONNECTION_STATUS_INVALID_CONNECTION);
	g_return_val_if_fail (E_IS_CAL_COMPONENT (comp), E_GW_CONNECTION_STATUS_INVALID_OBJECT);

	item = e_gw_item_new_from_cal_component (container, comp);
	status = e_gw_connection_send_item (cnc, item);
	g_object_unref (item);

	return status;
}
