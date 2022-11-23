/*
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

#include "evolution-data-server-config.h"

#include <glib/gi18n-lib.h>

#include "libedataserver/libedataserver.h"

#include "e-book-contacts-utils.h"

G_DEFINE_QUARK (e-book-client-error-quark, e_book_client_error)

/**
 * e_book_client_error_to_string:
 * @code: an #EBookClientError code
 *
 * Get localized human readable description of the given error code.
 *
 * Returns: Localized human readable description of the given error code
 *
 * Since: 3.2
 **/
const gchar *
e_book_client_error_to_string (EBookClientError code)
{
	switch (code) {
	case E_BOOK_CLIENT_ERROR_NO_SUCH_BOOK:
		return _("No such book");
	case E_BOOK_CLIENT_ERROR_CONTACT_NOT_FOUND:
		return _("Contact not found");
	case E_BOOK_CLIENT_ERROR_CONTACT_ID_ALREADY_EXISTS:
		return _("Contact ID already exists");
	case E_BOOK_CLIENT_ERROR_NO_SUCH_SOURCE:
		return _("No such source");
	case E_BOOK_CLIENT_ERROR_NO_SPACE:
		return _("No space");
	}

	return _("Unknown error");
}

/**
 * e_book_client_error_create:
 * @code: an #EBookClientError code to create
 * @custom_msg: (nullable): custom message to use for the error; can be %NULL
 *
 * Returns: a new #GError containing an E_BOOK_CLIENT_ERROR of the given
 * @code. If the @custom_msg is NULL, then the error message is
 * the one returned from e_book_client_error_to_string() for the @code,
 * otherwise the given message is used.
 *
 * Returned pointer should be freed with g_error_free().
 *
 * Since: 3.2
 **/
GError *
e_book_client_error_create (EBookClientError code,
                            const gchar *custom_msg)
{
	return g_error_new_literal (E_BOOK_CLIENT_ERROR, code, custom_msg ? custom_msg : e_book_client_error_to_string (code));
}

/**
 * e_book_client_error_create_fmt:
 * @code: an #EBookClientError
 * @format: (nullable): message format, or %NULL to use the default message for the @code
 * @...: arguments for the format
 *
 * Similar as e_book_client_error_create(), only here, instead of custom_msg,
 * is used a printf() format to create a custom message for the error.
 *
 * Returns: (transfer full): a newly allocated #GError, which should be
 *   freed with g_error_free(), when no longer needed.
 *   The #GError has set the custom message, or the default message for
 *   @code, when @format is %NULL.
 *
 * Since: 3.34
 **/
GError *
e_book_client_error_create_fmt (EBookClientError code,
				const gchar *format,
				...)
{
	GError *error;
	gchar *custom_msg;
	va_list ap;

	if (!format)
		return e_book_client_error_create (code, NULL);

	va_start (ap, format);
	custom_msg = g_strdup_vprintf (format, ap);
	va_end (ap);

	error = e_book_client_error_create (code, custom_msg);

	g_free (custom_msg);

	return error;
}

/**
 * e_book_util_operation_flags_to_conflict_resolution:
 * @flags: bit-or of #EBookOperationFlags
 *
 * Decodes the #EConflictResolution from the bit-or of #EBookOperationFlags.
 *
 * Returns: an #EConflictResolution as stored in the @flags
 *
 * Since: 3.34
 **/
EConflictResolution
e_book_util_operation_flags_to_conflict_resolution (guint32 flags)
{
	if ((flags & E_BOOK_OPERATION_FLAG_CONFLICT_FAIL) != 0)
		return E_CONFLICT_RESOLUTION_FAIL;
	else if ((flags & E_BOOK_OPERATION_FLAG_CONFLICT_USE_NEWER) != 0)
		return E_CONFLICT_RESOLUTION_USE_NEWER;
	else if ((flags & E_BOOK_OPERATION_FLAG_CONFLICT_KEEP_SERVER) != 0)
		return E_CONFLICT_RESOLUTION_KEEP_SERVER;
	else if ((flags & E_BOOK_OPERATION_FLAG_CONFLICT_WRITE_COPY) != 0)
		return E_CONFLICT_RESOLUTION_WRITE_COPY;

	/* E_BOOK_OPERATION_FLAG_CONFLICT_KEEP_LOCAL is the default */
	return E_CONFLICT_RESOLUTION_KEEP_LOCAL;
}

/**
 * e_book_util_conflict_resolution_to_operation_flags:
 * @conflict_resolution: an #EConflictResolution
 *
 * Encodes the #EConflictResolution into the bit-or of #EBookOperationFlags.
 * The returned value can be bit-or-ed with other #EBookOperationFlags values.
 *
 * Returns: a bit-or of #EBookOperationFlags, corresponding to the @conflict_resolution
 *
 * Since: 3.34
 **/
