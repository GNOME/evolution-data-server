/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * SPDX-FileCopyrightText: (C) 2022 Red Hat (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

/**
 * SECTION: e-gdata-session
 * @include: libedataserver/libedataserver.h
 * @short_description: A GData (Google Data) session
 *
 * The #EGDataSession is a class to work with Google's GData API.
 **/

#include "evolution-data-server-config.h"

#include <glib.h>
#include <glib/gi18n-lib.h>

#include "camel/camel.h"

#include "e-flag.h"
#include "e-json-utils.h"
#include "e-gdata-query.h"
#include "e-soup-session.h"

#include "e-gdata-session.h"

#define LOCK(x) g_rec_mutex_lock (&(x->priv->property_lock))
#define UNLOCK(x) g_rec_mutex_unlock (&(x->priv->property_lock))

#define E_GDATA_RETRY_IO_ERROR_SECONDS 3

#define TASKS_API_URL		"https://tasks.googleapis.com"
#define TASKLISTS_TOP_URL	TASKS_API_URL "/tasks/v1/users/@me/lists"
#define TASKS_TOP_URL		TASKS_API_URL "/tasks/v1/lists"

struct _EGDataSessionPrivate {
	GRecMutex property_lock;
	gint64 backoff_for_usec;
};

G_DEFINE_TYPE_WITH_PRIVATE (EGDataSession, e_gdata_session, E_TYPE_SOUP_SESSION)

static void
e_gdata_session_finalize (GObject *object)
{
	EGDataSession *self = E_GDATA_SESSION (object);

	g_rec_mutex_clear (&self->priv->property_lock);

	/* Chain up to parent's method. */
	G_OBJECT_CLASS (e_gdata_session_parent_class)->finalize (object);
}

static void
e_gdata_session_class_init (EGDataSessionClass *klass)
{
	GObjectClass *object_class;

	object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = e_gdata_session_finalize;
}

static void
e_gdata_session_init (EGDataSession *self)
{
	self->priv = e_gdata_session_get_instance_private (self);

	g_rec_mutex_init (&self->priv->property_lock);

	e_soup_session_setup_logging (E_SOUP_SESSION (self), g_getenv ("GDATA_DEBUG"));
}

/**
 * e_gdata_session_new:
 * @source: an #ESource
 *
 * Creates a new #EGDataSession associated with the given @source.
 *
 * Returns: (transfer full): a new #EGDataSession; free it with g_object_unref(),
 *    when no longer needed.
 *
 * Since: 3.46
 **/
EGDataSession *
e_gdata_session_new (ESource *source)
{
	g_return_val_if_fail (E_IS_SOURCE (source), NULL);

	return g_object_new (E_TYPE_GDATA_SESSION,
		"source", source,
		NULL);
}

static void
e_gdata_session_request_cancelled_cb (GCancellable *cancellable,
				      gpointer user_data)
{
	EFlag *flag = user_data;

	g_return_if_fail (flag != NULL);

	e_flag_set (flag);
}

/* (transfer full) (nullable): Free the *out_node with json_node_unref(), if not NULL;
   It can return 'success', even when the *out_node is NULL. */
static gboolean
e_gdata_session_json_node_from_message (SoupMessage *message,
					GByteArray *bytes,
					JsonNode **out_node,
					GCancellable *cancellable,
					GError **error)
{
	const gchar *content_type;
	gboolean success = TRUE;
	GError *local_error = NULL;

	g_return_val_if_fail (SOUP_IS_MESSAGE (message), FALSE);
	g_return_val_if_fail (out_node != NULL, FALSE);

	*out_node = NULL;

	content_type = soup_message_get_response_headers (message) ?
		soup_message_headers_get_content_type (soup_message_get_response_headers (message), NULL) : NULL;

	if (content_type && g_ascii_strcasecmp (content_type, "application/json") == 0) {
		JsonParser *json_parser;

		json_parser = json_parser_new_immutable ();

		if (bytes) {
			success = json_parser_load_from_data (json_parser, (const gchar *) bytes->data, bytes->len, error);
		} else {
			/* This should not happen, it's for safety check only, thus the string is not localized */
			success = FALSE;
			g_set_error_literal (&local_error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "No JSON data found");
		}

		if (success)
			*out_node = json_parser_steal_root (json_parser);

		g_object_unref (json_parser);
	}

	if (!success && *out_node)
		g_clear_pointer (out_node, json_node_unref);

	if (local_error)
		g_propagate_error (error, local_error);

	return success;
}

static gboolean
e_gdata_session_send_request_sync (EGDataSession *self,
				   SoupMessage *message,
				   JsonNode **out_node,
				   GByteArray **out_bytes,
				   GCancellable *cancellable,
				   GError **error)
{
	gint need_retry_seconds = 5;
	gboolean did_io_error_retry = FALSE;
	gboolean success = FALSE, need_retry = TRUE;

	g_return_val_if_fail (E_IS_GDATA_SESSION (self), FALSE);
	g_return_val_if_fail (SOUP_IS_MESSAGE (message), FALSE);
	g_return_val_if_fail (out_node == NULL || out_bytes == NULL, FALSE);

	while (need_retry && !g_cancellable_is_cancelled (cancellable)) {
		gboolean is_io_error = FALSE;
		GByteArray *bytes;
		GError *local_error = NULL;

		need_retry = FALSE;

		LOCK (self);

		if (self->priv->backoff_for_usec) {
			EFlag *flag;
			gint64 wait_ms;
			gulong handler_id = 0;

			wait_ms = self->priv->backoff_for_usec / G_TIME_SPAN_MILLISECOND;

			UNLOCK (self);

			flag = e_flag_new ();

			if (cancellable) {
				handler_id = g_cancellable_connect (cancellable, G_CALLBACK (e_gdata_session_request_cancelled_cb),
					flag, NULL);
			}

			while (wait_ms > 0 && !g_cancellable_is_cancelled (cancellable)) {
				gint64 now = g_get_monotonic_time ();
				gint left_minutes, left_seconds;

				left_minutes = wait_ms / 60000;
				left_seconds = (wait_ms / 1000) % 60;

				if (left_minutes > 0) {
					camel_operation_push_message (cancellable,
						g_dngettext (GETTEXT_PACKAGE,
							"Google server is busy, waiting to retry (%d:%02d minute)",
							"Google server is busy, waiting to retry (%d:%02d minutes)", left_minutes),
						left_minutes, left_seconds);
				} else {
					camel_operation_push_message (cancellable,
						g_dngettext (GETTEXT_PACKAGE,
							"Google server is busy, waiting to retry (%d second)",
							"Google server is busy, waiting to retry (%d seconds)", left_seconds),
						left_seconds);
				}

				e_flag_wait_until (flag, now + (G_TIME_SPAN_MILLISECOND * (wait_ms > 1000 ? 1000 : wait_ms)));
				e_flag_clear (flag);

				now = g_get_monotonic_time () - now;
				now = now / G_TIME_SPAN_MILLISECOND;

				if (now >= wait_ms)
					wait_ms = 0;
				wait_ms -= now;

				camel_operation_pop_message (cancellable);
			}

			if (handler_id)
				g_cancellable_disconnect (cancellable, handler_id);

			e_flag_free (flag);

			LOCK (self);

			self->priv->backoff_for_usec = 0;
		}

		if (g_cancellable_set_error_if_cancelled (cancellable, error)) {
			UNLOCK (self);
			return FALSE;
		}

		bytes = e_soup_session_send_message_simple_sync (E_SOUP_SESSION (self), message, cancellable, &local_error);

		success = bytes != NULL;
		is_io_error = !did_io_error_retry && g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_PARTIAL_INPUT);

		if (local_error) {
			g_propagate_error (error, local_error);
			local_error = NULL;
		}

		if (is_io_error ||
		    /* Throttling */
		    soup_message_get_status (message) == 429 ||
		    /* https://docs.microsoft.com/en-us/graph/best-practices-concept#handling-expected-errors */
		    soup_message_get_status (message) == SOUP_STATUS_SERVICE_UNAVAILABLE) {
			did_io_error_retry = did_io_error_retry || is_io_error;
			need_retry = TRUE;
		}

		if (need_retry) {
			const gchar *retry_after_str;
			gint64 retry_after;

			retry_after_str = soup_message_get_response_headers (message) ?
				soup_message_headers_get_one (soup_message_get_response_headers (message), "Retry-After") : NULL;

			if (retry_after_str && *retry_after_str)
				retry_after = g_ascii_strtoll (retry_after_str, NULL, 10);
			else if (is_io_error)
				retry_after = E_GDATA_RETRY_IO_ERROR_SECONDS;
			else
				retry_after = 0;

			if (retry_after > 0)
				need_retry_seconds = retry_after;
			else if (need_retry_seconds < 120)
				need_retry_seconds *= 2;

			if (self->priv->backoff_for_usec < need_retry_seconds * G_USEC_PER_SEC)
				self->priv->backoff_for_usec = need_retry_seconds * G_USEC_PER_SEC;

			if (soup_message_get_status (message) == SOUP_STATUS_SERVICE_UNAVAILABLE)
				soup_session_abort (SOUP_SESSION (self));

			success = FALSE;
		} else if (success && out_bytes && SOUP_STATUS_IS_SUCCESSFUL (soup_message_get_status (message))) {
			*out_bytes = bytes;
			bytes = NULL;
		} else if (success && out_node) {
			JsonNode *node = NULL;

			success = e_gdata_session_json_node_from_message (message, bytes, &node, cancellable, error);

			if (success) {
				*out_node = node;
				node = NULL;
			} else if (error && !*error && !SOUP_STATUS_IS_SUCCESSFUL (soup_message_get_status (message))) {
				g_set_error_literal (error, E_SOUP_SESSION_ERROR, soup_message_get_status (message),
					soup_message_get_reason_phrase (message) ? soup_message_get_reason_phrase (message) :
					soup_status_get_phrase (soup_message_get_status (message)));
			}

			g_clear_pointer (&node, json_node_unref);
		}

		g_clear_pointer (&bytes, g_byte_array_unref);

		UNLOCK (self);

		if (need_retry) {
			success = FALSE;
			g_clear_error (error);
		}
	}

	return success;
}

