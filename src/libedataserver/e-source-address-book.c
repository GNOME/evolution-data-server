/*
 * e-source-address-book.c
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

/**
 * SECTION: e-source-address-book
 * @include: libedataserver/libedataserver.h
 * @short_description: #ESource extension for an address book
 *
 * The #ESourceAddressBook extension identifies the #ESource as an
 * address book.
 *
 * Access the extension as follows:
 *
 * |[
 *   #include <libedataserver/libedataserver.h>
 *
 *   ESourceAddressBook *extension;
 *
 *   extension = e_source_get_extension (source, E_SOURCE_EXTENSION_ADDRESS_BOOK);
 * ]|
 **/

#include "e-source-address-book.h"

#include <libedataserver/e-data-server-util.h>

struct _ESourceAddressBookPrivate {
	guint order;
};

enum {
	PROP_0,
	PROP_ORDER,
	N_PROPS
};

static GParamSpec *properties[N_PROPS] = { NULL, };

G_DEFINE_TYPE_WITH_PRIVATE (ESourceAddressBook, e_source_address_book, E_TYPE_SOURCE_BACKEND)

static void
source_address_book_set_property (GObject *object,
				  guint property_id,
				  const GValue *value,
				  GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_ORDER:
			e_source_address_book_set_order (
				E_SOURCE_ADDRESS_BOOK (object),
				g_value_get_uint (value));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
source_address_book_get_property (GObject *object,
				  guint property_id,
				  GValue *value,
				  GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_ORDER:
			g_value_set_uint (
				value,
				e_source_address_book_get_order (
				E_SOURCE_ADDRESS_BOOK (object)));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
e_source_address_book_class_init (ESourceAddressBookClass *class)
{
	GObjectClass *object_class;
	ESourceExtensionClass *extension_class;

	object_class = G_OBJECT_CLASS (class);
	object_class->set_property = source_address_book_set_property;
	object_class->get_property = source_address_book_get_property;

	extension_class = E_SOURCE_EXTENSION_CLASS (class);
	extension_class->name = E_SOURCE_EXTENSION_ADDRESS_BOOK;

	/**
	 * ESourceAddressBook:order
	 *
	 * A sorting order of the source
	 **/
	properties[PROP_ORDER] =
		g_param_spec_uint (
			"order",
			NULL, NULL,
			0, G_MAXUINT, 0,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			G_PARAM_EXPLICIT_NOTIFY |
			G_PARAM_STATIC_STRINGS |
			E_SOURCE_PARAM_SETTING);

	g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
e_source_address_book_init (ESourceAddressBook *extension)
{
	extension->priv = e_source_address_book_get_instance_private (extension);
}

/**
 * e_source_address_book_get_order:
 * @extension: an #ESourceAddressBook
 *
 * Returns: the sorting order of the source, if known. Zero is the default.
 *
 * Since: 3.40
 **/
guint
e_source_address_book_get_order (ESourceAddressBook *extension)
{
	g_return_val_if_fail (E_IS_SOURCE_ADDRESS_BOOK (extension), 0);

	return extension->priv->order;
}

/**
 * e_source_address_book_set_order:
 * @extension: an #ESourceAddressBook
 * @order: a sorting order
 *
 * Set the sorting order of the source.
 *
 * Since: 3.40
 **/
void
e_source_address_book_set_order (ESourceAddressBook *extension,
				 guint order)
{
	g_return_if_fail (E_IS_SOURCE_ADDRESS_BOOK (extension));

	if (extension->priv->order == order)
		return;

	extension->priv->order = order;

	g_object_notify_by_pspec (G_OBJECT (extension), properties[PROP_ORDER]);
}
