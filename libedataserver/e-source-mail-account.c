/*
 * e-source-mail-account.c
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
 * SECTION: e-source-mail-account
 * @include: libedataserver/libedataserver.h
 * @short_description: #ESource extension for an email account
 *
 * The #ESourceMailAccount extension identifies the #ESource as a
 * mail account and also links to a default "mail identity" to use.
 * See #ESourceMailIdentity for more information about identities.
 *
 * Access the extension as follows:
 *
 * |[
 *   #include <libedataserver/libedataserver.h>
 *
 *   ESourceMailAccount *extension;
 *
 *   extension = e_source_get_extension (source, E_SOURCE_EXTENSION_MAIL_ACCOUNT);
 * ]|
 **/

#include "e-source-mail-account.h"

#include <libedataserver/e-source-enumtypes.h>
#include <libedataserver/e-source-mail-identity.h>

#define E_SOURCE_MAIL_ACCOUNT_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), E_TYPE_SOURCE_MAIL_ACCOUNT, ESourceMailAccountPrivate))

struct _ESourceMailAccountPrivate {
	GMutex property_lock;
	gchar *identity_uid;
	gchar *archive_folder;
};

enum {
	PROP_0,
	PROP_IDENTITY_UID,
	PROP_ARCHIVE_FOLDER
};

G_DEFINE_TYPE (
	ESourceMailAccount,
	e_source_mail_account,
	E_TYPE_SOURCE_BACKEND)

