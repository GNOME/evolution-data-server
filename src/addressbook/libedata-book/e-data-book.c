/*
 * e-data-book.c
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
 * SECTION: e-data-book
 * @include: libedata-book/libedata-book.h
 * @short_description: Server side D-Bus layer to communicate with addressbooks
 *
 * This class communicates with #EBookClients over the bus and accesses
 * an #EBookBackend to satisfy client requests.
 **/

#include "evolution-data-server-config.h"

#include <locale.h>
#include <unistd.h>
#include <stdlib.h>
#include <glib/gi18n.h>
#include <gio/gio.h>

/* Private D-Bus classes. */
#include <e-dbus-address-book.h>

#include <libebook-contacts/libebook-contacts.h>

#include "e-data-book-factory.h"
#include "e-data-book.h"
#include "e-data-book-view.h"
#include "e-book-backend.h"
#include "e-book-backend-sexp.h"
#include "e-book-backend-factory.h"

typedef struct _AsyncContext AsyncContext;

struct _EDataBookPrivate {
	GDBusConnection *connection;
	EDBusAddressBook *dbus_interface;
	EModule *direct_module;
	EDataBookDirect *direct_book;

	GWeakRef backend;
	gchar *object_path;

	GMutex sender_lock;
	GHashTable *sender_table;
};

struct _AsyncContext {
	EDataBook *data_book;
	EDBusAddressBook *dbus_interface;
	GDBusMethodInvocation *invocation;
	GCancellable *cancellable;
	guint watcher_id;
};

enum {
	PROP_0,
	PROP_BACKEND,
	PROP_CONNECTION,
	PROP_OBJECT_PATH
};

/* Forward Declarations */
static void e_data_book_initable_init (GInitableIface *iface);

G_DEFINE_TYPE_WITH_CODE (
	EDataBook,
	e_data_book,
	G_TYPE_OBJECT,
	G_ADD_PRIVATE (EDataBook)
	G_IMPLEMENT_INTERFACE (
		G_TYPE_INITABLE,
		e_data_book_initable_init))

static void
sender_vanished_cb (GDBusConnection *connection,
                    const gchar *sender,
                    GCancellable *cancellable)
{
	g_cancellable_cancel (cancellable);
}

static void
sender_table_insert (EDataBook *data_book,
                     const gchar *sender,
                     GCancellable *cancellable)
{
	GHashTable *sender_table;
	GPtrArray *array;

	g_return_if_fail (sender != NULL);

	g_mutex_lock (&data_book->priv->sender_lock);

	sender_table = data_book->priv->sender_table;
	array = g_hash_table_lookup (sender_table, sender);

	if (array == NULL) {
		array = g_ptr_array_new_with_free_func (
			(GDestroyNotify) g_object_unref);
		g_hash_table_insert (
			sender_table, g_strdup (sender), array);
	}

	g_ptr_array_add (array, g_object_ref (cancellable));

	g_mutex_unlock (&data_book->priv->sender_lock);
}

static gboolean
sender_table_remove (EDataBook *data_book,
                     const gchar *sender,
                     GCancellable *cancellable)
{
	GHashTable *sender_table;
	GPtrArray *array;
	gboolean removed = FALSE;

	g_return_val_if_fail (sender != NULL, FALSE);

	g_mutex_lock (&data_book->priv->sender_lock);

	sender_table = data_book->priv->sender_table;
	array = g_hash_table_lookup (sender_table, sender);

	if (array != NULL) {
		removed = g_ptr_array_remove_fast (array, cancellable);

		if (array->len == 0)
			g_hash_table_remove (sender_table, sender);
	}

	g_mutex_unlock (&data_book->priv->sender_lock);

	return removed;
}

static AsyncContext *
async_context_new (EDataBook *data_book,
                   GDBusMethodInvocation *invocation)
{
	AsyncContext *async_context;
	EDBusAddressBook *dbus_interface;

	dbus_interface = data_book->priv->dbus_interface;

	async_context = g_slice_new0 (AsyncContext);
	async_context->data_book = g_object_ref (data_book);
	async_context->dbus_interface = g_object_ref (dbus_interface);
	async_context->invocation = g_object_ref (invocation);
	async_context->cancellable = g_cancellable_new ();

	async_context->watcher_id = g_bus_watch_name_on_connection (
		g_dbus_method_invocation_get_connection (invocation),
		g_dbus_method_invocation_get_sender (invocation),
		G_BUS_NAME_WATCHER_FLAGS_NONE,
		(GBusNameAppearedCallback) NULL,
		(GBusNameVanishedCallback) sender_vanished_cb,
		g_object_ref (async_context->cancellable),
		(GDestroyNotify) g_object_unref);

	sender_table_insert (
		async_context->data_book,
		g_dbus_method_invocation_get_sender (invocation),
		async_context->cancellable);

	return async_context;
}

static void
async_context_free (AsyncContext *async_context)
{
	sender_table_remove (
		async_context->data_book,
		g_dbus_method_invocation_get_sender (
			async_context->invocation),
		async_context->cancellable);

	g_clear_object (&async_context->data_book);
	g_clear_object (&async_context->dbus_interface);
	g_clear_object (&async_context->invocation);
	g_clear_object (&async_context->cancellable);

	if (async_context->watcher_id > 0)
		g_bus_unwatch_name (async_context->watcher_id);

	g_slice_free (AsyncContext, async_context);
}

static gchar *
construct_bookview_path (void)
{
	static volatile gint counter = 1;

	g_atomic_int_inc (&counter);

	return g_strdup_printf (
		"/org/gnome/evolution/dataserver/AddressBookView/%d/%d",
		getpid (), counter);
}

static gchar *
construct_bookcursor_path (void)
{
	static volatile gint counter = 1;

	g_atomic_int_inc (&counter);

	return g_strdup_printf (
		"/org/gnome/evolution/dataserver/AddressBookCursor/%d/%d",
		getpid (), counter);
}

static void
data_book_convert_to_client_error (GError *error)
{
	g_return_if_fail (error != NULL);

	/* Data-Factory returns common error for unknown/broken ESource-s */
	if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND)) {
		error->domain = E_BOOK_CLIENT_ERROR;
		error->code = E_BOOK_CLIENT_ERROR_NO_SUCH_BOOK;
	}
}

/**
 * e_data_book_string_slist_to_comma_string:
 * @strings: (element-type utf8): a list of gchar *
 *
 * Takes a list of strings and converts it to a comma-separated string of
 * values; free returned pointer with g_free()
 *
 * Returns: comma-separated newly allocated text of @strings
 *
 * Since: 3.2
 **/
gchar *
e_data_book_string_slist_to_comma_string (const GSList *strings)
{
	GString *tmp;
	gchar *res;
	const GSList *l;

	tmp = g_string_new ("");
	for (l = strings; l != NULL; l = l->next) {
		const gchar *str = l->data;

		if (!str)
			continue;

		if (strchr (str, ',')) {
			g_warning ("%s: String cannot contain comma; skipping value '%s'\n", G_STRFUNC, str);
			continue;
		}

		if (tmp->len)
			g_string_append_c (tmp, ',');
		g_string_append (tmp, str);
	}

	res = e_util_utf8_make_valid (tmp->str);

	g_string_free (tmp, TRUE);

	return res;
}

