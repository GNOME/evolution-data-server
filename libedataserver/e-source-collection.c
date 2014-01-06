/*
 * e-source-collection.c
 *
 * This library is free software you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library; if not, see <http://www.gnu.org/licenses/>.
 *
 */

/**
 * SECTION: e-source-collection
 * @include: libedataserver/libedataserver.h
 * @short_description: #ESource extension for grouping related resources
 *
 * The #ESourceCollection extension identifies the #ESource as the root
 * of a data source collection.
 *
 * Access the extension as follows:
 *
 * |[
 *    #include <libedataserver/libedataserver.h>
 *
 *    ESourceCollection *extension;
 *
 *    extension = e_source_get_extension (source, E_SOURCE_EXTENSION_COLLECTION);
 * ]|
 **/

#include "e-source-collection.h"

#include <config.h>
#include <glib/gi18n-lib.h>

/* Private D-Bus classes. */
#include <e-dbus-source.h>

#include <libedataserver/e-data-server-util.h>

#define E_SOURCE_COLLECTION_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), E_TYPE_SOURCE_COLLECTION, ESourceCollectionPrivate))

struct _ESourceCollectionPrivate {
	GMutex property_lock;
	gchar *identity;
	gboolean calendar_enabled;
	gboolean contacts_enabled;
	gboolean mail_enabled;
};

enum {
	PROP_0,
	PROP_CALENDAR_ENABLED,
	PROP_CONTACTS_ENABLED,
	PROP_IDENTITY,
	PROP_MAIL_ENABLED
};

G_DEFINE_TYPE (
	ESourceCollection,
	e_source_collection,
	E_TYPE_SOURCE_BACKEND)