static void
e_gdata_session_set_json_body (SoupMessage *message,
			       JsonBuilder *builder)
{
	JsonGenerator *generator;
	JsonNode *node;
	gchar *data;
	gsize data_length = 0;

	g_return_if_fail (SOUP_IS_MESSAGE (message));
	g_return_if_fail (builder != NULL);

	node = json_builder_get_root (builder);

	generator = json_generator_new ();
	json_generator_set_root (generator, node);

	data = json_generator_to_data (generator, &data_length);

	soup_message_headers_set_content_type (soup_message_get_request_headers (message), "application/json", NULL);

	if (data)
		e_soup_session_util_set_message_request_body_from_data (message, FALSE, "application/json", data, data_length, g_free);

	g_object_unref (generator);
	json_node_unref (node);
}

static SoupMessage *
e_gdata_session_new_message (EGDataSession *self,
			     const gchar *method,
			     const gchar *base_url,
			     const gchar *append_path,
			     gboolean base_url_has_params,
			     EGDataQuery *query,
			     JsonBuilder *body,
			     GError **error,
			     ...) G_GNUC_NULL_TERMINATED;

/* base URI followed by param name, GType of the value and the value, terminated by NULL */
static SoupMessage *
e_gdata_session_new_message (EGDataSession *self,
			     const gchar *method,
			     const gchar *base_url,
			     const gchar *append_path,
			     gboolean base_url_has_params,
			     EGDataQuery *query,
			     JsonBuilder *body,
			     GError **error,
			     ...)
{
	GString *uri = NULL;
	GUri *guri;
	SoupMessage *message = NULL;
	va_list ap;
	const gchar *name;
	gchar separator;

	g_return_val_if_fail (base_url != NULL, NULL);

	va_start (ap, error);
	name = va_arg (ap, gchar *);
	separator = base_url_has_params ? '&' : '?';
	while (name) {
		gint type = va_arg (ap, gint);

		if (!uri) {
			uri = g_string_new (base_url);

			if (append_path) {
				g_string_append_c (uri, '/');
				g_string_append (uri, append_path);
			}
		}

		switch (type) {
			case G_TYPE_INT:
			case G_TYPE_BOOLEAN: {
				gint val = va_arg (ap, gint);
				g_string_append_printf (uri, "%c%s=%d", separator, name, val);
				break;
			}
			case G_TYPE_FLOAT:
			case G_TYPE_DOUBLE: {
				gdouble val = va_arg (ap, double);
				g_string_append_printf (uri, "%c%s=%f", separator, name, val);
				break;
			}
			case G_TYPE_STRING: {
				gchar *val = va_arg (ap, gchar *);
				gchar *escaped = g_uri_escape_string (val, NULL, FALSE);
				g_string_append_printf (uri, "%c%s=%s", separator, name, escaped);
				g_free (escaped);
				break;
			}
			case G_TYPE_POINTER: {
				gpointer val = va_arg (ap, gpointer);
				g_string_append_printf (uri, "%c%s=%p", separator, name, val);
				break;
			}
			default:
				g_warning ("%s: Unexpected param type %s", G_STRFUNC, g_type_name (type));
				g_string_free (uri, TRUE);
				va_end (ap);
				return NULL;
		}

		if (separator == '?')
			separator = '&';

		name = va_arg (ap, gchar *);
	}
	va_end (ap);

	if (!uri && append_path) {
		uri = g_string_new (base_url);
		g_string_append_c (uri, '/');
		g_string_append (uri, append_path);
	}

	if (query) {
		gchar *str;

		str = e_gdata_query_to_string (query);

		if (str && *str) {
			if (!uri)
				uri = g_string_new (base_url);
			g_string_append_c (uri, separator);
			g_string_append (uri, str);
		}

		g_free (str);
	}

	guri = g_uri_parse (uri ? uri->str : base_url, G_URI_FLAGS_PARSE_RELAXED | G_URI_FLAGS_ENCODED, error);
	if (guri) {
		message = e_soup_session_new_message_from_uri (E_SOUP_SESSION (self), method, guri, error);

		if (message && body)
			e_gdata_session_set_json_body (message, body);

		g_uri_unref (guri);
	}

	if (uri)
		g_string_free (uri, TRUE);

	return message;
}

