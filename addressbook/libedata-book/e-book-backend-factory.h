/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* e-book-backend-factory.h
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 *
 * Author: Chris Toshok <toshok@ximian.com>
 */

#ifndef _E_BOOK_BACKEND_FACTORY_H_
#define _E_BOOK_BACKEND_FACTORY_H_

#include <glib-object.h>
#include "e-book-backend.h"

G_BEGIN_DECLS

#define E_TYPE_BOOK_BACKEND_FACTORY        (e_book_backend_factory_get_type ())
#define E_BOOK_BACKEND_FACTORY(o)          (G_TYPE_CHECK_INSTANCE_CAST ((o), E_TYPE_BOOK_BACKEND_FACTORY, EBookBackendFactory))
#define E_BOOK_BACKEND_FACTORY_CLASS(k)    (G_TYPE_CHECK_CLASS_CAST((k), E_TYPE_BOOK_BACKEND_FACTORY, EBookBackendFactoryClass))
#define E_IS_BOOK_BACKEND_FACTORY(o)       (G_TYPE_CHECK_INSTANCE_TYPE ((o), E_TYPE_BOOK_BACKEND_FACTORY))
#define E_IS_BOOK_BACKEND_FACTORY_CLASS(k) (G_TYPE_CHECK_CLASS_TYPE ((k), E_TYPE_BOOK_BACKEND_FACTORY))
#define E_BOOK_BACKEND_FACTORY_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), E_TYPE_BOOK_BACKEND_FACTORY, EBookBackendFactoryClass))

typedef struct _EBookBackendFactoryPrivate EBookBackendFactoryPrivate;

typedef struct {
	GObject            parent_object;
} EBookBackendFactory;

typedef struct {
	GObjectClass parent_class;

	const gchar *   (*get_protocol) (EBookBackendFactory *factory);
	EBookBackend* (*new_backend)  (EBookBackendFactory *factory);
} EBookBackendFactoryClass;

GType                e_book_backend_factory_get_type             (void);

const gchar *          e_book_backend_factory_get_protocol         (EBookBackendFactory *factory);
EBookBackend*        e_book_backend_factory_new_backend          (EBookBackendFactory *factory);

/* use this macro for simple, 1 factory modules */
#define E_BOOK_BACKEND_FACTORY_SIMPLE(p,t,f) \
typedef struct { \
	EBookBackendFactory      parent_object; \
} EBookBackend##t##Factory; \
\
typedef struct { \
	EBookBackendFactoryClass parent_class; \
} EBookBackend##t##FactoryClass; \
\
static void \
_ ## p ##_factory_instance_init (EBookBackend## t ##Factory *factory) \
{ \
} \
\
static const gchar * \
_ ## p ##_get_protocol (EBookBackendFactory *factory) \
{ \
	return #p; \
} \
\
static EBookBackend* \
_ ## p ##_new_backend (EBookBackendFactory *factory) \
{ \
	return (f) (); \
} \
\
static void \
_ ## p ##_factory_class_init (EBookBackend## t ##FactoryClass *klass) \
{ \
	E_BOOK_BACKEND_FACTORY_CLASS (klass)->get_protocol = _ ## p ##_get_protocol; \
	E_BOOK_BACKEND_FACTORY_CLASS (klass)->new_backend = _ ## p ##_new_backend; \
} \
\
static GType \
_ ## p ##_factory_get_type (GTypeModule *module) \
{ \
	GType type; \
\
	const GTypeInfo info = { \
		sizeof (EBookBackend##t##FactoryClass), \
		NULL, /* base_class_init */ \
		NULL, /* base_class_finalize */ \
		(GClassInitFunc)  _ ## p ##_factory_class_init, \
		NULL, /* class_finalize */ \
		NULL, /* class_data */ \
		sizeof (EBookBackend##t##Factory), \
		0,    /* n_preallocs */ \
		(GInstanceInitFunc) _ ## p ##_factory_instance_init \
	}; \
\
	type = g_type_module_register_type (module, \
					    E_TYPE_BOOK_BACKEND_FACTORY, \
					    "EBookBackend" #t "Factory", \
					    &info, 0); \
\
	return type; \
}

G_END_DECLS

#endif /* _E_BOOK_BACKEND_FACTORY_H_ */
