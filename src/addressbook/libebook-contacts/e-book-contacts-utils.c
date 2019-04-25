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
 * @custom_msg: custom message to use for the error; can be %NULL
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