static void
source_collection_set_property (GObject *object,
                                guint property_id,
                                const GValue *value,
                                GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_CALENDAR_ENABLED:
			e_source_collection_set_calendar_enabled (
				E_SOURCE_COLLECTION (object),
				g_value_get_boolean (value));
			return;

		case PROP_CONTACTS_ENABLED:
			e_source_collection_set_contacts_enabled (
				E_SOURCE_COLLECTION (object),
				g_value_get_boolean (value));
			return;

		case PROP_IDENTITY:
			e_source_collection_set_identity (
				E_SOURCE_COLLECTION (object),
				g_value_get_string (value));
			return;

		case PROP_MAIL_ENABLED:
			e_source_collection_set_mail_enabled (
				E_SOURCE_COLLECTION (object),
				g_value_get_boolean (value));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
source_collection_get_property (GObject *object,
                                guint property_id,
                                GValue *value,
                                GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_CALENDAR_ENABLED:
			g_value_set_boolean (
				value,
				e_source_collection_get_calendar_enabled (
				E_SOURCE_COLLECTION (object)));
			return;

		case PROP_CONTACTS_ENABLED:
			g_value_set_boolean (
				value,
				e_source_collection_get_contacts_enabled (
				E_SOURCE_COLLECTION (object)));
			return;

		case PROP_IDENTITY:
			g_value_take_string (
				value,
				e_source_collection_dup_identity (
				E_SOURCE_COLLECTION (object)));
			return;

		case PROP_MAIL_ENABLED:
			g_value_set_boolean (
				value,
				e_source_collection_get_mail_enabled (
				E_SOURCE_COLLECTION (object)));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
source_collection_finalize (GObject *object)
{
	ESourceCollectionPrivate *priv;

	priv = E_SOURCE_COLLECTION_GET_PRIVATE (object);

	g_mutex_clear (&priv->property_lock);

	g_free (priv->identity);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (e_source_collection_parent_class)->finalize (object);
}

static void
e_source_collection_class_init (ESourceCollectionClass *class)
{
	GObjectClass *object_class;
	ESourceExtensionClass *extension_class;

	g_type_class_add_private (class, sizeof (ESourceCollectionPrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->set_property = source_collection_set_property;
	object_class->get_property = source_collection_get_property;
	object_class->finalize = source_collection_finalize;

	extension_class = E_SOURCE_EXTENSION_CLASS (class);
	extension_class->name = E_SOURCE_EXTENSION_COLLECTION;

	g_object_class_install_property (
		object_class,
		PROP_CALENDAR_ENABLED,
		g_param_spec_boolean (
			"calendar-enabled",
			"Calendar Enabled",
			"Whether calendar resources are enabled",
			TRUE,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			G_PARAM_STATIC_STRINGS |
			E_SOURCE_PARAM_SETTING));

	g_object_class_install_property (
		object_class,
		PROP_CONTACTS_ENABLED,
		g_param_spec_boolean (
			"contacts-enabled",
			"Contacts Enabled",
			"Whether contact resources are enabled",
			TRUE,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			G_PARAM_STATIC_STRINGS |
			E_SOURCE_PARAM_SETTING));

	g_object_class_install_property (
		object_class,
		PROP_IDENTITY,
		g_param_spec_string (
			"identity",
			"Identity",
			"Uniquely identifies the account "
			"at the service provider",
			NULL,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			G_PARAM_STATIC_STRINGS |
			E_SOURCE_PARAM_SETTING));

	g_object_class_install_property (
		object_class,
		PROP_MAIL_ENABLED,
		g_param_spec_boolean (
			"mail-enabled",
			"Mail Enabled",
			"Whether mail resources are enabled",
			TRUE,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			G_PARAM_STATIC_STRINGS |
			E_SOURCE_PARAM_SETTING));
}

static void
e_source_collection_init (ESourceCollection *extension)
{
	extension->priv = E_SOURCE_COLLECTION_GET_PRIVATE (extension);
	g_mutex_init (&extension->priv->property_lock);
}

/**
 * e_source_collection_get_identity:
 * @extension: an #ESourceCollection
 *
 * Returns the string used to uniquely identify the user account at
 * the service provider.  Often this is an email address or user name.
 *
 * Returns: the collection identity
 *
 * Since: 3.6
 **/
const gchar *
e_source_collection_get_identity (ESourceCollection *extension)
{
	g_return_val_if_fail (E_IS_SOURCE_COLLECTION (extension), NULL);

	return extension->priv->identity;
}

/**
 * e_source_collection_dup_identity:
 * @extension: an #ESourceCollection
 *
 * Thread-safe variation of e_source_collection_get_identity().
 * Use this function when accessing @extension from multiple threads.
 *
 * The returned string should be freed with g_free() when no longer needed.
 *
 * Returns: a newly-allocated copy of #ESourceCollection:identity
 *
 * Since: 3.6
 **/
gchar *
e_source_collection_dup_identity (ESourceCollection *extension)
{
	const gchar *protected;
	gchar *duplicate;

	g_return_val_if_fail (E_IS_SOURCE_COLLECTION (extension), NULL);

	g_mutex_lock (&extension->priv->property_lock);

	protected = e_source_collection_get_identity (extension);
	duplicate = g_strdup (protected);

	g_mutex_unlock (&extension->priv->property_lock);

	return duplicate;
}

/**
 * e_source_collection_set_identity:
 * @extension: an #ESourceCollection
 * @identity: (allow-none): the collection identity, or %NULL
 *
 * Sets the string used to uniquely identify the user account at the
 * service provider.  Often this is an email address or user name.
 *
 * The internal copy of @identity is automatically stripped of leading
 * and trailing whitespace.  If the resulting string is empty, %NULL is
 * set instead.
 *
 * Since: 3.6
 **/
void
e_source_collection_set_identity (ESourceCollection *extension,
                                  const gchar *identity)
{
	g_return_if_fail (E_IS_SOURCE_COLLECTION (extension));

	g_mutex_lock (&extension->priv->property_lock);

	if (g_strcmp0 (extension->priv->identity, identity) == 0) {
		g_mutex_unlock (&extension->priv->property_lock);
		return;
	}

	g_free (extension->priv->identity);
	extension->priv->identity = e_util_strdup_strip (identity);

	g_mutex_unlock (&extension->priv->property_lock);

	g_object_notify (G_OBJECT (extension), "identity");
}

/**
 * e_source_collection_get_calendar_enabled:
 * @extension: an #ESourceCollection
 *
 * Returns whether calendar sources within the collection should be
 * enabled.
 *
 * An #ECollectionBackend running within the registry D-Bus service will
 * automatically synchronize any calendar sources it maintains with the
 * #ESourceCollection:calendar-enabled property.
 *
 * Returns: whether calendar sources should be enabled
 *
 * Since: 3.6
 **/
gboolean
e_source_collection_get_calendar_enabled (ESourceCollection *extension)
{
	g_return_val_if_fail (E_IS_SOURCE_COLLECTION (extension), FALSE);

	return extension->priv->calendar_enabled;
}

/**
 * e_source_collection_set_calendar_enabled:
 * @extension: an #ESourceCollection
 * @calendar_enabled: whether calendar sources should be enabled
 *
 * Sets whether calendar sources within the collection should be enabled.
 *
 * An #ECollectionBackend running within the registry D-Bus service will
 * automatically synchronize any calendar sources it maintains with the
 * #ESourceCollection:calendar-enabled property.
 *
 * Calling this function from a registry service client has no effect until
 * the change is submitted to the registry service through e_source_write(),
 * but there should rarely be any need for clients to call this.
 *
 * Since: 3.6
 **/
void
e_source_collection_set_calendar_enabled (ESourceCollection *extension,
                                          gboolean calendar_enabled)
{
	g_return_if_fail (E_IS_SOURCE_COLLECTION (extension));

	if (extension->priv->calendar_enabled == calendar_enabled)
		return;

	extension->priv->calendar_enabled = calendar_enabled;

	g_object_notify (G_OBJECT (extension), "calendar-enabled");
}

/**
 * e_source_collection_get_contacts_enabled:
 * @extension: an #ESourceCollection
 *
 * Returns whether address book sources within the collection should be
 * enabled.
 *
 * An #ECollectionBackend running within the registry D-Bus service will
 * automatically synchronize any address book sources it maintains with
 * the #ESourceCollection:contacts-enabled property.
 *
 * Returns: whether address book sources should be enabled
 *
 * Since: 3.6
 **/
gboolean
e_source_collection_get_contacts_enabled (ESourceCollection *extension)
{
	g_return_val_if_fail (E_IS_SOURCE_COLLECTION (extension), FALSE);

	return extension->priv->contacts_enabled;
}

/**
 * e_source_collection_set_contacts_enabled:
 * @extension: an #ESourceCollection
 * @contacts_enabled: whether address book sources should be enabled
 *
 * Sets whether address book sources within the collection should be enabled.
 *
 * An #ECollectionBackend running within the registry D-Bus service will
 * automatically synchronize any address book sources it maintains with
 * the #ESourceCollection:contacts-enabled property.
 *
 * Calling this function from a registry service client has no effect until
 * the change is submitted to the registry service through e_source_write(),
 * but there should rarely be any need for clients to call this.
 *
 * Since: 3.6
 **/
void
e_source_collection_set_contacts_enabled (ESourceCollection *extension,
                                          gboolean contacts_enabled)
{
	g_return_if_fail (E_IS_SOURCE_COLLECTION (extension));

	if (extension->priv->contacts_enabled == contacts_enabled)
		return;

	extension->priv->contacts_enabled = contacts_enabled;

	g_object_notify (G_OBJECT (extension), "contacts-enabled");
}

/**
 * e_source_collection_get_mail_enabled:
 * @extension: an #ESourceCollection
 *
 * Returns whether mail sources within the collection should be enabled.
 *
 * An #ECollectionBackend running within the registry D-Bus service will
 * automatically synchronize any mail sources it maintains with the
 * #ESourceCollection:mail-enabled property.
 *
 * Returns: whether mail sources should be enabled
 *
 * Since: 3.6
 **/
gboolean
e_source_collection_get_mail_enabled (ESourceCollection *extension)
{
	g_return_val_if_fail (E_IS_SOURCE_COLLECTION (extension), FALSE);

	return extension->priv->mail_enabled;
}

/**
 * e_source_collection_set_mail_enabled:
 * @extension: an #ESourceCollection
 * @mail_enabled: whether mail sources should be enabled
 *
 * Sets whether mail sources within the collection should be enabled.
 *
 * An #ECollectionBackend running within the registry D-Bus service will
 * automatically synchronize any mail sources it maintains with the
 * #ESourceCollection:mail-enabled property.
 *
 * Calling this function from a registry service client has no effect until
 * the changes is submitted to the registry service through e_source_write(),
 * but there should rarely be any need for clients to call this.
 *
 * Since: 3.6
 **/
void
e_source_collection_set_mail_enabled (ESourceCollection *extension,
                                      gboolean mail_enabled)
{
	g_return_if_fail (E_IS_SOURCE_COLLECTION (extension));

	if (extension->priv->mail_enabled == mail_enabled)
		return;

	extension->priv->mail_enabled = mail_enabled;

	g_object_notify (G_OBJECT (extension), "mail-enabled");
}