static GPtrArray *
data_book_encode_properties (EDBusAddressBook *dbus_interface)
{
	GPtrArray *properties_array;

	g_warn_if_fail (E_DBUS_IS_ADDRESS_BOOK (dbus_interface));

	properties_array = g_ptr_array_new_with_free_func (g_free);

	if (dbus_interface) {
		GParamSpec **properties;
		guint ii, n_properties = 0;

		properties = g_object_class_list_properties (G_OBJECT_GET_CLASS (dbus_interface), &n_properties);

		for (ii = 0; ii < n_properties; ii++) {
			gboolean can_process =
				g_type_is_a (properties[ii]->value_type, G_TYPE_BOOLEAN) ||
				g_type_is_a (properties[ii]->value_type, G_TYPE_STRING) ||
				g_type_is_a (properties[ii]->value_type, G_TYPE_STRV) ||
				g_type_is_a (properties[ii]->value_type, G_TYPE_UCHAR) ||
				g_type_is_a (properties[ii]->value_type, G_TYPE_INT) ||
				g_type_is_a (properties[ii]->value_type, G_TYPE_UINT) ||
				g_type_is_a (properties[ii]->value_type, G_TYPE_INT64) ||
				g_type_is_a (properties[ii]->value_type, G_TYPE_UINT64) ||
				g_type_is_a (properties[ii]->value_type, G_TYPE_DOUBLE);

			if (can_process) {
				GValue value = G_VALUE_INIT;
				GVariant *stored = NULL;

				g_value_init (&value, properties[ii]->value_type);
				g_object_get_property ((GObject *) dbus_interface, properties[ii]->name, &value);

				#define WORKOUT(gvl, gvr) \
					if (g_type_is_a (properties[ii]->value_type, G_TYPE_ ## gvl)) \
						stored = g_dbus_gvalue_to_gvariant (&value, G_VARIANT_TYPE_ ## gvr);

				WORKOUT (BOOLEAN, BOOLEAN);
				WORKOUT (STRING, STRING);
				WORKOUT (STRV, STRING_ARRAY);
				WORKOUT (UCHAR, BYTE);
				WORKOUT (INT, INT32);
				WORKOUT (UINT, UINT32);
				WORKOUT (INT64, INT64);
				WORKOUT (UINT64, UINT64);
				WORKOUT (DOUBLE, DOUBLE);

				#undef WORKOUT

				g_value_unset (&value);

				if (stored) {
					g_ptr_array_add (properties_array, g_strdup (properties[ii]->name));
					g_ptr_array_add (properties_array, g_variant_print (stored, TRUE));

					g_variant_unref (stored);
				}
			}
		}

		g_free (properties);
	}

	g_ptr_array_add (properties_array, NULL);

	return properties_array;
}

static gboolean
data_book_handle_retrieve_properties_cb (EDBusAddressBook *dbus_interface,
					 GDBusMethodInvocation *invocation,
					 EDataBook *data_book)
{
	GPtrArray *properties_array;

	properties_array = data_book_encode_properties (dbus_interface);

	e_dbus_address_book_complete_retrieve_properties (
		dbus_interface,
		invocation,
		(const gchar * const *) properties_array->pdata);

	g_ptr_array_free (properties_array, TRUE);

	return TRUE;
}

static void
data_book_complete_open_cb (GObject *source_object,
                            GAsyncResult *result,
                            gpointer user_data)
{
	AsyncContext *async_context = user_data;
	GError *error = NULL;

	e_book_backend_open_finish (
		E_BOOK_BACKEND (source_object), result, &error);

	if (error == NULL) {
		GPtrArray *properties_array;

		properties_array = data_book_encode_properties (async_context->dbus_interface);

		e_dbus_address_book_complete_open (
			async_context->dbus_interface,
			async_context->invocation,
			(const gchar * const *) properties_array->pdata);

		g_ptr_array_free (properties_array, TRUE);
	} else {
		data_book_convert_to_client_error (error);
		g_dbus_method_invocation_take_error (
			async_context->invocation, error);
	}

	async_context_free (async_context);
}

static gboolean
data_book_handle_open_cb (EDBusAddressBook *dbus_interface,
                          GDBusMethodInvocation *invocation,
                          EDataBook *data_book)
{
	EBookBackend *backend;
	AsyncContext *async_context;

	backend = e_data_book_ref_backend (data_book);
	g_return_val_if_fail (backend != NULL, FALSE);

	async_context = async_context_new (data_book, invocation);

	e_book_backend_open (
		backend,
		async_context->cancellable,
		data_book_complete_open_cb,
		async_context);

	g_object_unref (backend);

	return TRUE;
}

static void
data_book_complete_refresh_cb (GObject *source_object,
                               GAsyncResult *result,
                               gpointer user_data)
{
	AsyncContext *async_context = user_data;
	GError *error = NULL;

	e_book_backend_refresh_finish (
		E_BOOK_BACKEND (source_object), result, &error);

	if (error == NULL) {
		e_dbus_address_book_complete_refresh (
			async_context->dbus_interface,
			async_context->invocation);
	} else {
		data_book_convert_to_client_error (error);
		g_dbus_method_invocation_take_error (
			async_context->invocation, error);
	}

	async_context_free (async_context);
}

static gboolean
data_book_handle_refresh_cb (EDBusAddressBook *dbus_interface,
                             GDBusMethodInvocation *invocation,
                             EDataBook *data_book)
{
	EBookBackend *backend;
	AsyncContext *async_context;

	backend = e_data_book_ref_backend (data_book);
	g_return_val_if_fail (backend != NULL, FALSE);

	async_context = async_context_new (data_book, invocation);

	e_book_backend_refresh (
		backend,
		async_context->cancellable,
		data_book_complete_refresh_cb,
		async_context);

	g_object_unref (backend);

	return TRUE;
}

static void
data_book_complete_get_contact_cb (GObject *source_object,
                                   GAsyncResult *result,
                                   gpointer user_data)
{
	AsyncContext *async_context = user_data;
	EContact *contact;
	GError *error = NULL;

	contact = e_book_backend_get_contact_finish (
		E_BOOK_BACKEND (source_object), result, &error);

	/* Sanity check. */
	g_return_if_fail (
		((contact != NULL) && (error == NULL)) ||
		((contact == NULL) && (error != NULL)));

	if (contact != NULL) {
		gchar *vcard;
		gchar *utf8_vcard;

		vcard = e_vcard_to_string (
			E_VCARD (contact),
			EVC_FORMAT_VCARD_30);
		utf8_vcard = e_util_utf8_make_valid (vcard);
		e_dbus_address_book_complete_get_contact (
			async_context->dbus_interface,
			async_context->invocation,
			utf8_vcard);
		g_free (utf8_vcard);
		g_free (vcard);

		g_object_unref (contact);
	} else {
		data_book_convert_to_client_error (error);
		g_dbus_method_invocation_take_error (
			async_context->invocation, error);
	}

	async_context_free (async_context);
}

static gboolean
data_book_handle_get_contact_cb (EDBusAddressBook *dbus_interface,
                                 GDBusMethodInvocation *invocation,
                                 const gchar *in_uid,
                                 EDataBook *data_book)
{
	EBookBackend *backend;
	AsyncContext *async_context;

	backend = e_data_book_ref_backend (data_book);
	g_return_val_if_fail (backend != NULL, FALSE);

	async_context = async_context_new (data_book, invocation);

	e_book_backend_get_contact (
		backend, in_uid,
		async_context->cancellable,
		data_book_complete_get_contact_cb,
		async_context);

	g_object_unref (backend);

	return TRUE;
}

static void
data_book_complete_get_contact_list_cb (GObject *source_object,
                                        GAsyncResult *result,
                                        gpointer user_data)
{
	AsyncContext *async_context = user_data;
	GQueue queue = G_QUEUE_INIT;
	GError *error = NULL;

	e_book_backend_get_contact_list_finish (
		E_BOOK_BACKEND (source_object), result, &queue, &error);

	if (error == NULL) {
		gchar **strv;
		gint ii = 0;

		strv = g_new0 (gchar *, queue.length + 1);

		while (!g_queue_is_empty (&queue)) {
			EContact *contact;
			gchar *vcard;

			contact = g_queue_pop_head (&queue);

			vcard = e_vcard_to_string (
				E_VCARD (contact),
				EVC_FORMAT_VCARD_30);
			strv[ii++] = e_util_utf8_make_valid (vcard);
			g_free (vcard);

			g_object_unref (contact);
		}

		e_dbus_address_book_complete_get_contact_list (
			async_context->dbus_interface,
			async_context->invocation,
			(const gchar * const *) strv);

		g_strfreev (strv);
	} else {
		data_book_convert_to_client_error (error);
		g_dbus_method_invocation_take_error (
			async_context->invocation, error);
	}

	async_context_free (async_context);
}

static gboolean
data_book_handle_get_contact_list_cb (EDBusAddressBook *dbus_interface,
                                      GDBusMethodInvocation *invocation,
                                      const gchar *in_query,
                                      EDataBook *data_book)
{
	EBookBackend *backend;
	AsyncContext *async_context;

	backend = e_data_book_ref_backend (data_book);
	g_return_val_if_fail (backend != NULL, FALSE);

	async_context = async_context_new (data_book, invocation);

	e_book_backend_get_contact_list (
		backend, in_query,
		async_context->cancellable,
		data_book_complete_get_contact_list_cb,
		async_context);

	g_object_unref (backend);

	return TRUE;
}

static void
data_book_complete_get_contact_list_uids_cb (GObject *source_object,
                                             GAsyncResult *result,
                                             gpointer user_data)
{
	AsyncContext *async_context = user_data;
	GQueue queue = G_QUEUE_INIT;
	GError *error = NULL;

	e_book_backend_get_contact_list_uids_finish (
		E_BOOK_BACKEND (source_object), result, &queue, &error);

	if (error == NULL) {
		gchar **strv;
		gint ii = 0;

		strv = g_new0 (gchar *, queue.length + 1);

		while (!g_queue_is_empty (&queue)) {
			gchar *uid = g_queue_pop_head (&queue);
			strv[ii++] = e_util_utf8_make_valid (uid);
			g_free (uid);
		}

		e_dbus_address_book_complete_get_contact_list_uids (
			async_context->dbus_interface,
			async_context->invocation,
			(const gchar * const *) strv);

		g_strfreev (strv);
	} else {
		data_book_convert_to_client_error (error);
		g_dbus_method_invocation_take_error (
			async_context->invocation, error);
	}

	async_context_free (async_context);
}

static gboolean
data_book_handle_get_contact_list_uids_cb (EDBusAddressBook *dbus_interface,
                                           GDBusMethodInvocation *invocation,
                                           const gchar *in_query,
                                           EDataBook *data_book)
{
	EBookBackend *backend;
	AsyncContext *async_context;

	backend = e_data_book_ref_backend (data_book);
	g_return_val_if_fail (backend != NULL, FALSE);

	async_context = async_context_new (data_book, invocation);

	e_book_backend_get_contact_list_uids (
		backend, in_query,
		async_context->cancellable,
		data_book_complete_get_contact_list_uids_cb,
		async_context);

	g_object_unref (backend);

	return TRUE;
}

static void
data_book_complete_create_contacts_cb (GObject *source_object,
                                       GAsyncResult *result,
                                       gpointer user_data)
{
	AsyncContext *async_context = user_data;
	GQueue queue = G_QUEUE_INIT;
	GError *error = NULL;

	e_book_backend_create_contacts_finish (
		E_BOOK_BACKEND (source_object), result, &queue, &error);

	if (error == NULL) {
		gchar **strv;
		gint ii = 0;

		strv = g_new0 (gchar *, queue.length + 1);

		while (!g_queue_is_empty (&queue)) {
			EContact *contact;
			const gchar *uid;

			contact = g_queue_pop_head (&queue);
			uid = e_contact_get_const (contact, E_CONTACT_UID);
			strv[ii++] = e_util_utf8_make_valid (uid);
			g_object_unref (contact);
		}

		e_dbus_address_book_complete_create_contacts (
			async_context->dbus_interface,
			async_context->invocation,
			(const gchar * const *) strv);

		g_strfreev (strv);
	} else {
		data_book_convert_to_client_error (error);
		g_dbus_method_invocation_take_error (
			async_context->invocation, error);
	}

	async_context_free (async_context);
}

static gboolean
data_book_handle_create_contacts_cb (EDBusAddressBook *dbus_interface,
				     GDBusMethodInvocation *invocation,
				     const gchar * const *in_vcards,
				     guint32 in_opflags,
				     EDataBook *data_book)
{
	EBookBackend *backend;
	AsyncContext *async_context;

	backend = e_data_book_ref_backend (data_book);
	g_return_val_if_fail (backend != NULL, FALSE);

	async_context = async_context_new (data_book, invocation);

	e_book_backend_create_contacts (
		backend, in_vcards, in_opflags,
		async_context->cancellable,
		data_book_complete_create_contacts_cb,
		async_context);

	g_object_unref (backend);

	return TRUE;
}

static void
data_book_complete_modify_contacts_cb (GObject *source_object,
                                       GAsyncResult *result,
                                       gpointer user_data)
{
	AsyncContext *async_context = user_data;
	GError *error = NULL;

	e_book_backend_modify_contacts_finish (
		E_BOOK_BACKEND (source_object), result, &error);

	if (error == NULL) {
		e_dbus_address_book_complete_modify_contacts (
			async_context->dbus_interface,
			async_context->invocation);
	} else {
		data_book_convert_to_client_error (error);
		g_dbus_method_invocation_take_error (
			async_context->invocation, error);
	}

	async_context_free (async_context);
}

static gboolean
data_book_handle_modify_contacts_cb (EDBusAddressBook *dbus_interface,
				     GDBusMethodInvocation *invocation,
				     const gchar * const *in_vcards,
				     guint32 in_opflags,
				     EDataBook *data_book)
{
	EBookBackend *backend;
	AsyncContext *async_context;

	backend = e_data_book_ref_backend (data_book);
	g_return_val_if_fail (backend != NULL, FALSE);

	async_context = async_context_new (data_book, invocation);

	e_book_backend_modify_contacts (
		backend, in_vcards, in_opflags,
		async_context->cancellable,
		data_book_complete_modify_contacts_cb,
		async_context);

	g_object_unref (backend);

	return TRUE;
}

static void
data_book_complete_remove_contacts_cb (GObject *source_object,
                                       GAsyncResult *result,
                                       gpointer user_data)
{
	AsyncContext *async_context = user_data;
	GError *error = NULL;

	e_book_backend_remove_contacts_finish (
		E_BOOK_BACKEND (source_object), result, &error);

	if (error == NULL) {
		e_dbus_address_book_complete_remove_contacts (
			async_context->dbus_interface,
			async_context->invocation);
	} else {
		data_book_convert_to_client_error (error);
		g_dbus_method_invocation_take_error (
			async_context->invocation, error);
	}

	async_context_free (async_context);
}

static gboolean
data_book_handle_remove_contacts_cb (EDBusAddressBook *dbus_interface,
				     GDBusMethodInvocation *invocation,
				     const gchar * const *in_uids,
				     guint32 in_opflags,
				     EDataBook *data_book)
{
	EBookBackend *backend;
	AsyncContext *async_context;

	backend = e_data_book_ref_backend (data_book);
	g_return_val_if_fail (backend != NULL, FALSE);

	async_context = async_context_new (data_book, invocation);

	e_book_backend_remove_contacts (
		backend, in_uids, in_opflags,
		async_context->cancellable,
		data_book_complete_remove_contacts_cb,
		async_context);

	g_object_unref (backend);

	return TRUE;
}

static gboolean
data_book_handle_get_view_cb (EDBusAddressBook *dbus_interface,
                              GDBusMethodInvocation *invocation,
                              const gchar *in_query,
                              EDataBook *data_book)
{
	EBookBackend *backend;
	EDataBookView *view;
	EBookBackendSExp *sexp;
	GDBusConnection *connection;
	gchar *object_path;
	GError *error = NULL;

	backend = e_data_book_ref_backend (data_book);
	g_return_val_if_fail (backend != NULL, FALSE);

	sexp = e_book_backend_sexp_new (in_query);
	if (sexp == NULL) {
		g_dbus_method_invocation_return_error_literal (
			invocation,
			E_CLIENT_ERROR,
			E_CLIENT_ERROR_INVALID_QUERY,
			_("Invalid query"));
		g_object_unref (backend);
		return TRUE;
	}

	object_path = construct_bookview_path ();
	connection = g_dbus_method_invocation_get_connection (invocation);

	view = e_data_book_view_new (
		backend, sexp, connection, object_path, &error);

	g_object_unref (sexp);

	/* Sanity check. */
	g_return_val_if_fail (
		((view != NULL) && (error == NULL)) ||
		((view == NULL) && (error != NULL)), FALSE);

	if (view != NULL) {
		e_dbus_address_book_complete_get_view (
			dbus_interface, invocation, object_path);
		e_book_backend_add_view (backend, view);
		g_object_unref (view);
	} else {
		data_book_convert_to_client_error (error);
		g_prefix_error (&error, "%s", _("Invalid query: "));
		g_dbus_method_invocation_take_error (invocation, error);
	}

	g_free (object_path);

	g_object_unref (backend);

	return TRUE;
}

static gboolean
data_book_interpret_sort_keys (const gchar * const *in_sort_keys,
                               const gchar * const *in_sort_types,
                               EContactField **out_sort_keys,
                               EBookCursorSortType **out_sort_types,
                               gint *n_fields,
                               GError **error)
{
	gint i, key_count = 0, type_count = 0;
	EContactField *sort_keys;
	EBookCursorSortType *sort_types;
	gboolean success = TRUE;

	if (!in_sort_keys || !in_sort_types) {
		g_set_error (
			error,
			E_CLIENT_ERROR,
			E_CLIENT_ERROR_INVALID_ARG,
			"Missing sort keys while trying to create a Cursor");
		return FALSE;
	}

	for (i = 0; in_sort_keys[i] != NULL; i++)
		key_count++;
	for (i = 0; in_sort_types[i] != NULL; i++)
		type_count++;

	if (key_count != type_count) {
		g_set_error (
			error,
			E_CLIENT_ERROR,
			E_CLIENT_ERROR_INVALID_ARG,
			"Must specify the same amount of sort keys as sort types while creating a Cursor");
		return FALSE;
	}

	sort_keys = g_new0 (EContactField, key_count);
	sort_types = g_new0 (EBookCursorSortType, type_count);

	for (i = 0; success && i < key_count; i++) {

		sort_keys[i] = e_contact_field_id (in_sort_keys[i]);

		if (sort_keys[i] == 0) {
			g_set_error (
				error,
				E_CLIENT_ERROR,
				E_CLIENT_ERROR_INVALID_ARG,
				"Invalid sort key '%s' specified when creating a Cursor",
				in_sort_keys[i]);
			success = FALSE;
		}
	}

	for (i = 0; success && i < type_count; i++) {
		gint enum_value = 0;

		if (!e_enum_from_string (E_TYPE_BOOK_CURSOR_SORT_TYPE,
					 in_sort_types[i],
					 &enum_value)) {
			g_set_error (
				error,
				E_CLIENT_ERROR,
				E_CLIENT_ERROR_INVALID_ARG,
				"Invalid sort type '%s' specified when creating a Cursor",
				in_sort_types[i]);
			success = FALSE;
		}

		sort_types[i] = enum_value;
	}

	if (!success) {
		g_free (sort_keys);
		g_free (sort_types);
	} else {
		*out_sort_keys = sort_keys;
		*out_sort_types = sort_types;
		*n_fields = key_count;
	}

	return success;
}

static gboolean
data_book_handle_get_cursor_cb (EDBusAddressBook *dbus_interface,
                                GDBusMethodInvocation *invocation,
                                const gchar *in_query,
                                const gchar * const *in_sort_keys,
                                const gchar * const *in_sort_types,
                                EDataBook *data_book)
{
	EBookBackend *backend;
	EDataBookCursor *cursor;
	GDBusConnection *connection;
	EContactField *sort_keys = NULL;
	EBookCursorSortType *sort_types = NULL;
	gint n_fields = 0;
	gchar *object_path;
	GError *error = NULL;

	backend = e_data_book_ref_backend (data_book);
	g_return_val_if_fail (backend != NULL, FALSE);

	/*
	 * Interpret arguments
	 */
	if (!data_book_interpret_sort_keys (in_sort_keys,
					    in_sort_types,
					    &sort_keys,
					    &sort_types,
					    &n_fields,
					    &error)) {
		g_dbus_method_invocation_take_error (invocation, error);
		g_object_unref (backend);
		return TRUE;
	}

	/*
	 * Create cursor
	 */
	cursor = e_book_backend_create_cursor (
		backend, sort_keys, sort_types, n_fields, &error);
	g_free (sort_keys);
	g_free (sort_types);

	if (!cursor) {
		g_dbus_method_invocation_take_error (invocation, error);
		g_object_unref (backend);
		return TRUE;
	}

	/*
	 * Set the query, if any (no query is allowed)
	 */
	if (!e_data_book_cursor_set_sexp (cursor, in_query, NULL, &error)) {

		e_book_backend_delete_cursor (backend, cursor, NULL);
		g_dbus_method_invocation_take_error (invocation, error);
		g_object_unref (backend);
		return TRUE;
	}

	object_path = construct_bookcursor_path ();
	connection = g_dbus_method_invocation_get_connection (invocation);

	/*
	 * Now export the object on the connection
	 */
	if (!e_data_book_cursor_register_gdbus_object (cursor, connection, object_path, &error)) {
		e_book_backend_delete_cursor (backend, cursor, NULL);
		g_dbus_method_invocation_take_error (invocation, error);
		g_object_unref (backend);
		g_free (object_path);
		return TRUE;
	}

	/*
	 * All is good in the hood, complete the method call
	 */
	e_dbus_address_book_complete_get_cursor (
		dbus_interface, invocation, object_path);
	g_free (object_path);
	g_object_unref (backend);
	return TRUE;
}

static void
data_book_complete_contains_email_cb (GObject *source_object,
				      GAsyncResult *result,
				      gpointer user_data)
{
	AsyncContext *async_context = user_data;
	GError *error = NULL;
	gboolean res;

	res = e_book_backend_contains_email_finish (E_BOOK_BACKEND (source_object), result, &error);

	if (error == NULL) {
		e_dbus_address_book_complete_contains_email (
			async_context->dbus_interface,
			async_context->invocation,
			res);
	} else {
		data_book_convert_to_client_error (error);
		g_dbus_method_invocation_take_error (
			async_context->invocation, error);
	}

	async_context_free (async_context);
}

static gboolean
data_book_handle_contains_email_cb (EDBusAddressBook *dbus_interface,
				    GDBusMethodInvocation *invocation,
				    const gchar *in_email,
				    EDataBook *data_book)
{
	EBookBackend *backend;
	AsyncContext *async_context;

	backend = e_data_book_ref_backend (data_book);
	g_return_val_if_fail (backend != NULL, FALSE);

	async_context = async_context_new (data_book, invocation);

	e_book_backend_contains_email (
		backend, in_email,
		async_context->cancellable,
		data_book_complete_contains_email_cb,
		async_context);

	g_object_unref (backend);

	return TRUE;
}

static void
data_book_source_unset_last_credentials_required_arguments_cb (GObject *source_object,
							       GAsyncResult *result,
							       gpointer user_data)
{
	GError *local_error = NULL;

	g_return_if_fail (E_IS_SOURCE (source_object));

	e_source_unset_last_credentials_required_arguments_finish (E_SOURCE (source_object), result, &local_error);

	if (local_error)
		g_debug ("%s: Call failed: %s", G_STRFUNC, local_error->message);

	g_clear_error (&local_error);
}

static gboolean
data_book_handle_close_cb (EDBusAddressBook *dbus_interface,
                           GDBusMethodInvocation *invocation,
                           EDataBook *data_book)
{
	EBookBackend *backend;
	ESource *source;
	const gchar *sender;

	/* G_DBUS_MESSAGE_FLAGS_NO_REPLY_EXPECTED should be set on
	 * the GDBusMessage, but we complete the invocation anyway
	 * and let the D-Bus machinery suppress the reply. */
	e_dbus_address_book_complete_close (dbus_interface, invocation);

	backend = e_data_book_ref_backend (data_book);
	g_return_val_if_fail (backend != NULL, FALSE);

	source = e_backend_get_source (E_BACKEND (backend));
	e_source_unset_last_credentials_required_arguments (source, NULL,
		data_book_source_unset_last_credentials_required_arguments_cb, NULL);

	sender = g_dbus_method_invocation_get_sender (invocation);
	g_signal_emit_by_name (backend, "closed", sender);

	g_object_unref (backend);

	return TRUE;
}

/**
 * e_data_book_respond_open:
 * @book: An #EDataBook
 * @opid: An operation ID
 * @error: Operation error, if any, automatically freed if passed it
 *
 * Notifies listeners of the completion of the open method call.
 **/
void
e_data_book_respond_open (EDataBook *book,
                          guint opid,
                          GError *error)
{
	EBookBackend *backend;
	GSimpleAsyncResult *simple;

	g_return_if_fail (E_IS_DATA_BOOK (book));

	backend = e_data_book_ref_backend (book);
	g_return_if_fail (backend != NULL);

	simple = e_book_backend_prepare_for_completion (backend, opid, NULL);
	g_return_if_fail (simple != NULL);

	/* Translators: This is prefix to a detailed error message */
	g_prefix_error (&error, "%s", _("Cannot open book: "));

	if (error != NULL)
		g_simple_async_result_take_error (simple, error);

	g_simple_async_result_complete_in_idle (simple);

	g_object_unref (simple);
	g_object_unref (backend);
}

/**
 * e_data_book_respond_refresh:
 * @book: An #EDataBook
 * @opid: An operation ID
 * @error: Operation error, if any, automatically freed if passed it
 *
 * Notifies listeners of the completion of the refresh method call.
 *
 * Since: 3.2
 */
void
e_data_book_respond_refresh (EDataBook *book,
                             guint32 opid,
                             GError *error)
{
	EBookBackend *backend;
	GSimpleAsyncResult *simple;

	g_return_if_fail (E_IS_DATA_BOOK (book));

	backend = e_data_book_ref_backend (book);
	g_return_if_fail (backend != NULL);

	simple = e_book_backend_prepare_for_completion (backend, opid, NULL);
	g_return_if_fail (simple);

	/* Translators: This is prefix to a detailed error message */
	g_prefix_error (&error, "%s", _("Cannot refresh address book: "));

	if (error != NULL)
		g_simple_async_result_take_error (simple, error);

	g_simple_async_result_complete_in_idle (simple);

	g_object_unref (simple);
	g_object_unref (backend);
}

/**
 * e_data_book_respond_get_contact:
 * @book: An #EDataBook
 * @opid: An operation ID
 * @error: (nullable) (transfer full): Operation error, if any, automatically freed if passed it
 * @contact: (nullable): the found #EContact, or %NULL, if it could not be found
 *
 * Notifies listeners of the completion of the get_contact method call.
 * Only one of @error and @contact can be set.
 */
void
e_data_book_respond_get_contact (EDataBook *book,
                                 guint32 opid,
                                 GError *error,
                                 const EContact *contact)
{
	EBookBackend *backend;
	GSimpleAsyncResult *simple;
	GQueue *queue = NULL;

	g_return_if_fail (E_IS_DATA_BOOK (book));

	backend = e_data_book_ref_backend (book);
	g_return_if_fail (backend != NULL);

	simple = e_book_backend_prepare_for_completion (backend, opid, &queue);
	g_return_if_fail (simple != NULL);
	g_return_if_fail (queue != NULL);

	/* Translators: This is prefix to a detailed error message */
	g_prefix_error (&error, "%s", _("Cannot get contact: "));

	if (error == NULL) {
		g_queue_push_tail (queue, g_object_ref ((EContact *) contact));
	} else {
		g_simple_async_result_take_error (simple, error);
	}

	g_simple_async_result_complete_in_idle (simple);

	g_object_unref (simple);
	g_object_unref (backend);
}

/**
 * e_data_book_respond_get_contact_list:
 * @book: An #EDataBook
 * @opid: An operation ID
 * @error: Operation error, if any, automatically freed if passed it
 * @contacts: (element-type EContact): A list of #EContact, empty on error
 *
 * Finishes a call to get list of #EContact, which satisfy certain criteria.
 *
 * Since: 3.2
 **/
void
e_data_book_respond_get_contact_list (EDataBook *book,
                                      guint32 opid,
                                      GError *error,
                                      const GSList *contacts)
{
	EBookBackend *backend;
	GSimpleAsyncResult *simple;
	GQueue *queue = NULL;

	g_return_if_fail (E_IS_DATA_BOOK (book));

	backend = e_data_book_ref_backend (book);
	g_return_if_fail (backend != NULL);

	simple = e_book_backend_prepare_for_completion (backend, opid, &queue);
	g_return_if_fail (simple != NULL);
	g_return_if_fail (queue != NULL);

	/* Translators: This is prefix to a detailed error message */
	g_prefix_error (&error, "%s", _("Cannot get contact list: "));

	if (error == NULL) {
		GSList *link;

		for (link = (GSList *) contacts; link; link = g_slist_next (link)) {
			EContact *contact = link->data;

			g_queue_push_tail (queue, g_object_ref (contact));
		}

	} else {
		g_simple_async_result_take_error (simple, error);
	}

	g_simple_async_result_complete_in_idle (simple);

	g_object_unref (simple);
	g_object_unref (backend);
}

/**
 * e_data_book_respond_get_contact_list_uids:
 * @book: An #EDataBook
 * @opid: An operation ID
 * @error: Operation error, if any, automatically freed if passed it
 * @uids: (element-type utf8): A list of picked UIDs, empty on error
 *
 * Finishes a call to get list of UIDs which satisfy certain criteria.
 *
 * Since: 3.2
 **/
void
e_data_book_respond_get_contact_list_uids (EDataBook *book,
                                           guint32 opid,
                                           GError *error,
                                           const GSList *uids)
{
	EBookBackend *backend;
	GSimpleAsyncResult *simple;
	GQueue *queue = NULL;

	g_return_if_fail (E_IS_DATA_BOOK (book));

	backend = e_data_book_ref_backend (book);
	g_return_if_fail (backend != NULL);

	simple = e_book_backend_prepare_for_completion (backend, opid, &queue);
	g_return_if_fail (simple != NULL);
	g_return_if_fail (queue != NULL);

	/* Translators: This is prefix to a detailed error message */
	g_prefix_error (&error, "%s", _("Cannot get contact list uids: "));

	if (error == NULL) {
		GSList *list, *link;

		list = (GSList *) uids;

		for (link = list; link != NULL; link = g_slist_next (link))
			g_queue_push_tail (queue, g_strdup (link->data));

	} else {
		g_simple_async_result_take_error (simple, error);
	}

	g_simple_async_result_complete_in_idle (simple);

	g_object_unref (simple);
	g_object_unref (backend);
}

/**
 * e_data_book_respond_create_contacts:
 * @book: An #EDataBook
 * @opid: An operation ID
 * @error: Operation error, if any, automatically freed if passed it
 * @contacts: (element-type EContact): A list of created #EContact(s), empty on error
 *
 * Finishes a call to create a list contacts.
 *
 * Since: 3.4
 **/
void
e_data_book_respond_create_contacts (EDataBook *book,
                                     guint32 opid,
                                     GError *error,
                                     const GSList *contacts)
{
	EBookBackend *backend;
	GSimpleAsyncResult *simple;
	GQueue *queue = NULL;

	g_return_if_fail (E_IS_DATA_BOOK (book));

	backend = e_data_book_ref_backend (book);
	g_return_if_fail (backend != NULL);

	simple = e_book_backend_prepare_for_completion (backend, opid, &queue);
	g_return_if_fail (simple != NULL);
	g_return_if_fail (queue != NULL);

	/* Translators: This is prefix to a detailed error message */
	g_prefix_error (&error, "%s", _("Cannot add contact: "));

	if (error == NULL) {
		GSList *link;

		for (link = (GSList *) contacts; link; link = g_slist_next (link)) {
			EContact *contact = link->data;
			g_queue_push_tail (queue, g_object_ref (contact));
		}

	} else {
		g_simple_async_result_take_error (simple, error);
	}

	g_simple_async_result_complete_in_idle (simple);

	g_object_unref (simple);
	g_object_unref (backend);
}

/**
 * e_data_book_respond_modify_contacts:
 * @book: An #EDataBook
 * @opid: An operation ID
 * @error: Operation error, if any, automatically freed if passed it
 * @contacts: (element-type EContact): A list of modified #EContact(s), empty on error
 *
 * Finishes a call to modify a list of contacts.
 *
 * Since: 3.4
 **/
void
e_data_book_respond_modify_contacts (EDataBook *book,
                                     guint32 opid,
                                     GError *error,
                                     const GSList *contacts)
{
	EBookBackend *backend;
	GSimpleAsyncResult *simple;
	GQueue *queue = NULL;

	g_return_if_fail (E_IS_DATA_BOOK (book));

	backend = e_data_book_ref_backend (book);
	g_return_if_fail (backend != NULL);

	simple = e_book_backend_prepare_for_completion (backend, opid, &queue);
	g_return_if_fail (simple != NULL);
	g_return_if_fail (queue != NULL);

	/* Translators: This is prefix to a detailed error message */
	g_prefix_error (&error, "%s", _("Cannot modify contacts: "));

	if (error == NULL) {
		GSList *link;

		for (link = (GSList *) contacts; link; link = g_slist_next (link)) {
			EContact *contact = contacts->data;

			g_queue_push_tail (queue, g_object_ref (contact));
		}

	} else {
		g_simple_async_result_take_error (simple, error);
	}

	g_simple_async_result_complete_in_idle (simple);

	g_object_unref (simple);
	g_object_unref (backend);
}

/**
 * e_data_book_respond_remove_contacts:
 * @book: An #EDataBook
 * @opid: An operation ID
 * @error: Operation error, if any, automatically freed if passed it
 * @ids: (element-type utf8): A list of removed contact UID-s, empty on error
 *
 * Finishes a call to remove a list of contacts.
 *
 * Since: 3.4
 **/
void
e_data_book_respond_remove_contacts (EDataBook *book,
                                     guint32 opid,
                                     GError *error,
                                     const GSList *ids)
{
	EBookBackend *backend;
	GSimpleAsyncResult *simple;
	GQueue *queue = NULL;

	g_return_if_fail (E_IS_DATA_BOOK (book));

	backend = e_data_book_ref_backend (book);
	g_return_if_fail (backend != NULL);

	simple = e_book_backend_prepare_for_completion (backend, opid, &queue);
	g_return_if_fail (simple != NULL);
	g_return_if_fail (queue != NULL);

	/* Translators: This is prefix to a detailed error message */
	g_prefix_error (&error, "%s", _("Cannot remove contacts: "));

	if (error == NULL) {
		GSList *link;

		for (link = (GSList *) ids; link; link = g_slist_next (link))
			g_queue_push_tail (queue, g_strdup (link->data));

	} else {
		g_simple_async_result_take_error (simple, error);
	}

	g_simple_async_result_complete_in_idle (simple);

	g_object_unref (simple);
	g_object_unref (backend);
}

/**
 * e_data_book_respond_contains_email:
 * @book: An #EDataBook
 * @opid: An operation ID
 * @error: Operation error, if any, automatically freed if passed it
 * @found: %TRUE, when found the email in the address book
 *
 * Finishes a call to check whether contains an email address.
 *
 * Since: 3.44
 **/
void
e_data_book_respond_contains_email (EDataBook *book,
				    guint32 opid,
				    GError *error,
				    gboolean found)
{
	EBookBackend *backend;
	GSimpleAsyncResult *simple;

	g_return_if_fail (E_IS_DATA_BOOK (book));

	backend = e_data_book_ref_backend (book);
	g_return_if_fail (backend != NULL);

	simple = e_book_backend_prepare_for_completion (backend, opid, NULL);
	g_return_if_fail (simple != NULL);

	/* Translators: This is prefix to a detailed error message */
	g_prefix_error (&error, "%s", _("Cannot find email address: "));

	if (error == NULL) {
		g_simple_async_result_set_op_res_gboolean (simple, found);
	} else {
		g_simple_async_result_take_error (simple, error);
	}

	g_simple_async_result_complete_in_idle (simple);

	g_object_unref (simple);
	g_object_unref (backend);
}

/**
 * e_data_book_report_error:
 * @book: An #EDataBook
 * @message: An error message
 *
 * Notifies the clients about an error, which happened out of any client-initiate operation.
 *
 * Since: 3.2
 **/
void
e_data_book_report_error (EDataBook *book,
                          const gchar *message)
{
	gchar *valid_utf8;

	g_return_if_fail (E_IS_DATA_BOOK (book));
	g_return_if_fail (message != NULL);

	valid_utf8 = e_util_utf8_make_valid (message);

	e_dbus_address_book_emit_error (book->priv->dbus_interface, valid_utf8 ? valid_utf8 : message);

	g_free (valid_utf8);
}

/**
 * e_data_book_report_backend_property_changed:
 * @book: An #EDataBook
 * @prop_name: Property name which changed
 * @prop_value: (nullable): The new property value
 *
 * Notifies the clients about a property change.
 *
 * Since: 3.2
 **/
void
e_data_book_report_backend_property_changed (EDataBook *book,
                                             const gchar *prop_name,
                                             const gchar *prop_value)
{
	EDBusAddressBook *dbus_interface;
	gchar **strv;

	g_return_if_fail (E_IS_DATA_BOOK (book));
	g_return_if_fail (prop_name != NULL);

	if (prop_value == NULL)
		prop_value = "";

	dbus_interface = book->priv->dbus_interface;

	/* XXX This will be NULL in direct access mode.  No way to
	 *     report property changes, I guess.  Return silently. */
	if (dbus_interface == NULL)
		return;

	if (g_str_equal (prop_name, CLIENT_BACKEND_PROPERTY_CAPABILITIES)) {
		strv = g_strsplit (prop_value, ",", -1);
		e_dbus_address_book_set_capabilities (
			dbus_interface, (const gchar * const *) strv);
		g_strfreev (strv);
	}

	if (g_str_equal (prop_name, CLIENT_BACKEND_PROPERTY_REVISION))
		e_dbus_address_book_set_revision (dbus_interface, prop_value);

	if (g_str_equal (prop_name, E_BOOK_BACKEND_PROPERTY_REQUIRED_FIELDS)) {
		strv = g_strsplit (prop_value, ",", -1);
		e_dbus_address_book_set_required_fields (
			dbus_interface, (const gchar * const *) strv);
		g_strfreev (strv);
	}

	if (g_str_equal (prop_name, E_BOOK_BACKEND_PROPERTY_SUPPORTED_FIELDS)) {
		strv = g_strsplit (prop_value, ",", -1);
		e_dbus_address_book_set_supported_fields (
			dbus_interface, (const gchar * const *) strv);
		g_strfreev (strv);
	}

	/* Ensure the property change signal on the D-Bus is invoked immediately, not on idle */
	g_dbus_interface_skeleton_flush (G_DBUS_INTERFACE_SKELETON (dbus_interface));

	if (book->priv->connection && !g_dbus_connection_is_closed (book->priv->connection))
		g_dbus_connection_flush_sync (book->priv->connection, NULL, NULL);

	/* Disregard anything else. */
}

static void
data_book_set_backend (EDataBook *book,
                       EBookBackend *backend)
{
	g_return_if_fail (E_IS_BOOK_BACKEND (backend));

	g_weak_ref_set (&book->priv->backend, backend);
}

static void
data_book_set_connection (EDataBook *book,
                          GDBusConnection *connection)
{
	g_return_if_fail (connection == NULL ||
			  G_IS_DBUS_CONNECTION (connection));
	g_return_if_fail (book->priv->connection == NULL);

	if (connection)
		book->priv->connection = g_object_ref (connection);
}

static void
data_book_set_object_path (EDataBook *book,
                           const gchar *object_path)
{
	g_return_if_fail (book->priv->object_path == NULL);

	book->priv->object_path = g_strdup (object_path);
}

static void
data_book_set_property (GObject *object,
                        guint property_id,
                        const GValue *value,
                        GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_BACKEND:
			data_book_set_backend (
				E_DATA_BOOK (object),
				g_value_get_object (value));
			return;

		case PROP_CONNECTION:
			data_book_set_connection (
				E_DATA_BOOK (object),
				g_value_get_object (value));
			return;

		case PROP_OBJECT_PATH:
			data_book_set_object_path (
				E_DATA_BOOK (object),
				g_value_get_string (value));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
data_book_get_property (GObject *object,
                        guint property_id,
                        GValue *value,
                        GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_BACKEND:
			g_value_take_object (
				value,
				e_data_book_ref_backend (
				E_DATA_BOOK (object)));
			return;

		case PROP_CONNECTION:
			g_value_set_object (
				value,
				e_data_book_get_connection (
				E_DATA_BOOK (object)));
			return;

		case PROP_OBJECT_PATH:
			g_value_set_string (
				value,
				e_data_book_get_object_path (
				E_DATA_BOOK (object)));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
data_book_dispose (GObject *object)
{
	EDataBookPrivate *priv;

	priv = E_DATA_BOOK (object)->priv;

	g_weak_ref_set (&priv->backend, NULL);

	g_clear_object (&priv->connection);
	g_clear_object (&priv->direct_book);

	if (priv->direct_module) {
		g_type_module_unuse (G_TYPE_MODULE (priv->direct_module));
		priv->direct_module = NULL;
	}

	g_hash_table_remove_all (priv->sender_table);

	/* Chain up to parent's dispose() metnod. */
	G_OBJECT_CLASS (e_data_book_parent_class)->dispose (object);
}

static void
data_book_finalize (GObject *object)
{
	EDataBookPrivate *priv;

	priv = E_DATA_BOOK (object)->priv;

	g_free (priv->object_path);

	g_mutex_clear (&priv->sender_lock);
	g_weak_ref_clear (&priv->backend);
	g_hash_table_destroy (priv->sender_table);

	g_clear_object (&priv->dbus_interface);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (e_data_book_parent_class)->finalize (object);
}

static void
data_book_constructed (GObject *object)
{
	EDataBook *book = E_DATA_BOOK (object);
	EBookBackend *backend;
	const gchar *prop_name;
	gchar *prop_value;

	/* Chain up to parent's constructed() method. */
	G_OBJECT_CLASS (e_data_book_parent_class)->constructed (object);

	backend = e_data_book_ref_backend (book);
	g_warn_if_fail (backend != NULL);

	/* Attach ourselves to the EBookBackend. */
	e_book_backend_set_data_book (backend, book);

	e_binding_bind_property (
		backend, "cache-dir",
		book->priv->dbus_interface, "cache-dir",
		G_BINDING_SYNC_CREATE);

	e_binding_bind_property (
		backend, "online",
		book->priv->dbus_interface, "online",
		G_BINDING_SYNC_CREATE);

	e_binding_bind_property (
		backend, "writable",
		book->priv->dbus_interface, "writable",
		G_BINDING_SYNC_CREATE);

	/* XXX Initialize the rest of the properties. */

	prop_name = CLIENT_BACKEND_PROPERTY_CAPABILITIES;
	prop_value = e_book_backend_get_backend_property (backend, prop_name);
	e_data_book_report_backend_property_changed (
		book, prop_name, prop_value);
	g_free (prop_value);

	prop_name = CLIENT_BACKEND_PROPERTY_REVISION;
	prop_value = e_book_backend_get_backend_property (backend, prop_name);
	e_data_book_report_backend_property_changed (
		book, prop_name, prop_value);
	g_free (prop_value);

	prop_name = E_BOOK_BACKEND_PROPERTY_REQUIRED_FIELDS;
	prop_value = e_book_backend_get_backend_property (backend, prop_name);
	e_data_book_report_backend_property_changed (
		book, prop_name, prop_value);
	g_free (prop_value);

	prop_name = E_BOOK_BACKEND_PROPERTY_SUPPORTED_FIELDS;
	prop_value = e_book_backend_get_backend_property (backend, prop_name);
	e_data_book_report_backend_property_changed (
		book, prop_name, prop_value);
	g_free (prop_value);

	/* Initialize the locale to the value reported by setlocale() until
	 * systemd says otherwise.
	 */
	e_dbus_address_book_set_locale (
		book->priv->dbus_interface,
		setlocale (LC_COLLATE, NULL));

	g_object_unref (backend);
}

static gboolean
data_book_initable_init (GInitable *initable,
                         GCancellable *cancellable,
                         GError **error)
{
	EBookBackend *backend;
	EDataBook *book;
	gchar *locale;

	book = E_DATA_BOOK (initable);

	/* XXX If we're serving a direct access backend only for the
	 *     purpose of catching "respond" calls, skip this stuff. */
	if (book->priv->connection == NULL)
		return TRUE;
	if (book->priv->object_path == NULL)
		return TRUE;

	/* This will be NULL for a backend that
	 * does not support direct read access. */
	backend = e_data_book_ref_backend (book);
	book->priv->direct_book = e_book_backend_get_direct_book (backend);
	g_object_unref (backend);

	if (book->priv->direct_book != NULL) {
		gboolean success;

		success = e_data_book_direct_register_gdbus_object (
			book->priv->direct_book,
			book->priv->connection,
			book->priv->object_path,
			error);

		if (!success)
			return FALSE;
	}

	/* Fetch backend configured locale and set that as the initial
	 * value on the dbus object
	 */
	locale = e_book_backend_dup_locale (backend);
	e_dbus_address_book_set_locale (book->priv->dbus_interface, locale);
	g_free (locale);

	return g_dbus_interface_skeleton_export (
		G_DBUS_INTERFACE_SKELETON (book->priv->dbus_interface),
		book->priv->connection,
		book->priv->object_path,
		error);
}

static void
e_data_book_class_init (EDataBookClass *class)
{
	GObjectClass *object_class;

	object_class = G_OBJECT_CLASS (class);
	object_class->set_property = data_book_set_property;
	object_class->get_property = data_book_get_property;
	object_class->dispose = data_book_dispose;
	object_class->finalize = data_book_finalize;
	object_class->constructed = data_book_constructed;

	g_object_class_install_property (
		object_class,
		PROP_BACKEND,
		g_param_spec_object (
			"backend",
			"Backend",
			"The backend driving this connection",
			E_TYPE_BOOK_BACKEND,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT_ONLY |
			G_PARAM_STATIC_STRINGS));

	g_object_class_install_property (
		object_class,
		PROP_CONNECTION,
		g_param_spec_object (
			"connection",
			"Connection",
			"The GDBusConnection on which to "
			"export the address book interface",
			G_TYPE_DBUS_CONNECTION,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT_ONLY |
			G_PARAM_STATIC_STRINGS));

	g_object_class_install_property (
		object_class,
		PROP_OBJECT_PATH,
		g_param_spec_string (
			"object-path",
			"Object Path",
			"The object path at which to "
			"export the address book interface",
			NULL,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT_ONLY |
			G_PARAM_STATIC_STRINGS));
}

static void
e_data_book_initable_init (GInitableIface *iface)
{
	iface->init = data_book_initable_init;
}

static void
e_data_book_init (EDataBook *data_book)
{
	EDBusAddressBook *dbus_interface;

	data_book->priv = e_data_book_get_instance_private (data_book);

	dbus_interface = e_dbus_address_book_skeleton_new ();
	data_book->priv->dbus_interface = dbus_interface;

	g_mutex_init (&data_book->priv->sender_lock);
	g_weak_ref_init (&data_book->priv->backend, NULL);

	data_book->priv->sender_table = g_hash_table_new_full (
		(GHashFunc) g_str_hash,
		(GEqualFunc) g_str_equal,
		(GDestroyNotify) g_free,
		(GDestroyNotify) g_ptr_array_unref);

	g_signal_connect (
		dbus_interface, "handle-retrieve-properties",
		G_CALLBACK (data_book_handle_retrieve_properties_cb),
		data_book);
	g_signal_connect (
		dbus_interface, "handle-open",
		G_CALLBACK (data_book_handle_open_cb),
		data_book);
	g_signal_connect (
		dbus_interface, "handle-refresh",
		G_CALLBACK (data_book_handle_refresh_cb),
		data_book);
	g_signal_connect (
		dbus_interface, "handle-get-contact",
		G_CALLBACK (data_book_handle_get_contact_cb),
		data_book);
	g_signal_connect (
		dbus_interface, "handle-get-contact-list",
		G_CALLBACK (data_book_handle_get_contact_list_cb),
		data_book);
	g_signal_connect (
		dbus_interface, "handle-get-contact-list-uids",
		G_CALLBACK (data_book_handle_get_contact_list_uids_cb),
		data_book);
	g_signal_connect (
		dbus_interface, "handle-create-contacts",
		G_CALLBACK (data_book_handle_create_contacts_cb),
		data_book);
	g_signal_connect (
		dbus_interface, "handle-remove-contacts",
		G_CALLBACK (data_book_handle_remove_contacts_cb),
		data_book);
	g_signal_connect (
		dbus_interface, "handle-modify-contacts",
		G_CALLBACK (data_book_handle_modify_contacts_cb),
		data_book);
	g_signal_connect (
		dbus_interface, "handle-get-view",
		G_CALLBACK (data_book_handle_get_view_cb),
		data_book);
	g_signal_connect (
		dbus_interface, "handle-get-cursor",
		G_CALLBACK (data_book_handle_get_cursor_cb),
		data_book);
	g_signal_connect (
		dbus_interface, "handle-contains-email",
		G_CALLBACK (data_book_handle_contains_email_cb),
		data_book);
	g_signal_connect (
		dbus_interface, "handle-close",
		G_CALLBACK (data_book_handle_close_cb),
		data_book);
}

/**
 * e_data_book_new:
 * @backend: (type EBookBackend): an #EBookBackend
 * @connection: a #GDBusConnection
 * @object_path: object path for the D-Bus interface
 * @error: return location for a #GError, or %NULL
 *
 * Creates a new #EDataBook and exports the AddressBook D-Bus interface
 * on @connection at @object_path.  The #EDataBook handles incoming remote
 * method invocations and forwards them to the @backend.  If the AddressBook
 * interface fails to export, the function sets @error and returns %NULL.
 *
 * Returns: an #EDataBook, or %NULL on error
 **/
EDataBook *
e_data_book_new (EBookBackend *backend,
                 GDBusConnection *connection,
                 const gchar *object_path,
                 GError **error)
{
	g_return_val_if_fail (E_IS_BOOK_BACKEND (backend), NULL);
	g_return_val_if_fail (G_IS_DBUS_CONNECTION (connection), NULL);
	g_return_val_if_fail (object_path != NULL, NULL);

	return g_initable_new (
		E_TYPE_DATA_BOOK, NULL, error,
		"backend", backend,
		"connection", connection,
		"object-path", object_path,
		NULL);
}

/**
 * e_data_book_ref_backend:
 * @book: an #EDataBook
 *
 * Returns the #EBookBackend to which incoming remote method invocations
 * are being forwarded.
 *
 * The returned #EBookBackend is referenced for thread-safety and should
 * be unreferenced with g_object_unref() when finished with it.
 *
 * Returns: (transfer full) (type EBookBackend): an #EBookBackend
 *
 * Since: 3.10
 **/
EBookBackend *
e_data_book_ref_backend (EDataBook *book)
{
	g_return_val_if_fail (E_IS_DATA_BOOK (book), NULL);

	return g_weak_ref_get (&book->priv->backend);
}

/**
 * e_data_book_get_connection:
 * @book: an #EDataBook
 *
 * Returns the #GDBusConnection on which the AddressBook D-Bus interface
 * is exported.
 *
 * Returns: (transfer none): the #GDBusConnection
 *
 * Since: 3.8
 **/
GDBusConnection *
e_data_book_get_connection (EDataBook *book)
{
	g_return_val_if_fail (E_IS_DATA_BOOK (book), NULL);

	return book->priv->connection;
}

/**
 * e_data_book_get_object_path:
 * @book: an #EDataBook
 *
 * Returns the object path at which the AddressBook D-Bus interface is
 * exported.
 *
 * Returns: the object path
 *
 * Since: 3.8
 **/
const gchar *
e_data_book_get_object_path (EDataBook *book)
{
	g_return_val_if_fail (E_IS_DATA_BOOK (book), NULL);

	return book->priv->object_path;
}

/**
 * e_data_book_set_locale:
 * @book: an #EDataBook 
 * @locale: the new locale to set for this book
 * @cancellable: a #GCancellable
 * @error: a location to store any error which might occur
 *
 * Set's the locale for this addressbook, this can result in renormalization of
 * locale sensitive data.
 *
 * Returns: %TRUE on success, otherwise %FALSE is returned and @error is set appropriately.
 *
 * Since: 3.12
 */
gboolean
e_data_book_set_locale (EDataBook *book,
                        const gchar *locale,
                        GCancellable *cancellable,
                        GError **error)
{
	EBookBackend *backend;
	gboolean success;

	g_return_val_if_fail (E_IS_DATA_BOOK (book), FALSE);

	backend = e_data_book_ref_backend (book);
	success = e_book_backend_set_locale (
		backend, locale, cancellable, error);

	if (success) {
		e_dbus_address_book_set_locale (
			book->priv->dbus_interface, locale);
		g_dbus_interface_skeleton_flush (
			G_DBUS_INTERFACE_SKELETON (book->priv->dbus_interface));
	}

	g_object_unref (backend);

	return success;
}