static void
source_mail_account_set_property (GObject *object,
                                  guint property_id,
                                  const GValue *value,
                                  GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_IDENTITY_UID:
			e_source_mail_account_set_identity_uid (
				E_SOURCE_MAIL_ACCOUNT (object),
				g_value_get_string (value));
			return;

		case PROP_ARCHIVE_FOLDER:
			e_source_mail_account_set_archive_folder (
				E_SOURCE_MAIL_ACCOUNT (object),
				g_value_get_string (value));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
source_mail_account_get_property (GObject *object,
                                  guint property_id,
                                  GValue *value,
                                  GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_IDENTITY_UID:
			g_value_take_string (
				value,
				e_source_mail_account_dup_identity_uid (
				E_SOURCE_MAIL_ACCOUNT (object)));
			return;

		case PROP_ARCHIVE_FOLDER:
			g_value_take_string (
				value,
				e_source_mail_account_dup_archive_folder (
				E_SOURCE_MAIL_ACCOUNT (object)));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
source_mail_account_finalize (GObject *object)
{
	ESourceMailAccountPrivate *priv;

	priv = E_SOURCE_MAIL_ACCOUNT_GET_PRIVATE (object);

	g_mutex_clear (&priv->property_lock);

	g_free (priv->identity_uid);
	g_free (priv->archive_folder);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (e_source_mail_account_parent_class)->finalize (object);
}

static void
e_source_mail_account_class_init (ESourceMailAccountClass *class)
{
	GObjectClass *object_class;
	ESourceExtensionClass *extension_class;

	g_type_class_add_private (class, sizeof (ESourceMailAccountPrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->set_property = source_mail_account_set_property;
	object_class->get_property = source_mail_account_get_property;
	object_class->finalize = source_mail_account_finalize;

	extension_class = E_SOURCE_EXTENSION_CLASS (class);
	extension_class->name = E_SOURCE_EXTENSION_MAIL_ACCOUNT;

	g_object_class_install_property (
		object_class,
		PROP_IDENTITY_UID,
		g_param_spec_string (
			"identity-uid",
			"Identity UID",
			"ESource UID of a Mail Identity",
			"self",
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			G_PARAM_STATIC_STRINGS |
			E_SOURCE_PARAM_SETTING));

	g_object_class_install_property (
		object_class,
		PROP_ARCHIVE_FOLDER,
		g_param_spec_string (
			"archive-folder",
			"Archive Folder",
			"Folder to Archive messages in",
			"",
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			G_PARAM_STATIC_STRINGS |
			E_SOURCE_PARAM_SETTING));
}

static void
e_source_mail_account_init (ESourceMailAccount *extension)
{
	extension->priv = E_SOURCE_MAIL_ACCOUNT_GET_PRIVATE (extension);
	g_mutex_init (&extension->priv->property_lock);
}

/**
 * e_source_mail_account_get_identity_uid:
 * @extension: an #ESourceMailAccount
 *
 * Returns the #ESource:uid of the #ESource that describes the mail
 * identity to be used for this account.
 *
 * Returns: the mail identity #ESource:uid
 *
 * Since: 3.6
 **/
const gchar *
e_source_mail_account_get_identity_uid (ESourceMailAccount *extension)
{
	g_return_val_if_fail (E_IS_SOURCE_MAIL_ACCOUNT (extension), NULL);

	return extension->priv->identity_uid;
}

/**
 * e_source_mail_account_dup_identity_uid:
 * @extension: an #ESourceMailAccount
 *
 * Thread-safe variation of e_source_mail_account_get_identity_uid().
 * Use this function when accessing @extension from multiple threads.
 *
 * The returned string should be freed with g_free() when no longer needed.
 *
 * Returns: a newly-allocated copy of #ESourceMailAccount:identity-uid
 *
 * Since: 3.6
 **/
gchar *
e_source_mail_account_dup_identity_uid (ESourceMailAccount *extension)
{
	const gchar *protected;
	gchar *duplicate;

	g_return_val_if_fail (E_IS_SOURCE_MAIL_ACCOUNT (extension), NULL);

	g_mutex_lock (&extension->priv->property_lock);

	protected = e_source_mail_account_get_identity_uid (extension);
	duplicate = g_strdup (protected);

	g_mutex_unlock (&extension->priv->property_lock);

	return duplicate;
}

/**
 * e_source_mail_account_set_identity_uid:
 * @extension: an #ESourceMailAccount
 * @identity_uid: (allow-none): the mail identity #ESource:uid, or %NULL
 *
 * Sets the #ESource:uid of the #ESource that describes the mail
 * identity to be used for this account.
 *
 * Since: 3.6
 **/
void
e_source_mail_account_set_identity_uid (ESourceMailAccount *extension,
                                        const gchar *identity_uid)
{
	g_return_if_fail (E_IS_SOURCE_MAIL_ACCOUNT (extension));

	g_mutex_lock (&extension->priv->property_lock);

	if (g_strcmp0 (extension->priv->identity_uid, identity_uid) == 0) {
		g_mutex_unlock (&extension->priv->property_lock);
		return;
	}

	g_free (extension->priv->identity_uid);
	extension->priv->identity_uid = g_strdup (identity_uid);

	g_mutex_unlock (&extension->priv->property_lock);

	g_object_notify (G_OBJECT (extension), "identity-uid");
}

/**
 * e_source_mail_account_get_archive_folder:
 * @extension: an #ESourceMailAccount
 *
 * Returns a string identifying the archive folder.
 * The format of the identifier string is defined by the client application.
 *
 * Returns: an identifier of the archive folder
 *
 * Since: 3.14
 **/
const gchar *
e_source_mail_account_get_archive_folder (ESourceMailAccount *extension)
{
	g_return_val_if_fail (E_IS_SOURCE_MAIL_ACCOUNT (extension), NULL);

	return extension->priv->archive_folder;
}

/**
 * e_source_mail_account_dup_archive_folder:
 * @extension: an #ESourceMailAccount
 *
 * Thread-safe variation of e_source_mail_account_get_archive_folder().
 * Use this function when accessing @extension from multiple threads.
 *
 * The returned string should be freed with g_free() when no longer needed.
 *
 * Returns: a newly-allocated copy of #ESourceMailAccount:archive-folder
 *
 * Since: 3.14
 **/
gchar *
e_source_mail_account_dup_archive_folder (ESourceMailAccount *extension)
{
	const gchar *protected;
	gchar *duplicate;

	g_return_val_if_fail (E_IS_SOURCE_MAIL_ACCOUNT (extension), NULL);

	g_mutex_lock (&extension->priv->property_lock);

	protected = e_source_mail_account_get_archive_folder (extension);
	duplicate = g_strdup (protected);

	g_mutex_unlock (&extension->priv->property_lock);

	return duplicate;
}

/**
 * e_source_mail_account_set_archive_folder:
 * @extension: an #ESourceMailAccount
 * @archive_folder: (allow-none): an identifier for the archive folder, or %NULL
 *
 * Sets the folder for sent messages by an identifier string.
 * The format of the identifier string is defined by the client application.
 *
 * The internal copy of @archive_folder is automatically stripped of leading
 * and trailing whitespace. If the resulting string is empty, %NULL is set
 * instead.
 *
 * Since: 3.14
 **/
void
e_source_mail_account_set_archive_folder (ESourceMailAccount *extension,
					  const gchar *archive_folder)
{
	g_return_if_fail (E_IS_SOURCE_MAIL_ACCOUNT (extension));

	g_mutex_lock (&extension->priv->property_lock);

	if (g_strcmp0 (extension->priv->archive_folder, archive_folder) == 0) {
		g_mutex_unlock (&extension->priv->property_lock);
		return;
	}

	g_free (extension->priv->archive_folder);
	extension->priv->archive_folder = g_strdup (archive_folder);

	g_mutex_unlock (&extension->priv->property_lock);

	g_object_notify (G_OBJECT (extension), "archive-folder");
}