static gboolean
e_gdata_session_handle_resource_items (EGDataSession *self,
				       const gchar *method,
				       const gchar *base_url,
				       gboolean base_url_has_params,
				       EGDataQuery *query,
				       EGDataObjectCallback cb,
				       gpointer user_data,
				       GCancellable *cancellable,
				       GError **error)
{
	gchar *next_page_token = NULL;
	gboolean success = TRUE, done = FALSE;

	g_return_val_if_fail (E_IS_GDATA_SESSION (self), FALSE);
	g_return_val_if_fail (cb != NULL, FALSE);

	while (success && !done) {
		SoupMessage *message;
		JsonNode *node = NULL;

		done = TRUE;

		message = e_gdata_session_new_message (self, method, base_url, NULL, base_url_has_params, query, NULL, error,
			next_page_token ? "pageToken" : NULL, G_TYPE_STRING, next_page_token,
			NULL);

		if (!message) {
			success = FALSE;
			break;
		}

		success = e_gdata_session_send_request_sync (self, message, &node, NULL, cancellable, error);

		if (success && node) {
			JsonObject *object;

			object = json_node_get_object (node);
			if (object) {
				const gchar *str = e_json_get_string_member (object, "nextPageToken", NULL);
				JsonArray *array;

				if (str) {
					g_free (next_page_token);
					next_page_token = g_strdup (str);
					done = FALSE;
				}

				array = e_json_get_array_member (object, "items");

				if (array) {
					guint ii, len;

					len = json_array_get_length (array);

					for (ii = 0; ii < len && !g_cancellable_is_cancelled (cancellable); ii++) {
						JsonNode *elem = json_array_get_element (array, ii);

						if (JSON_NODE_HOLDS_OBJECT (elem)) {
							JsonObject *elem_object = json_node_get_object (elem);

							if (elem_object && !cb (self, elem_object, user_data)) {
								done = TRUE;
								break;
							}
						}
					}
				} else {
					done = TRUE;
				}

				success = !g_cancellable_set_error_if_cancelled (cancellable, error);
			} else {
				g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, _("No JSON object returned by the server"));
				success = FALSE;
			}
		}

		g_clear_pointer (&node, json_node_unref);
		g_clear_object (&message);
	}

	g_free (next_page_token);

	return success;
}

static gboolean
e_gdata_object_is_kind (JsonObject *object,
			const gchar *kind)
{
	return object != NULL && kind != NULL &&
		g_strcmp0 (e_json_get_string_member (object, "kind", NULL), kind) == 0;
}

/**
 * e_gdata_tasklist_get_id:
 * @tasklist: a GData TaskList
 *
 * Returns TaskList::id property.
 *
 * Returns: (transfer none) (nullable): TaskList::id property or %NULL, when not found
 *
 * Since: 3.46
 **/
const gchar *
e_gdata_tasklist_get_id (JsonObject *tasklist)
{
	g_return_val_if_fail (e_gdata_object_is_kind (tasklist, "tasks#taskList"), NULL);

	return e_json_get_string_member (tasklist, "id", NULL);
}

/**
 * e_gdata_tasklist_add_id:
 * @builder: a #JsonBuilder with a started object member
 * @value: a TaskList::id property value
 *
 * Adds a TaskList::id property @value into the @builder, which
 * should have started an object member.
 *
 * Since: 3.46
 **/
void
e_gdata_tasklist_add_id (JsonBuilder *builder,
			 const gchar *value)
{
	g_return_if_fail (builder != NULL);
	g_return_if_fail (value != NULL);

	e_json_add_string_member (builder, "id", value);
}

/**
 * e_gdata_tasklist_get_etag:
 * @tasklist: a GData TaskList
 *
 * Returns TaskList::etag property.
 *
 * Returns: (transfer none) (nullable): TaskList::etag property or %NULL, when not found
 *
 * Since: 3.46
 **/
const gchar *
e_gdata_tasklist_get_etag (JsonObject *tasklist)
{
	g_return_val_if_fail (e_gdata_object_is_kind (tasklist, "tasks#taskList"), NULL);

	return e_json_get_string_member (tasklist, "etag", NULL);
}

/**
 * e_gdata_tasklist_get_title:
 * @tasklist: a GData TaskList
 *
 * Returns TaskList::title property.
 *
 * Returns: (transfer none) (nullable): TaskList::title property or %NULL, when not found
 *
 * Since: 3.46
 **/
const gchar *
e_gdata_tasklist_get_title (JsonObject *tasklist)
{
	g_return_val_if_fail (e_gdata_object_is_kind (tasklist, "tasks#taskList"), NULL);

	return e_json_get_string_member (tasklist, "title", NULL);
}

/**
 * e_gdata_tasklist_add_title:
 * @builder: a #JsonBuilder with a started object member
 * @value: a TaskList::title property value
 *
 * Adds a TaskList::title property @value into the @builder, which
 * should have started an object member.
 *
 * Since: 3.46
 **/
void
e_gdata_tasklist_add_title (JsonBuilder *builder,
			    const gchar *value)
{
	g_return_if_fail (builder != NULL);
	g_return_if_fail (value != NULL);

	e_json_add_string_member (builder, "title", value);
}

/**
 * e_gdata_tasklist_get_self_link:
 * @tasklist: a GData TaskList
 *
 * Returns TaskList::selfLink property.
 *
 * Returns: (transfer none) (nullable): TaskList::selfLink property or %NULL, when not found
 *
 * Since: 3.46
 **/
const gchar *
e_gdata_tasklist_get_self_link (JsonObject *tasklist)
{
	g_return_val_if_fail (e_gdata_object_is_kind (tasklist, "tasks#taskList"), NULL);

	return e_json_get_string_member (tasklist, "selfLink", NULL);
}

/**
 * e_gdata_tasklist_get_updated:
 * @tasklist: a GData TaskList
 *
 * Returns TaskList::updated property, as Unix time.
 *
 * Returns: TaskList::updated property or 0, when not found
 *
 * Since: 3.46
 **/
gint64
e_gdata_tasklist_get_updated (JsonObject *tasklist)
{
	g_return_val_if_fail (e_gdata_object_is_kind (tasklist, "tasks#taskList"), 0);

	return e_json_get_iso8601_member (tasklist, "updated", 0);
}