guint32
e_book_util_conflict_resolution_to_operation_flags (EConflictResolution conflict_resolution)
{
	switch (conflict_resolution) {
	case E_CONFLICT_RESOLUTION_FAIL:
		return E_BOOK_OPERATION_FLAG_CONFLICT_FAIL;
	case E_CONFLICT_RESOLUTION_USE_NEWER:
		return E_BOOK_OPERATION_FLAG_CONFLICT_USE_NEWER;
	case E_CONFLICT_RESOLUTION_KEEP_SERVER:
		return E_BOOK_OPERATION_FLAG_CONFLICT_KEEP_SERVER;
	case E_CONFLICT_RESOLUTION_KEEP_LOCAL:
		return E_BOOK_OPERATION_FLAG_CONFLICT_KEEP_LOCAL;
	case E_CONFLICT_RESOLUTION_WRITE_COPY:
		return E_BOOK_OPERATION_FLAG_CONFLICT_WRITE_COPY;
	}

	return E_BOOK_OPERATION_FLAG_CONFLICT_KEEP_LOCAL;
}

/**
 * e_book_util_foreach_address:
 * @email_address: one or more email addresses as string
 * @func: (scope call): a function to call for each email
 * @user_data (closure func): user data passed to @func
 *
 * Parses the @email_address and calls @func for each found address.
 * The first parameter of the @func is the name, the second parameter
 * of the @func is the email, the third parameters of the @func is
 * the @user_data. The @func returns %TRUE, to continue processing.
 *
 * Since: 3.44
 **/
void
e_book_util_foreach_address (const gchar *email_address,
			     GHRFunc func,
			     gpointer user_data)
{
	CamelInternetAddress *address;
	const gchar *name, *email;
	gint index;

	g_return_if_fail (func != NULL);

	if (!email_address || !*email_address)
		return;

	address = camel_internet_address_new ();
	if (!camel_address_decode (CAMEL_ADDRESS (address), email_address)) {
		g_object_unref (address);
		return;
	}

	for (index = 0; camel_internet_address_get (address, index, &name, &email); index++) {
		if (!func ((gpointer) name, (gpointer) email, user_data))
			break;
	}

	g_object_unref (address);
}

static GHashTable *
e_book_util_extract_categories (EContact *contact)
{
	GHashTable *categories_hash = NULL;
	gchar *categories_str;

	if (!contact)
		return NULL;

	g_return_val_if_fail (E_IS_CONTACT (contact), NULL);

	categories_str = e_contact_get (contact, E_CONTACT_CATEGORIES);

	if (categories_str && *categories_str) {
		gchar **categories_strv;
		guint ii;

		categories_strv = g_strsplit (categories_str, ",", -1);

		for (ii = 0; categories_strv && categories_strv[ii]; ii++) {
			gchar *category = g_strstrip (categories_strv[ii]);

			if (*category) {
				if (!categories_hash)
					categories_hash = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

				g_hash_table_insert (categories_hash, g_strdup (category), GINT_TO_POINTER (1));
			}
		}

		g_strfreev (categories_strv);
	}

	g_free (categories_str);

	return categories_hash;
}

static gboolean
e_book_util_remove_matching_category_cb (gpointer key,
					 gpointer value,
					 gpointer user_data)
{
	GHashTable *other_table = user_data;

	/* Remove from both tables those common */
	return g_hash_table_remove (other_table, key);
}

/**
 * e_book_util_diff_categories:
 * @old_contact: (nullable): an old #EContact, or %NULL
 * @new_contact: (nullable): a new #EContact, or %NULL
 * @out_added: (out) (transfer container) (element-type utf8 int): a #GHashTable with added categories
 * @out_removed: (out) (transfer container) (element-type utf8 int): a #GHashTable with removed categories
 *
 * Compares list of categories on the @old_contact with the list of categories
 * on the @new_contact and fills @out_added categories and @out_removed categories
 * accordingly, as if the @old_contact is replaced with the @new_contact. When either
 * of the contacts is %NULL, it's considered as having no categories set.
 * Rather than returning empty #GHashTable, the return argument is set to %NULL
 * when there are no added/removed categories.
 *
 * The key of the hash table is the category string, the value is an integer (1).
 * There is used the hash table only for speed.
 *
 * The returned #GHashTable-s should be freed with g_hash_table_unref(),
 * when no longer needed.
 *
 * Since: 3.48
 **/
void
e_book_util_diff_categories (EContact *old_contact,
			     EContact *new_contact,
			     GHashTable **out_added, /* const gchar *category ~> 1 */
			     GHashTable **out_removed) /* const gchar *category ~> 1 */
{
	if (old_contact)
		g_return_if_fail (E_IS_CONTACT (old_contact));
	if (new_contact)
		g_return_if_fail (E_IS_CONTACT (new_contact));
	g_return_if_fail (out_added != NULL);
	g_return_if_fail (out_removed != NULL);

	*out_added = e_book_util_extract_categories (new_contact);
	*out_removed = e_book_util_extract_categories (old_contact);

	if (*out_added && *out_removed) {
		g_hash_table_foreach_remove (*out_added, e_book_util_remove_matching_category_cb, *out_removed);

		if (!g_hash_table_size (*out_added)) {
			g_hash_table_unref (*out_added);
			*out_added = NULL;
		}

		if (!g_hash_table_size (*out_removed)) {
			g_hash_table_unref (*out_removed);
			*out_removed = NULL;
		}
	}
}
