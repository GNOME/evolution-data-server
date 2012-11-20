/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/* e-source-address-book-config.c - Address Book Configuration.
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

/**
 * SECTION: e-source-address-book-config
 * @include: libebook-contacts/libebook-contacts.h
 * @short_description: #ESource extension for an address book configuration
 *
 * The #ESourceAddressBookConfig extension adds configuration data to
 * an #ESource which is already defined as an #ESourceAddressBook.
 *
 * Access the extension as follows:
 *
 * |[
 *   #include <libebook-contacts/libebook-contacts.h>
 *
 *   ESourceAddressBookConfig *extension;
 *
 *   extension = e_source_get_extension (source, E_SOURCE_EXTENSION_ADDRESS_BOOK_CONFIG);
 * ]|
 **/

#include "e-source-address-book-config.h"
#include "e-book-contacts-enumtypes.h"

#define E_SOURCE_ABC_GET_PRIVATE(obj)			\
	(G_TYPE_INSTANCE_GET_PRIVATE			\
	 ((obj), E_TYPE_SOURCE_ADDRESS_BOOK_CONFIG,	\
	  ESourceAddressBookConfigPrivate))

struct _ESourceAddressBookConfigPrivate {
	GMutex *property_lock;
	gboolean revision_guards;
};

enum {
	PROP_0,
	PROP_REVISION_GUARDS
};

G_DEFINE_TYPE (
	ESourceAddressBookConfig,
	e_source_address_book_config,
	E_TYPE_SOURCE_EXTENSION)


static void
source_address_book_config_finalize (GObject *object)
{
	ESourceAddressBookConfigPrivate *priv;

	priv = E_SOURCE_ABC_GET_PRIVATE (object);

	g_mutex_free (priv->property_lock);

	G_OBJECT_CLASS (e_source_address_book_config_parent_class)->finalize (object);
}

static void
source_address_book_config_set_property (GObject *object,
					 guint property_id,
					 const GValue *value,
					 GParamSpec *pspec)
{
	ESourceAddressBookConfig *extension = E_SOURCE_ADDRESS_BOOK_CONFIG (object);

	switch (property_id) {
	case PROP_REVISION_GUARDS:
		e_source_address_book_config_set_revision_guards_enabled (extension, g_value_get_boolean (value));
		return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
source_address_book_config_get_property (GObject *object,
					 guint property_id,
					 GValue *value,
					 GParamSpec *pspec)
{
	ESourceAddressBookConfig *extension = E_SOURCE_ADDRESS_BOOK_CONFIG (object);

	switch (property_id) {
	case PROP_REVISION_GUARDS:
		g_value_set_boolean (value, e_source_address_book_config_get_revision_guards_enabled (extension));
		return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
e_source_address_book_config_class_init (ESourceAddressBookConfigClass *class)
{
	GObjectClass          *object_class;
	ESourceExtensionClass *extension_class;

	extension_class       = E_SOURCE_EXTENSION_CLASS (class);
	extension_class->name = E_SOURCE_EXTENSION_ADDRESS_BOOK_CONFIG;

	object_class               = G_OBJECT_CLASS (class);
	object_class->finalize     = source_address_book_config_finalize;
	object_class->get_property = source_address_book_config_get_property;
	object_class->set_property = source_address_book_config_set_property;

	g_object_class_install_property (
		object_class,
		PROP_REVISION_GUARDS,
		g_param_spec_boolean (
			"revision-guards",
			"Revision Guards",
			"Whether to enable or disable the revision guards",
			TRUE,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			E_SOURCE_PARAM_SETTING));

	g_type_class_add_private (class, sizeof (ESourceAddressBookConfigPrivate));
}

static void
e_source_address_book_config_init (ESourceAddressBookConfig *extension)
{
	extension->priv = E_SOURCE_ABC_GET_PRIVATE (extension);
	extension->priv->property_lock = g_mutex_new ();
	extension->priv->revision_guards = TRUE;
}

/**
 * e_source_address_book_config_set_revision_guards_enabled:
 * @extension: An #ESourceAddressBookConfig
 * @enabled: Whether to enable or disable the revision guards.
 *
 * Enables or disables the revision guards in the address book backend. If revision
 * guards are enabled, then contact modifications will be refused with the
 * error %E_DATA_BOOK_STATUS_BAD_REVISION if the modified contact revision is out of date.
 *
 * This avoids data loss when multiple processes write to the addressbook by forcing
 * the calling process to get an updated contact before committing it to the addressbook.
 *
 * Revision guards are enabled by default.
 *
 * Since: 3.8
 */
void
e_source_address_book_config_set_revision_guards_enabled (ESourceAddressBookConfig  *extension,
							  gboolean                   enabled)
{
	g_return_if_fail (E_IS_SOURCE_ADDRESS_BOOK_CONFIG (extension));

	g_mutex_lock (extension->priv->property_lock);

	if (extension->priv->revision_guards == enabled) {
		g_mutex_unlock (extension->priv->property_lock);
		return;
	}

	extension->priv->revision_guards = enabled;

	g_mutex_unlock (extension->priv->property_lock);

	g_object_notify (G_OBJECT (extension), "revision-guards");
}


/**
 * e_source_address_book_config_get_revision_guards_enabled:
 * @extension: An #ESourceAddressBookConfig
 *
 * Checks whether revision guards in the address book backend are enabled.
 *
 * Returns: %TRUE if the revision guards are enabled.
 *
 * Since: 3.8
 */
gboolean
e_source_address_book_config_get_revision_guards_enabled (ESourceAddressBookConfig *extension)
{
	gboolean enabled;

	g_return_val_if_fail (E_IS_SOURCE_ADDRESS_BOOK_CONFIG (extension), FALSE);

	g_mutex_lock (extension->priv->property_lock);
	enabled = extension->priv->revision_guards;
	g_mutex_unlock (extension->priv->property_lock);

	return enabled;
}