/**
 * e_gdata_session_tasklists_delete_sync:
 * @self: an #EGDataSession
 * @tasklist_id: id of a task list
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Deletes a task list @tasklist_id.
 *
 * Returns: whether succeeded
 *
 * Since: 3.46
 **/
gboolean
e_gdata_session_tasklists_delete_sync (EGDataSession *self,
				       const gchar *tasklist_id,
				       GCancellable *cancellable,
				       GError **error)
{
	SoupMessage *message;
	gboolean success;

	g_return_val_if_fail (E_IS_GDATA_SESSION (self), FALSE);
	g_return_val_if_fail (tasklist_id != NULL, FALSE);

	message = e_gdata_session_new_message (self, SOUP_METHOD_DELETE, TASKLISTS_TOP_URL, tasklist_id, FALSE, NULL, NULL, error, NULL);

	if (!message)
		return FALSE;

	success = e_gdata_session_send_request_sync (self, message, NULL, NULL, cancellable, error);

	g_clear_object (&message);

	g_prefix_error (error, _("Failed to call %s: "), "tasklists::delete");

	return success;
}

/**
 * e_gdata_session_tasklists_get_sync:
 * @self: an #EGDataSession
 * @tasklist_id: id of a task list
 * @out_tasklist: (out) (transfer full): tasklist object
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Gets a task list @tasklist_id and returns it as a #JsonObject,
 * which should be freed with json_object_unref(), when no longer needed.
 *
 * There can be used e_gdata_tasklist_get_id(), e_gdata_tasklist_get_etag(),
 * e_gdata_tasklist_get_title(), e_gdata_tasklist_get_self_link(),
 * e_gdata_tasklist_get_updated() to read the properties of the task list.
 *
 * Returns: whether succeeded
 *
 * Since: 3.46
 **/
gboolean
e_gdata_session_tasklists_get_sync (EGDataSession *self,
				    const gchar *tasklist_id,
				    JsonObject **out_tasklist,
				    GCancellable *cancellable,
				    GError **error)
{
	SoupMessage *message;
	JsonNode *node = NULL;
	gboolean success;

	g_return_val_if_fail (E_IS_GDATA_SESSION (self), FALSE);
	g_return_val_if_fail (tasklist_id != NULL, FALSE);
	g_return_val_if_fail (out_tasklist != NULL, FALSE);

	*out_tasklist = NULL;

	message = e_gdata_session_new_message (self, SOUP_METHOD_GET, TASKLISTS_TOP_URL, tasklist_id, FALSE, NULL, NULL, error, NULL);

	if (!message)
		return FALSE;

	success = e_gdata_session_send_request_sync (self, message, &node, NULL, cancellable, error);

	if (success && node) {
		JsonObject *object;

		object = json_node_get_object (node);
		if (object)
			*out_tasklist = json_object_ref (object);
	}

	g_clear_object (&message);
	g_clear_pointer (&node, json_node_unref);

	g_prefix_error (error, _("Failed to call %s: "), "tasklists::get");

	return success;
}

/**
 * e_gdata_session_tasklists_insert_sync:
 * @self: an #EGDataSession
 * @title: title to set
 * @out_inserted_tasklist: (out) (transfer full): the created task list
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Creates a new task list, titled @title. The @out_inserted_tasklist should
 * be freed with json_object_unref(), when no longer needed.
 *
 * Returns: whether succeeded
 *
 * Since: 3.46
 **/
gboolean
e_gdata_session_tasklists_insert_sync (EGDataSession *self,
				       const gchar *title,
				       JsonObject **out_inserted_tasklist,
				       GCancellable *cancellable,
				       GError **error)
{
	SoupMessage *message;
	JsonBuilder *builder;
	JsonNode *node = NULL;
	gboolean success;

	g_return_val_if_fail (E_IS_GDATA_SESSION (self), FALSE);
	g_return_val_if_fail (title != NULL, FALSE);
	g_return_val_if_fail (out_inserted_tasklist != NULL, FALSE);

	*out_inserted_tasklist = NULL;

	builder = json_builder_new_immutable ();

	e_json_begin_object_member (builder, NULL);
	e_json_add_string_member (builder, "title", title);
	e_json_end_object_member (builder);

	message = e_gdata_session_new_message (self, SOUP_METHOD_POST, TASKLISTS_TOP_URL, NULL, FALSE, NULL, builder, error, NULL);

	g_clear_object (&builder);

	if (!message)
		return FALSE;

	success = e_gdata_session_send_request_sync (self, message, &node, NULL, cancellable, error);

	if (success && node) {
		JsonObject *object;

		object = json_node_get_object (node);
		if (object)
			*out_inserted_tasklist = json_object_ref (object);
	}

	g_clear_pointer (&node, json_node_unref);
	g_clear_object (&message);

	g_prefix_error (error, _("Failed to call %s: "), "tasklists::insert");

	return success;
}

/**
 * e_gdata_session_tasklists_list_sync:
 * @self: an #EGDataSession
 * @query: (nullable): an #EGDataQuery to limit returned task lists, or %NULL
 * @cb: (scope call): an #EGDataObjectCallback to call for each found task list
 * @user_data: (closure cb): user data passed to the @cb
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Lists all configured task lists for the user, calling the @cb for each of them.
 *
 * Returns: whether succeeded
 *
 * Since: 3.46
 **/
gboolean
e_gdata_session_tasklists_list_sync (EGDataSession *self,
				     EGDataQuery *query,
				     EGDataObjectCallback cb,
				     gpointer user_data,
				     GCancellable *cancellable,
				     GError **error)
{
	gboolean success;

	g_return_val_if_fail (E_IS_GDATA_SESSION (self), FALSE);
	g_return_val_if_fail (cb != NULL, FALSE);

	success = e_gdata_session_handle_resource_items (self, SOUP_METHOD_GET, TASKLISTS_TOP_URL, FALSE,
		query, cb, user_data, cancellable, error);

	g_prefix_error (error, _("Failed to call %s: "), "tasklists::list");

	return success;
}

/**
 * e_gdata_session_tasklists_patch_sync:
 * @self: an #EGDataSession
 * @tasklist_id: id of a task list
 * @tasklist_properties: task list properties to change
 * @out_patched_tasklist: (out) (optional) (transfer full): where to store patched task list, or %NULL
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Changes properties of a task list @tasklist_id.
 *
 * If not %NULL, free the @out_patched_tasklist with json_object_unref(),
 * when no longer needed.
 *
 * Returns: whether succeeded
 *
 * Since: 3.46
 **/
