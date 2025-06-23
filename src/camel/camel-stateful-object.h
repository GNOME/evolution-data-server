/*
 * Copyright 2025 Collabora, Ltd. (https://www.collabora.com)
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

#if !defined (__CAMEL_H_INSIDE__) && !defined (CAMEL_COMPILATION)
#error "Only <camel/camel.h> can be included directly."
#endif

#ifndef CAMEL_STATEFUL_OBJECT_H
#define CAMEL_STATEFUL_OBJECT_H

#include <glib-object.h>

G_BEGIN_DECLS

#define CAMEL_TYPE_STATEFUL_OBJECT (camel_stateful_object_get_type ())
G_DECLARE_INTERFACE (CamelStatefulObject, camel_stateful_object, CAMEL, STATEFUL_OBJECT, GObject)

struct _CamelStatefulObjectInterface {
	GTypeInterface parent_iface;

	const gchar *	(*get_state_file)		(CamelStatefulObject *self);
	guint32		(*get_property_tag)		(CamelStatefulObject *self,
							 guint property_id);
};

gboolean	camel_stateful_object_read_state		(CamelStatefulObject *self);
gboolean	camel_stateful_object_write_state		(CamelStatefulObject *self);
gboolean	camel_stateful_object_delete_state_file	(CamelStatefulObject *self,
								 GError **error);
const gchar *	camel_stateful_object_get_state_file		(CamelStatefulObject *self);

G_END_DECLS

#endif /* CAMEL_STATEFUL_OBJECT_H */
