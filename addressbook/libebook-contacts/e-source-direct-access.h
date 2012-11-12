/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/* e-source-direct-access.h - Directly Accessible Extension.
 *
 * Copyright (C) 2012 Openismus GmbH
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
 * Authors: Tristan Van Berkom <tristanvb@openismus.com>
 */

#if !defined (__LIBEBOOK_CONTACTS_H_INSIDE__) && !defined (LIBEBOOK_CONTACTS_COMPILATION)
#error "Only <libebook-contacts/libebook-contacts.h> should be included directly."
#endif

#ifndef E_SOURCE_DIRECT_ACCESS_H
#define E_SOURCE_DIRECT_ACCESS_H

#include <libedataserver/libedataserver.h>

/* Standard GObject macros */
#define E_TYPE_SOURCE_DIRECT_ACCESS \
	(e_source_direct_access_get_type ())
#define E_SOURCE_DIRECT_ACCESS(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_SOURCE_DIRECT_ACCESS, ESourceDirectAccess))
#define E_SOURCE_DIRECT_ACCESS_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), E_TYPE_SOURCE_DIRECT_ACCESS, ESourceDirectAccessClass))
#define E_IS_SOURCE_DIRECT_ACCESS(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_SOURCE_DIRECT_ACCESS))
#define E_IS_SOURCE_DIRECT_ACCESS_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), E_TYPE_SOURCE_DIRECT_ACCESS))
#define E_SOURCE_DIRECT_ACCESS_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), E_TYPE_SOURCE_DIRECT_ACCESS, ESourceDirectAccessClass))

/**
 * E_SOURCE_EXTENSION_DIRECT_ACCESS:
 *
 * Pass this extension name to e_source_get_extension() to access
 * #ESourceDirectAccess.  This is also used as a group name in key files.
 *
 * Since: 3.8
 **/
#define E_SOURCE_EXTENSION_DIRECT_ACCESS "Direct Access"

G_BEGIN_DECLS

typedef struct _ESourceDirectAccess ESourceDirectAccess;
typedef struct _ESourceDirectAccessClass ESourceDirectAccessClass;
typedef struct _ESourceDirectAccessPrivate ESourceDirectAccessPrivate;

/**
 * ESourceDirectAccess:
 *
 * Contains only private data that should be read and manipulated using the
 * functions below.
 *
 * Since: 3.6
 **/
struct _ESourceDirectAccess {
	ESourceBackend parent;
	ESourceDirectAccessPrivate *priv;
};

struct _ESourceDirectAccessClass {
	ESourceBackendClass parent_class;
};

GType           e_source_direct_access_get_type            (void) G_GNUC_CONST;

gchar          *e_source_direct_access_dup_backend_path    (ESourceDirectAccess  *extension);
void            e_source_direct_access_set_backend_path    (ESourceDirectAccess  *extension,
							    const gchar          *backend_path);

gchar          *e_source_direct_access_dup_backend_name    (ESourceDirectAccess  *extension);
void            e_source_direct_access_set_backend_name    (ESourceDirectAccess  *extension,
							    const gchar          *backend_name);


G_END_DECLS

#endif /* E_SOURCE_DIRECT_ACCESS_H */