gboolean
e_gdata_session_tasklists_patch_sync (EGDataSession *self,
				      const gchar *tasklist_id,
				      JsonBuilder *tasklist_properties,
				      JsonObject **out_patched_tasklist,
				      GCancellable *cancellable,
				      GError **error)
{
	SoupMessage *message;
	JsonNode *node = NULL;
	gboolean success;

	g_return_val_if_fail (E_IS_GDATA_SESSION (self), FALSE);
	g_return_val_if_fail (tasklist_id != NULL, FALSE);
	g_return_val_if_fail (tasklist_properties != NULL, FALSE);

	message = e_gdata_session_new_message (self, "PATCH", TASKLISTS_TOP_URL, tasklist_id, FALSE, NULL, tasklist_properties, error, NULL);

	if (!message)
		return FALSE;

	success = e_gdata_session_send_request_sync (self, message, out_patched_tasklist ? &node : NULL, NULL, cancellable, error);

	if (success && node && out_patched_tasklist) {
		JsonObject *object;

		object = json_node_get_object (node);
		if (object)
			*out_patched_tasklist = json_object_ref (object);
	}

	g_clear_pointer (&node, json_node_unref);
	g_clear_object (&message);

	g_prefix_error (error, _("Failed to call %s: "), "tasklists::patch");

	return success;
}

/**
 * e_gdata_session_tasklists_update_sync:
 * @self: an #EGDataSession
 * @tasklist_id: id of a task list
 * @tasklist: task list object to update the task list with
 * @out_updated_tasklist: (out) (optional) (transfer full): where to store updated task list, or %NULL
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Updates a task list @tasklist_id with values from the @tasklist.
 *
 * Returns: whether succeeded
 *
 * Since: 3.46
 **/
gboolean
e_gdata_session_tasklists_update_sync (EGDataSession *self,
				       const gchar *tasklist_id,
				       JsonBuilder *tasklist,
				       JsonObject **out_updated_tasklist,
				       GCancellable *cancellable,
				       GError **error)
{
	SoupMessage *message;
	JsonNode *node = NULL;
	gboolean success;

	g_return_val_if_fail (E_IS_GDATA_SESSION (self), FALSE);
	g_return_val_if_fail (tasklist_id != NULL, FALSE);
	g_return_val_if_fail (tasklist != NULL, FALSE);

	message = e_gdata_session_new_message (self, SOUP_METHOD_PUT, TASKLISTS_TOP_URL, tasklist_id, FALSE, NULL, tasklist, error, NULL);

	if (!message)
		return FALSE;

	success = e_gdata_session_send_request_sync (self, message, out_updated_tasklist ? &node : NULL, NULL, cancellable, error);

	if (success && node && out_updated_tasklist) {
		JsonObject *object;

		object = json_node_get_object (node);
		if (object)
			*out_updated_tasklist = json_object_ref (object);
	}

	g_clear_pointer (&node, json_node_unref);
	g_clear_object (&message);

	g_prefix_error (error, _("Failed to call %s: "), "tasklists::update");

	return success;
}

/**
 * e_gdata_task_get_id:
 * @task: a GData Task
 *
 * Returns Task::id property.
 *
 * Returns: (transfer none) (nullable): Task::id property or %NULL, when not found
 *
 * Since: 3.46
 **/
const gchar *
e_gdata_task_get_id (JsonObject *task)
{
	g_return_val_if_fail (e_gdata_object_is_kind (task, "tasks#task"), NULL);

	return e_json_get_string_member (task, "id", NULL);
}

/**
 * e_gdata_task_add_id:
 * @builder: a #JsonBuilder with a started object member
 * @value: a Task::id property value
 *
 * Adds a Task::id property @value into the @builder, which
 * should have started an object member.
 *
 * Since: 3.46
 **/
void
e_gdata_task_add_id (JsonBuilder *builder,
		     const gchar *value)
{
	g_return_if_fail (builder != NULL);
	g_return_if_fail (value != NULL);

	e_json_add_string_member (builder, "id", value);
}

/**
 * e_gdata_task_get_etag:
 * @task: a GData Task
 *
 * Returns Task::etag property.
 *
 * Returns: (transfer none) (nullable): Task::etag property or %NULL, when not found
 *
 * Since: 3.46
 **/
const gchar *
e_gdata_task_get_etag (JsonObject *task)
{
	g_return_val_if_fail (e_gdata_object_is_kind (task, "tasks#task"), NULL);

	return e_json_get_string_member (task, "etag", NULL);
}

/**
 * e_gdata_task_get_title:
 * @task: a GData Task
 *
 * Returns Task::title property.
 *
 * Returns: (transfer none) (nullable): Task::title property or %NULL, when not found
 *
 * Since: 3.46
 **/
const gchar *
e_gdata_task_get_title (JsonObject *task)
{
	g_return_val_if_fail (e_gdata_object_is_kind (task, "tasks#task"), NULL);

	return e_json_get_string_member (task, "title", NULL);
}

/**
 * e_gdata_task_add_title:
 * @builder: a #JsonBuilder with a started object member
 * @value: a Task::title property value
 *
 * Adds a Task::title property @value into the @builder, which
 * should have started an object member.
 *
 * Since: 3.46
 **/
void
e_gdata_task_add_title (JsonBuilder *builder,
			const gchar *value)
{
	g_return_if_fail (builder != NULL);
	g_return_if_fail (value != NULL);

	e_json_add_string_member (builder, "title", value);
}

/**
 * e_gdata_task_get_updated:
 * @task: a GData Task
 *
 * Returns Task::updated property, as Unix time.
 *
 * Returns: Task::updated property or 0, when not found
 *
 * Since: 3.46
 **/
gint64
e_gdata_task_get_updated (JsonObject *task)
{
	g_return_val_if_fail (e_gdata_object_is_kind (task, "tasks#task"), 0);

	return e_json_get_iso8601_member (task, "updated", 0);
}

/**
 * e_gdata_task_get_self_link:
 * @task: a GData TaskList
 *
 * Returns Task::selfLink property.
 *
 * Returns: (transfer none) (nullable): Task::selfLink property or %NULL, when not found
 *
 * Since: 3.46
 **/
const gchar *
e_gdata_task_get_self_link (JsonObject *task)
{
	g_return_val_if_fail (e_gdata_object_is_kind (task, "tasks#task"), NULL);

	return e_json_get_string_member (task, "selfLink", NULL);
}

/**
 * e_gdata_task_get_parent:
 * @task: a GData Task
 *
 * Returns Task::parent property.
 *
 * Returns: (transfer none) (nullable): Task::parent property or %NULL, when not found
 *
 * Since: 3.46
 **/
const gchar *
e_gdata_task_get_parent (JsonObject *task)
{
	g_return_val_if_fail (e_gdata_object_is_kind (task, "tasks#task"), NULL);

	return e_json_get_string_member (task, "parent", NULL);
}

/**
 * e_gdata_task_get_position:
 * @task: a GData Task
 *
 * Returns Task::position property.
 *
 * Returns: (transfer none) (nullable): Task::position property or %NULL, when not found
 *
 * Since: 3.46
 **/
