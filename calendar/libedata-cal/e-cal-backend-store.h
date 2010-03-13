/*-*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* e-cal-backend-store.h
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * Authors: Chenthill Palanisamy <pchenthill@novell.com>
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#ifndef _E_CAL_BACKEND_STORE
#define _E_CAL_BACKEND_STORE

#include <glib-object.h>
#include <libecal/e-cal-component.h>
#include <libecal/e-cal.h>

G_BEGIN_DECLS

#define E_TYPE_CAL_BACKEND_STORE e_cal_backend_store_get_type()

#define E_CAL_BACKEND_STORE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), E_TYPE_CAL_BACKEND_STORE, ECalBackendStore))

#define E_CAL_BACKEND_STORE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), E_TYPE_CAL_BACKEND_STORE, ECalBackendStoreClass))

#define E_IS_CAL_BACKEND_STORE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), E_TYPE_CAL_BACKEND_STORE))

#define E_IS_CAL_BACKEND_STORE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), E_TYPE_CAL_BACKEND_STORE))

#define E_CAL_BACKEND_STORE_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), E_TYPE_CAL_BACKEND_STORE, ECalBackendStoreClass))

typedef struct _ECalBackendStorePrivate ECalBackendStorePrivate;

/**
 * ECalBackendStore:
 *
 * Since: 2.28
 **/
typedef struct {
	GObject parent;
	ECalBackendStorePrivate *priv;
} ECalBackendStore;

typedef struct {
	GObjectClass parent_class;

	/* virtual methods */
	gboolean	(*load) (ECalBackendStore *store);
	gboolean	(*remove) (ECalBackendStore *store);
	gboolean	(*clean) (ECalBackendStore *store);

	ECalComponent *	(*get_component) (ECalBackendStore *store, const gchar *uid, const gchar *rid);
	gboolean	(*put_component) (ECalBackendStore *store, ECalComponent *comp);
	gboolean	(*remove_component) (ECalBackendStore *store, const gchar *uid, const gchar *rid);
	gboolean	(*has_component) (ECalBackendStore *store, const gchar *uid, const gchar *rid);

	GSList *	(*get_components_by_uid) (ECalBackendStore *store, const gchar *uid);
	GSList *	(*get_components) (ECalBackendStore *store);
	GSList *	(*get_component_ids) (ECalBackendStore *store);

	const icaltimezone *	(*get_timezone) (ECalBackendStore *store, const gchar *tzid);
	gboolean	(*put_timezone) (ECalBackendStore *store, const icaltimezone *zone);
	gboolean	(*remove_timezone) (ECalBackendStore *store, const gchar *tzid);

	const icaltimezone *	(*get_default_timezone) (ECalBackendStore *store);
	gboolean	(*set_default_timezone) (ECalBackendStore *store, const icaltimezone *zone);

	void	(*thaw_changes) (ECalBackendStore *store);
	void	(*freeze_changes) (ECalBackendStore *store);

	const gchar *	(*get_key_value) (ECalBackendStore *store, const gchar *key);
	gboolean	(*put_key_value) (ECalBackendStore *store, const gchar *key, const gchar *value);

} ECalBackendStoreClass;

GType e_cal_backend_store_get_type (void);

const gchar *e_cal_backend_store_get_path (ECalBackendStore *store);

gboolean		e_cal_backend_store_load (ECalBackendStore *store);
gboolean		e_cal_backend_store_is_loaded (ECalBackendStore *store);
gboolean		e_cal_backend_store_remove (ECalBackendStore *store);
gboolean		e_cal_backend_store_clean (ECalBackendStore *store);
ECalComponent *		e_cal_backend_store_get_component (ECalBackendStore *store, const gchar *uid, const gchar *rid);
gboolean		e_cal_backend_store_put_component (ECalBackendStore *store, ECalComponent *comp);
gboolean		e_cal_backend_store_remove_component (ECalBackendStore *store, const gchar *uid, const gchar *rid);
gboolean		e_cal_backend_store_has_component (ECalBackendStore *store, const gchar *uid, const gchar *rid);
const icaltimezone *	e_cal_backend_store_get_timezone (ECalBackendStore *store, const gchar *tzid);
gboolean		e_cal_backend_store_put_timezone (ECalBackendStore *store, const icaltimezone *zone);
gboolean		e_cal_backend_store_remove_timezone (ECalBackendStore *store, const gchar *tzid);
const icaltimezone *	e_cal_backend_store_get_default_timezone (ECalBackendStore *store);
gboolean		e_cal_backend_store_set_default_timezone (ECalBackendStore *store, const icaltimezone *zone);
GSList *		e_cal_backend_store_get_components_by_uid (ECalBackendStore *store, const gchar *uid);
GSList *		e_cal_backend_store_get_components (ECalBackendStore *store);
GSList *		e_cal_backend_store_get_component_ids (ECalBackendStore *store);
const gchar *		e_cal_backend_store_get_key_value (ECalBackendStore *store, const gchar *key);
gboolean		e_cal_backend_store_put_key_value (ECalBackendStore *store, const gchar *key, const gchar *value);
void			e_cal_backend_store_thaw_changes (ECalBackendStore *store);
void			e_cal_backend_store_freeze_changes (ECalBackendStore *store);

G_END_DECLS

#endif /* _E_CAL_BACKEND_STORE */
