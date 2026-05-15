/*
 * SPDX-FileCopyrightText: (C) 1999-2008 Novell, Inc. (www.novell.com)
 * SPDX-FileCopyrightText: (C) 2019 Red Hat, Inc. (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.0-or-later
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
