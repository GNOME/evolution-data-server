/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/* e-source-address-book-config.h - Address Book Configuration.
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

#if !defined (__LIBEBOOK_H_INSIDE__) && !defined (LIBEBOOK_COMPILATION)
#error "Only <libebook/libebook.h> should be included directly."
#endif

#ifndef E_SOURCE_ADDRESS_BOOK_CONFIG_H
#define E_SOURCE_ADDRESS_BOOK_CONFIG_H

#include <libedataserver/libedataserver.h>
#include <libebook/e-contact.h>
#include <libebook/e-book-types.h>

/* Standard GObject macros */
#define E_TYPE_SOURCE_ADDRESS_BOOK_CONFIG \
	(e_source_address_book_config_get_type ())
#define E_SOURCE_ADDRESS_BOOK_CONFIG(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_SOURCE_ADDRESS_BOOK_CONFIG, ESourceAddressBookConfig))
#define E_SOURCE_ADDRESS_BOOK_CONFIG_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), E_TYPE_SOURCE_ADDRESS_BOOK_CONFIG, ESourceAddressBookConfigClass))
#define E_IS_SOURCE_ADDRESS_BOOK_CONFIG(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_SOURCE_ADDRESS_BOOK_CONFIG))
#define E_IS_SOURCE_ADDRESS_BOOK_CONFIG_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), E_TYPE_SOURCE_ADDRESS_BOOK_CONFIG))
#define E_SOURCE_ADDRESS_BOOK_CONFIG_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), E_TYPE_SOURCE_ADDRESS_BOOK_CONFIG, ESourceAddressBookConfigClass))

/**
 * E_SOURCE_EXTENSION_ADDRESS_BOOK_CONFIG:
 *
 * Pass this extension name to e_source_get_extension() to access
 * #ESourceAddressBookConfig.  This is also used as a group name in key files.
 *
 * Since: 3.8
 **/
#define E_SOURCE_EXTENSION_ADDRESS_BOOK_CONFIG "Address Book Configuration"

G_BEGIN_DECLS

typedef struct _ESourceAddressBookConfig ESourceAddressBookConfig;
typedef struct _ESourceAddressBookConfigClass ESourceAddressBookConfigClass;
typedef struct _ESourceAddressBookConfigPrivate ESourceAddressBookConfigPrivate;

/**
 * ESourceAddressBookConfig:
 *
 * Contains only private data that should be read and manipulated using the
 * functions below.
 *
 * Since: 3.6
 **/
struct _ESourceAddressBookConfig {
	ESourceBackend parent;
	ESourceAddressBookConfigPrivate *priv;
};

struct _ESourceAddressBookConfigClass {
	ESourceBackendClass parent_class;
};

GType           e_source_address_book_config_get_type            (void) G_GNUC_CONST;

EContactField  *e_source_address_book_config_get_summary_fields  (ESourceAddressBookConfig  *extension,
								  gint                       *n_fields);
void            e_source_address_book_config_set_summary_fieldsv (ESourceAddressBookConfig  *extension,
								  EContactField             *fields,
								  gint                       n_fields);
void            e_source_address_book_config_set_summary_fields  (ESourceAddressBookConfig  *extension,
								  ...);

EContactField  *e_source_address_book_config_get_indexed_fields  (ESourceAddressBookConfig  *extension,
								  EBookIndexType           **types,
								  gint                      *n_fields);
void            e_source_address_book_config_set_indexed_fieldsv (ESourceAddressBookConfig  *extension,
								  EContactField             *fields,
								  EBookIndexType            *types,
								  gint                       n_fields);
void            e_source_address_book_config_set_indexed_fields  (ESourceAddressBookConfig  *extension,
								  ...);

void            e_source_address_book_config_set_revision_guards_enabled  (ESourceAddressBookConfig  *extension,
									   gboolean                   enabled);
gboolean        e_source_address_book_config_get_revision_guards_enabled  (ESourceAddressBookConfig  *extension);


G_END_DECLS

#endif /* E_SOURCE_ADDRESS_BOOK_CONFIG_H */
