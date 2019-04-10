/*
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 * Copyright (C) 2019 Red Hat, Inc. (www.redhat.com)
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
 *
 */

#if !defined (__LIBECAL_H_INSIDE__) && !defined (LIBECAL_COMPILATION)
#error "Only <libecal/libecal.h> should be included directly."
#endif

#ifndef E_CAL_COMPONENT_ID_H
#define E_CAL_COMPONENT_ID_H

#include <glib-object.h>

G_BEGIN_DECLS

/**
 * ECalComponentId:
 *
 * An opaque structure containing UID of a component and
 * its recurrence ID (which can be %NULL). Use the functions
 * below to work with it.
 **/
typedef struct _ECalComponentId ECalComponentId;

GType		e_cal_component_id_get_type	(void);
ECalComponentId *
		e_cal_component_id_new		(const gchar *uid,
						 const gchar *rid);
ECalComponentId *
		e_cal_component_id_new_take	(gchar *uid,
						 gchar *rid);
ECalComponentId *
		e_cal_component_id_copy		(const ECalComponentId *id);
void		e_cal_component_id_free		(gpointer id); /* ECalComponentId * */
guint		e_cal_component_id_hash		(gconstpointer id); /* ECalComponentId * */
gboolean	e_cal_component_id_equal	(gconstpointer id1, /* ECalComponentId * */
						 gconstpointer id2); /* ECalComponentId * */
const gchar *	e_cal_component_id_get_uid	(const ECalComponentId *id);
void		e_cal_component_id_set_uid	(ECalComponentId *id,
						 const gchar *uid);
const gchar *	e_cal_component_id_get_rid	(const ECalComponentId *id);
void		e_cal_component_id_set_rid	(ECalComponentId *id,
						 const gchar *rid);

G_END_DECLS

#endif /* E_CAL_COMPONENT_ID_H */