const gchar *
e_gdata_task_get_position (JsonObject *task)
{
	g_return_val_if_fail (e_gdata_object_is_kind (task, "tasks#task"), NULL);

	return e_json_get_string_member (task, "position", NULL);
}

/**
 * e_gdata_task_get_notes:
 * @task: a GData Task
 *
 * Returns Task::notes property.
 *
 * Returns: (transfer none) (nullable): Task::notes property or %NULL, when not found
 *
 * Since: 3.46
 **/
const gchar *
e_gdata_task_get_notes (JsonObject *task)
{
	g_return_val_if_fail (e_gdata_object_is_kind (task, "tasks#task"), NULL);

	return e_json_get_string_member (task, "notes", NULL);
}

/**
 * e_gdata_task_add_notes:
 * @builder: a #JsonBuilder with a started object member
 * @value: (nullable): a Task::notes property value
 *
 * Adds a Task::notes property @value into the @builder, which
 * should have started an object member.
 *
 * When the @value is %NULL, then adds a NULL-object, to indicate removal
 * of the property.
 *
 * Since: 3.46
 **/
void
e_gdata_task_add_notes (JsonBuilder *builder,
			const gchar *value)
{
	g_return_if_fail (builder != NULL);

	if (value)
		e_json_add_string_member (builder, "notes", value);
	else
		e_json_add_null_member (builder, "notes");
}

/**
 * e_gdata_task_get_status:
 * @task: a GData Task
 *
 * Returns Task::status property.
 *
 * Returns: Task::status property as #EGDataTaskStatus or %E_GDATA_TASK_STATUS_UNKNOWN,
 *    when not found or has set an unknown value.
 *
 * Since: 3.46
 **/
EGDataTaskStatus
e_gdata_task_get_status (JsonObject *task)
{
	const gchar *str;

	g_return_val_if_fail (e_gdata_object_is_kind (task, "tasks#task"), E_GDATA_TASK_STATUS_UNKNOWN);

	str = e_json_get_string_member (task, "status", NULL);

	if (g_strcmp0 (str, "needsAction") == 0)
		return E_GDATA_TASK_STATUS_NEEDS_ACTION;

	if (g_strcmp0 (str, "completed") == 0)
		return E_GDATA_TASK_STATUS_COMPLETED;

	return E_GDATA_TASK_STATUS_UNKNOWN;
}

/**
 * e_gdata_task_add_status:
 * @builder: a #JsonBuilder with a started object member
 * @value: a Task::status property value
 *
 * Adds a Task::status property @value into the @builder, which
 * should have started an object member.
 *
 * When the @value is %E_GDATA_TASK_STATUS_UNKNOWN, then adds a NULL-object,
 * to indicate removal of the property.
 *
 * Since: 3.46
 **/
void
e_gdata_task_add_status (JsonBuilder *builder,
			 EGDataTaskStatus value)
{
	g_return_if_fail (builder != NULL);

	switch (value) {
	case E_GDATA_TASK_STATUS_UNKNOWN:
		e_json_add_null_member (builder, "status");
		break;
	case E_GDATA_TASK_STATUS_NEEDS_ACTION:
		e_json_add_string_member (builder, "status", "needsAction");
		break;
	case E_GDATA_TASK_STATUS_COMPLETED:
		e_json_add_string_member (builder, "status", "completed");
		break;
	}
}

/**
 * e_gdata_task_get_due:
 * @task: a GData Task
 *
 * Returns Task::due property, as Unix time.
 *
 * Returns: Task::due property or 0, when not found
 *
 * Since: 3.46
 **/
gint64
e_gdata_task_get_due (JsonObject *task)
{
	g_return_val_if_fail (e_gdata_object_is_kind (task, "tasks#task"), 0);

	return e_json_get_iso8601_member (task, "due", 0);
}

/**
 * e_gdata_task_add_due:
 * @builder: a #JsonBuilder with a started object member
 * @value: a Task::due property value, as Unix time
 *
 * Adds a Task::due property @value into the @builder, which
 * should have started an object member.
 *
 * When the @value is 0, then adds a NULL-object, to indicate
 * removal of the property.
 *
 * Since: 3.46
 **/
void
e_gdata_task_add_due (JsonBuilder *builder,
		      gint64 value)
{
	g_return_if_fail (builder != NULL);

	if (value != 0)
		e_json_add_iso8601_member (builder, "due", value);
	else
		e_json_add_null_member (builder, "due");
}

/**
 * e_gdata_task_get_completed:
 * @task: a GData Task
 *
 * Returns Task::completed property, as Unix time.
 *
 * Returns: Task::completed property or 0, when not found
 *
 * Since: 3.46
 **/
gint64
e_gdata_task_get_completed (JsonObject *task)
{
	g_return_val_if_fail (e_gdata_object_is_kind (task, "tasks#task"), 0);

	return e_json_get_iso8601_member (task, "completed", 0);
}

/**
 * e_gdata_task_add_completed:
 * @builder: a #JsonBuilder with a started object member
 * @value: a Task::completed property value, as Unix time
 *
 * Adds a Task:completed property @value into the @builder, which
 * should have started an object member.
 *
 * When the @value is 0, then adds a NULL-object, to indicate
 * removal of the property.
 *
 * Since: 3.46
 **/
void
e_gdata_task_add_completed (JsonBuilder *builder,
			    gint64 value)
{
	g_return_if_fail (builder != NULL);

	if (value != 0)
		e_json_add_iso8601_member (builder, "completed", value);
	else
		e_json_add_null_member (builder, "completed");
}

/**
 * e_gdata_task_get_deleted:
 * @task: a GData Task
 *
 * Returns Task::deleted property, as Unix time.
 *
 * Returns: Task::deleted property or %FALSE, when not found
 *
 * Since: 3.46
 **/
gboolean
e_gdata_task_get_deleted (JsonObject *task)
{
	g_return_val_if_fail (e_gdata_object_is_kind (task, "tasks#task"), FALSE);

	return e_json_get_boolean_member (task, "deleted", FALSE);
}

/**
 * e_gdata_task_get_hidden:
 * @task: a GData Task
 *
 * Returns Task::hidden property, as Unix time.
 *
 * Returns: Task::hidden property or %FALSE, when not found
 *
 * Since: 3.46
 **/
gboolean
e_gdata_task_get_hidden (JsonObject *task)
{
	g_return_val_if_fail (e_gdata_object_is_kind (task, "tasks#task"), FALSE);

	return e_json_get_boolean_member (task, "hidden", FALSE);
}

/**
 * e_gdata_session_tasks_clear_sync:
 * @self: an #EGDataSession
 * @tasklist_id: id of a task list
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Clears all completed tasks from the task list @tasklist_id. The affected tasks
 * will be marked as 'hidden' and no longer be returned by default when retrieving
 * all tasks for a task list.
 *
 * Returns: whether succeeded
 *
 * Since: 3.46
 **/
