/*
 * e-system-source.h
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with the program; if not, see <http://www.gnu.org/licenses/>
 *
 */

#ifndef E_SYSTEM_SOURCE_H
#define E_SYSTEM_SOURCE_H

#include <libedataserver/e-source.h>

/* Standard GObject macros */
#define E_TYPE_SYSTEM_SOURCE \
	(e_system_source_get_type ())
#define E_SYSTEM_SOURCE(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_SYSTEM_SOURCE, ESystemSource))
#define E_SYSTEM_SOURCE_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), E_TYPE_SYSTEM_SOURCE, ESystemSourceClass))
#define E_IS_SYSTEM_SOURCE(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_SYSTEM_SOURCE))
#define E_IS_SYSTEM_SOURCE_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), E_TYPE_SYSTEM_SOURCE))
#define E_SYSTEM_SOURCE_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), E_TYPE_SOURCE, ESystemSourceClass))

G_BEGIN_DECLS

typedef struct _ESystemSource ESystemSource;
typedef struct _ESystemSourceClass ESystemSourceClass;
typedef struct _ESystemSourcePrivate ESystemSourcePrivate;

struct _ESystemSource {
	ESource parent;
	ESystemSourcePrivate *priv;
};

struct _ESystemSourceClass {
	ESourceClass parent_class;
};

GType		e_system_source_get_type	(void) G_GNUC_CONST;
ESource *	e_system_source_new		(void);

G_END_DECLS

#endif /* E_SYSTEM_SOURCE_H */