gboolean
e_gdata_session_tasks_clear_sync (EGDataSession *self,
				  const gchar *tasklist_id,
				  GCancellable *cancellable,
				  GError **error)
{
	SoupMessage *message;
	gboolean success;
	gchar *path;

	g_return_val_if_fail (E_IS_GDATA_SESSION (self), FALSE);
	g_return_val_if_fail (tasklist_id != NULL, FALSE);

	path = g_strconcat (tasklist_id, "/clear", NULL);
	message = e_gdata_session_new_message (self, SOUP_METHOD_POST, TASKS_TOP_URL, path, FALSE, NULL, NULL, error, NULL);
	g_free (path);

	if (!message)
		return FALSE;

	success = e_gdata_session_send_request_sync (self, message, NULL, NULL, cancellable, error);

	g_clear_object (&message);

	g_prefix_error (error, _("Failed to call %s: "), "tasks::clear");

	return success;
}

/**
 * e_gdata_session_tasks_delete_sync:
 * @self: an #EGDataSession
 * @tasklist_id: id of a task list
 * @task_id: id of a task
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Deletes a task @task_id from a task list @tasklist_id.
 *
 * Returns: whether succeeded
 *
 * Since: 3.46
 **/
gboolean
e_gdata_session_tasks_delete_sync (EGDataSession *self,
				   const gchar *tasklist_id,
				   const gchar *task_id,
				   GCancellable *cancellable,
				   GError **error)
{
	SoupMessage *message;
	gboolean success;
	gchar *path;

	g_return_val_if_fail (E_IS_GDATA_SESSION (self), FALSE);
	g_return_val_if_fail (tasklist_id != NULL, FALSE);

	path = g_strconcat (tasklist_id, "/tasks/", task_id, NULL);
	message = e_gdata_session_new_message (self, SOUP_METHOD_DELETE, TASKS_TOP_URL, path, FALSE, NULL, NULL, error, NULL);
	g_free (path);

	if (!message)
		return FALSE;

	success = e_gdata_session_send_request_sync (self, message, NULL, NULL, cancellable, error);

	g_clear_object (&message);

	g_prefix_error (error, _("Failed to call %s: "), "tasks::delete");

	return success;
}

/**
 * e_gdata_session_tasks_get_sync:
 * @self: an #EGDataSession
 * @tasklist_id: id of a task list
 * @task_id: id of a task
 * @out_task: (out) (transfer full): task object
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Gets a task @task_id from a task list @tasklist_id and returns it as a #JsonObject,
 * which should be freed with json_object_unref(), when no longer needed.
 *
 * There can be used e_gdata_task_get_id(), e_gdata_task_get_etag(),
 * e_gdata_task_get_title() and other e_gdata_task_... functions
 * to read the properties of the task.
 *
 * Returns: whether succeeded
 *
 * Since: 3.46
 **/
gboolean
e_gdata_session_tasks_get_sync (EGDataSession *self,
				const gchar *tasklist_id,
				const gchar *task_id,
				JsonObject **out_task,
				GCancellable *cancellable,
				GError **error)
{
	SoupMessage *message;
	JsonNode *node = NULL;
	gboolean success;
	gchar *path;

	g_return_val_if_fail (E_IS_GDATA_SESSION (self), FALSE);
	g_return_val_if_fail (tasklist_id != NULL, FALSE);
	g_return_val_if_fail (task_id != NULL, FALSE);
	g_return_val_if_fail (out_task != NULL, FALSE);

	*out_task = NULL;

	path = g_strconcat (tasklist_id, "/tasks/", task_id, NULL);
	message = e_gdata_session_new_message (self, SOUP_METHOD_GET, TASKS_TOP_URL, path, FALSE, NULL, NULL, error, NULL);
	g_free (path);

	if (!message)
		return FALSE;

	success = e_gdata_session_send_request_sync (self, message, &node, NULL, cancellable, error);

	if (success && node) {
		JsonObject *object;

		object = json_node_get_object (node);
		if (object)
			*out_task = json_object_ref (object);
	}

	g_clear_object (&message);
	g_clear_pointer (&node, json_node_unref);

	g_prefix_error (error, _("Failed to call %s: "), "tasks::get");

	return success;
}

/**
 * e_gdata_session_tasks_insert_sync:
 * @self: an #EGDataSession
 * @tasklist_id: id of a task list
 * @task: a #JsonBuilder with the task object
 * @parent_task_id: (nullable): parent task identifier, or %NULL to create at the top-level
 * @previous_task_id: (nullable): previous sibling task identifier, or %NULL to create at the first position among its siblings
 * @out_inserted_task: (out) (transfer full): the created task
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Creates a new task @task in the task list @tasklist_id. The @out_inserted_task should
 * be freed with json_object_unref(), when no longer needed.
 *
 * Returns: whether succeeded
 *
 * Since: 3.46
 **/
gboolean
e_gdata_session_tasks_insert_sync (EGDataSession *self,
				   const gchar *tasklist_id,
				   JsonBuilder *task,
				   const gchar *parent_task_id,
				   const gchar *previous_task_id,
				   JsonObject **out_inserted_task,
				   GCancellable *cancellable,
				   GError **error)
{
	SoupMessage *message;
	JsonNode *node = NULL;
	gboolean success;
	gchar *path;

	g_return_val_if_fail (E_IS_GDATA_SESSION (self), FALSE);
	g_return_val_if_fail (tasklist_id != NULL, FALSE);
	g_return_val_if_fail (task != NULL, FALSE);
	g_return_val_if_fail (out_inserted_task != NULL, FALSE);

	*out_inserted_task = NULL;

	path = g_strconcat (tasklist_id, "/tasks", NULL);
	message = e_gdata_session_new_message (self, SOUP_METHOD_POST, TASKS_TOP_URL, path, FALSE, NULL, task, error,
		parent_task_id ? "parent" : previous_task_id ? "previous" : NULL,
		G_TYPE_STRING,
		parent_task_id ? parent_task_id : previous_task_id ? previous_task_id : NULL,
		parent_task_id && previous_task_id ? "previous" : NULL,
		G_TYPE_STRING,
		previous_task_id,
		NULL);
	g_free (path);

	if (!message)
		return FALSE;

	success = e_gdata_session_send_request_sync (self, message, &node, NULL, cancellable, error);

	if (success && node) {
		JsonObject *object;

		object = json_node_get_object (node);
		if (object)
			*out_inserted_task = json_object_ref (object);
	}

	g_clear_object (&message);
	g_clear_pointer (&node, json_node_unref);

	g_prefix_error (error, _("Failed to call %s: "), "tasks::insert");

	return success;
}

/**
 * e_gdata_session_tasks_list_sync:
 * @self: an #EGDataSession
 * @tasklist_id: id of a task list
 * @query: (nullable): an #EGDataQuery to limit returned tasks, or %NULL
 * @cb: (scope call): an #EGDataObjectCallback to call for each found task
 * @user_data: (closure cb): user data passed to the @cb
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Lists all tasks in the task list @tasklist_id, calling the @cb for each of them.
 *
 * Returns: whether succeeded
 *
 * Since: 3.46
 **/
gboolean
e_gdata_session_tasks_list_sync (EGDataSession *self,
				 const gchar *tasklist_id,
				 EGDataQuery *query,
				 EGDataObjectCallback cb,
				 gpointer user_data,
				 GCancellable *cancellable,
				 GError **error)
{
	gboolean success;
	gchar *base_url;

	g_return_val_if_fail (E_IS_GDATA_SESSION (self), FALSE);
	g_return_val_if_fail (cb != NULL, FALSE);

	base_url = g_strconcat (TASKS_TOP_URL, "/", tasklist_id, "/tasks", NULL);
	success = e_gdata_session_handle_resource_items (self, SOUP_METHOD_GET, base_url, FALSE,
		query, cb, user_data, cancellable, error);
	g_free (base_url);

	g_prefix_error (error, _("Failed to call %s: "), "tasks::list");

	return success;
}

/**
 * e_gdata_session_tasks_move_sync:
 * @self: an #EGDataSession
 * @tasklist_id: id of a task list
 * @task_id: id of a task
 * @parent_task_id: (nullable): parent task identifier, or %NULL to move at the top-level
 * @previous_task_id: (nullable): previous sibling task identifier, or %NULL to move at the first position among its siblings
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Moves the specified task @task_id to another position in the task
 * list @tasklist_id. This can include putting it as a child task under
 * a new parent and/or move it to a different position among its sibling tasks.
 *
 * Returns: whether succeeded
 *
 * Since: 3.46
 **/
gboolean
e_gdata_session_tasks_move_sync (EGDataSession *self,
				 const gchar *tasklist_id,
				 const gchar *task_id,
				 const gchar *parent_task_id,
				 const gchar *previous_task_id,
				 GCancellable *cancellable,
				 GError **error)
{
	SoupMessage *message;
	gboolean success;
	gchar *path;

	g_return_val_if_fail (E_IS_GDATA_SESSION (self), FALSE);
	g_return_val_if_fail (tasklist_id != NULL, FALSE);
	g_return_val_if_fail (task_id != NULL, FALSE);

	path = g_strconcat (tasklist_id, "/tasks/", task_id, "/move", NULL);
	message = e_gdata_session_new_message (self, SOUP_METHOD_POST, TASKS_TOP_URL, path, FALSE, NULL, NULL, error,
		parent_task_id ? "parent" : previous_task_id ? "previous" : NULL,
		G_TYPE_STRING,
		parent_task_id ? parent_task_id : previous_task_id ? previous_task_id : NULL,
		parent_task_id && previous_task_id ? "previous" : NULL,
		G_TYPE_STRING,
		previous_task_id,
		NULL);
	g_free (path);

	if (!message)
		return FALSE;

	success = e_gdata_session_send_request_sync (self, message, NULL, NULL, cancellable, error);

	g_clear_object (&message);

	g_prefix_error (error, _("Failed to call %s: "), "tasks::move");

	return success;
}

/**
 * e_gdata_session_tasks_patch_sync:
 * @self: an #EGDataSession
 * @tasklist_id: id of a task list
 * @task_id: id of a task
 * @task_properties: task properties to change
 * @out_patched_task: (out) (optional) (transfer full): where to set patches task, or %NULL
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Changes properties @task_properties of a task @task_id in the task list @tasklist_id.
 *
 * If not %NULL, free the @out_patched_task with json_object_unref(),
 * when no longer needed.
 *
 * Returns: whether succeeded
 *
 * Since: 3.46
 **/
gboolean
e_gdata_session_tasks_patch_sync (EGDataSession *self,
				  const gchar *tasklist_id,
				  const gchar *task_id,
				  JsonBuilder *task_properties,
				  JsonObject **out_patched_task,
				  GCancellable *cancellable,
				  GError **error)
{
	SoupMessage *message;
	JsonNode *node = NULL;
	gboolean success;
	gchar *path;

	g_return_val_if_fail (E_IS_GDATA_SESSION (self), FALSE);
	g_return_val_if_fail (tasklist_id != NULL, FALSE);
	g_return_val_if_fail (task_id != NULL, FALSE);
	g_return_val_if_fail (task_properties != NULL, FALSE);

	path = g_strconcat (tasklist_id, "/tasks/", task_id, NULL);
	message = e_gdata_session_new_message (self, "PATCH", TASKS_TOP_URL, path, FALSE, NULL, task_properties, error, NULL);
	g_free (path);

	if (!message)
		return FALSE;

	success = e_gdata_session_send_request_sync (self, message, out_patched_task ? &node : NULL, NULL, cancellable, error);

	if (success && node && out_patched_task) {
		JsonObject *object;

		object = json_node_get_object (node);
		if (object)
			*out_patched_task = json_object_ref (object);
	}

	g_clear_pointer (&node, json_node_unref);
	g_clear_object (&message);

	g_prefix_error (error, _("Failed to call %s: "), "tasks::patch");

	return success;
}

/**
 * e_gdata_session_tasks_update_sync:
 * @self: an #EGDataSession
 * @tasklist_id: id of a task list
 * @task_id: id of a task
 * @task: task object to update the task with
 * @out_updated_task: (out) (optional) (transfer full): where to store updated task, or %NULL
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Updates a task @task_id in a task list @tasklist_id to the values from the @task.
 *
 * Returns: whether succeeded
 *
 * Since: 3.46
 **/
gboolean
e_gdata_session_tasks_update_sync (EGDataSession *self,
				   const gchar *tasklist_id,
				   const gchar *task_id,
				   JsonBuilder *task,
				   JsonObject **out_updated_task,
				   GCancellable *cancellable,
				   GError **error)
{
	SoupMessage *message;
	JsonNode *node = NULL;
	gboolean success;
	gchar *path;

	g_return_val_if_fail (E_IS_GDATA_SESSION (self), FALSE);
	g_return_val_if_fail (tasklist_id != NULL, FALSE);
	g_return_val_if_fail (task_id != NULL, FALSE);
	g_return_val_if_fail (task != NULL, FALSE);

	path = g_strconcat (tasklist_id, "/tasks/", task_id, NULL);
	message = e_gdata_session_new_message (self, SOUP_METHOD_PUT, TASKS_TOP_URL, path, FALSE, NULL, task, error, NULL);
	g_free (path);

	if (!message)
		return FALSE;

	success = e_gdata_session_send_request_sync (self, message, out_updated_task ? &node : NULL, NULL, cancellable, error);

	if (success && node && out_updated_task) {
		JsonObject *object;

		object = json_node_get_object (node);
		if (object)
			*out_updated_task = json_object_ref (object);
	}

	g_clear_pointer (&node, json_node_unref);
	g_clear_object (&message);

	g_prefix_error (error, _("Failed to call %s: "), "tasks::update");

	return success;
}
