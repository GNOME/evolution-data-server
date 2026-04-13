/*
 * SPDX-FileCopyrightText: 2026 Collabora, Ltd (https://collabora.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "evolution-data-server-config.h"

#include <glib/gi18n-lib.h>
#include <json-glib/json-glib.h>

#include "camel-jmap-folder.h"
#include "camel-jmap-store.h"

struct _CamelJmapFolder {
	CamelFolder parent;

	gchar *mailbox_id;
};

G_DEFINE_TYPE (
	CamelJmapFolder,
	camel_jmap_folder,
	CAMEL_TYPE_FOLDER)

/* Converts a JMAP Email object to a CamelMessageInfo. */
static CamelMessageInfo *
jmap_email_to_message_info (CamelFolder *folder,
                            JsonObject  *email_obj)
{
	CamelMessageInfo *info;
	const gchar *uid, *subject, *date_str;
	gint64 received_at;
	guint32 flags = 0;
	JsonObject *keywords;

	uid = json_object_get_string_member_with_default (email_obj, "id", NULL);
	if (!uid)
		return NULL;

	info = camel_message_info_new (NULL);
	camel_message_info_set_uid (info, uid);

	subject = json_object_get_string_member_with_default (email_obj, "subject", NULL);
	camel_message_info_set_subject (info, subject);

	received_at = json_object_get_int_member_with_default (email_obj, "receivedAt", 0);
	if (received_at > 0)
		camel_message_info_set_date_received (info, (gint64) received_at);

	date_str = json_object_get_string_member_with_default (email_obj, "sentAt", NULL);
	if (date_str) {
		GDateTime *dt;
		dt = g_date_time_new_from_iso8601 (date_str, NULL);
		if (dt) {
			camel_message_info_set_date_sent (info, (gint64) g_date_time_to_unix (dt));
			g_date_time_unref (dt);
		}
	}

	camel_message_info_set_size (info,
		(guint32) json_object_get_int_member_with_default (email_obj, "size", 0));

	/* Map JMAP keywords to Camel flags */
	keywords = json_object_get_object_member (email_obj, "keywords");
	if (keywords) {
		if (json_object_has_member (keywords, "$seen"))
			flags |= CAMEL_MESSAGE_SEEN;
		if (json_object_has_member (keywords, "$flagged"))
			flags |= CAMEL_MESSAGE_FLAGGED;
		if (json_object_has_member (keywords, "$answered"))
			flags |= CAMEL_MESSAGE_ANSWERED;
		if (json_object_has_member (keywords, "$draft"))
			flags |= CAMEL_MESSAGE_DRAFT;
	} else {
		/* Assume unread if no keywords */
	}
	camel_message_info_set_flags (info, flags, flags);

	/* Parse From: address */
	if (json_object_has_member (email_obj, "from")) {
		JsonArray *from_arr;
		from_arr = json_object_get_array_member (email_obj, "from");
		if (from_arr && json_array_get_length (from_arr) > 0) {
			JsonObject *addr_obj;
			const gchar *name, *email_addr;
			gchar *from_str;

			addr_obj = json_array_get_object_element (from_arr, 0);
			name = json_object_get_string_member_with_default (addr_obj, "name", NULL);
			email_addr = json_object_get_string_member_with_default (addr_obj, "email", "");

			if (name && *name)
				from_str = g_strdup_printf ("%s <%s>", name, email_addr);
			else
				from_str = g_strdup (email_addr);

			camel_message_info_set_from (info, from_str);
			g_free (from_str);
		}
	}

	return info;
}

/* Address field mappings for To/Cc/Bcc headers */
static const struct {
	const gchar *jmap_field;
	const gchar *camel_header;
} jmap_addr_fields[] = {
	{ "to",  "To" },
	{ "cc",  "Cc" },
	{ "bcc", "Bcc" },
};

static gboolean
jmap_folder_refresh_info_sync (CamelFolder   *folder,
                               GCancellable  *cancellable,
                               GError       **error)
{
	CamelJmapFolder *jmap_folder = CAMEL_JMAP_FOLDER (folder);
	CamelStore *parent_store;
	CamelJmapStore *jmap_store;
	CamelFolderSummary *summary;
	JsonBuilder *builder;
	JsonNode *request, *response;
	JsonArray *method_responses;
	JsonArray *email_ids;
	JsonObject *query_response, *get_response;
	JsonArray *email_list;
	const gchar *account_id;
	guint ii, len;

	parent_store = camel_folder_get_parent_store (folder);
	jmap_store = CAMEL_JMAP_STORE (parent_store);
	account_id = camel_jmap_store_get_account_id (jmap_store);

	if (!account_id) {
		g_set_error (error, CAMEL_SERVICE_ERROR,
			CAMEL_SERVICE_ERROR_NOT_CONNECTED,
			_("Not connected to JMAP server"));
		return FALSE;
	}

	/* Step 1: Email/query to get IDs in this mailbox */
	builder = json_builder_new ();
	json_builder_begin_object (builder);
		json_builder_set_member_name (builder, "using");
		json_builder_begin_array (builder);
			json_builder_add_string_value (builder, "urn:ietf:params:jmap:core");
			json_builder_add_string_value (builder, "urn:ietf:params:jmap:mail");
		json_builder_end_array (builder);
		json_builder_set_member_name (builder, "methodCalls");
		json_builder_begin_array (builder);
			/* Call 0: Email/query */
			json_builder_begin_array (builder);
				json_builder_add_string_value (builder, "Email/query");
				json_builder_begin_object (builder);
					json_builder_set_member_name (builder, "accountId");
					json_builder_add_string_value (builder, account_id);
					json_builder_set_member_name (builder, "filter");
					json_builder_begin_object (builder);
						json_builder_set_member_name (builder, "inMailbox");
						json_builder_add_string_value (builder,
							jmap_folder->mailbox_id);
					json_builder_end_object (builder);
					json_builder_set_member_name (builder, "sort");
					json_builder_begin_array (builder);
						json_builder_begin_object (builder);
							json_builder_set_member_name (builder, "property");
							json_builder_add_string_value (builder, "receivedAt");
							json_builder_set_member_name (builder, "isAscending");
							json_builder_add_boolean_value (builder, FALSE);
						json_builder_end_object (builder);
					json_builder_end_array (builder);
					json_builder_set_member_name (builder, "limit");
					json_builder_add_int_value (builder, 500);
				json_builder_end_object (builder);
				json_builder_add_string_value (builder, "c0");
			json_builder_end_array (builder);
			/* Call 1: Email/get with results from call 0 */
			json_builder_begin_array (builder);
				json_builder_add_string_value (builder, "Email/get");
				json_builder_begin_object (builder);
					json_builder_set_member_name (builder, "accountId");
					json_builder_add_string_value (builder, account_id);
					json_builder_set_member_name (builder, "#ids");
					json_builder_begin_object (builder);
						json_builder_set_member_name (builder, "resultOf");
						json_builder_add_string_value (builder, "c0");
						json_builder_set_member_name (builder, "name");
						json_builder_add_string_value (builder, "Email/query");
						json_builder_set_member_name (builder, "path");
						json_builder_add_string_value (builder, "/ids");
					json_builder_end_object (builder);
					json_builder_set_member_name (builder, "properties");
					json_builder_begin_array (builder);
						json_builder_add_string_value (builder, "id");
						json_builder_add_string_value (builder, "subject");
						json_builder_add_string_value (builder, "from");
						json_builder_add_string_value (builder, "receivedAt");
						json_builder_add_string_value (builder, "sentAt");
						json_builder_add_string_value (builder, "size");
						json_builder_add_string_value (builder, "keywords");
					json_builder_end_array (builder);
				json_builder_end_object (builder);
				json_builder_add_string_value (builder, "c1");
			json_builder_end_array (builder);
		json_builder_end_array (builder);
	json_builder_end_object (builder);

	request = json_builder_get_root (builder);
	g_object_unref (builder);

	response = camel_jmap_store_call_sync (jmap_store, request, cancellable, error);
	json_node_unref (request);

	if (!response)
		return FALSE;

	method_responses = json_object_get_array_member (
		json_node_get_object (response), "methodResponses");

	if (!method_responses || json_array_get_length (method_responses) < 2) {
		g_set_error (error, CAMEL_SERVICE_ERROR,
			CAMEL_SERVICE_ERROR_INVALID,
			_("Unexpected JMAP response for Email/query+Email/get"));
		json_node_unref (response);
		return FALSE;
	}

	/* Parse Email/query response to verify IDs */
	query_response = json_array_get_object_element (
		json_array_get_array_element (method_responses, 0), 1);
	email_ids = json_object_get_array_member (query_response, "ids");

	/* Parse Email/get response for message metadata */
	get_response = json_array_get_object_element (
		json_array_get_array_element (method_responses, 1), 1);
	email_list = json_object_get_array_member (get_response, "list");

	summary = camel_folder_get_folder_summary (folder);
	camel_folder_summary_lock (summary);
	camel_folder_summary_clear (summary, NULL);

	if (email_list) {
		len = json_array_get_length (email_list);
		for (ii = 0; ii < len; ii++) {
			JsonObject *email_obj;
			CamelMessageInfo *info;

			email_obj = json_array_get_object_element (email_list, ii);
			info = jmap_email_to_message_info (folder, email_obj);
			if (info) {
				camel_folder_summary_add (summary, info, FALSE);
				g_object_unref (info);
			}
		}
	}

	camel_folder_summary_unlock (summary);
	camel_folder_summary_save (summary, NULL);

	json_node_unref (response);
	return TRUE;
}

static CamelMimeMessage *
jmap_folder_get_message_sync (CamelFolder *folder,
                              const gchar *uid,
                              GCancellable *cancellable,
                              GError **error)
{
	CamelStore *parent_store;
	CamelJmapStore *jmap_store;
	JsonBuilder *builder;
	JsonNode *request, *response;
	JsonArray *method_responses;
	JsonObject *get_response;
	JsonArray *email_list;
	JsonObject *email_obj;
	const gchar *account_id, *raw_message;
	CamelMimeMessage *message = NULL;

	parent_store = camel_folder_get_parent_store (folder);
	jmap_store = CAMEL_JMAP_STORE (parent_store);
	account_id = camel_jmap_store_get_account_id (jmap_store);

	if (!account_id) {
		g_set_error (error, CAMEL_SERVICE_ERROR,
			CAMEL_SERVICE_ERROR_NOT_CONNECTED,
			_("Not connected to JMAP server"));
		return NULL;
	}

	/* Fetch the full email with body using Email/get */
	builder = json_builder_new ();
	json_builder_begin_object (builder);
		json_builder_set_member_name (builder, "using");
		json_builder_begin_array (builder);
			json_builder_add_string_value (builder, "urn:ietf:params:jmap:core");
			json_builder_add_string_value (builder, "urn:ietf:params:jmap:mail");
		json_builder_end_array (builder);
		json_builder_set_member_name (builder, "methodCalls");
		json_builder_begin_array (builder);
			json_builder_begin_array (builder);
				json_builder_add_string_value (builder, "Email/get");
				json_builder_begin_object (builder);
					json_builder_set_member_name (builder, "accountId");
					json_builder_add_string_value (builder, account_id);
					json_builder_set_member_name (builder, "ids");
					json_builder_begin_array (builder);
						json_builder_add_string_value (builder, uid);
					json_builder_end_array (builder);
					json_builder_set_member_name (builder, "fetchAllBodyValues");
					json_builder_add_boolean_value (builder, TRUE);
					json_builder_set_member_name (builder, "properties");
					json_builder_begin_array (builder);
						json_builder_add_string_value (builder, "id");
						json_builder_add_string_value (builder, "subject");
						json_builder_add_string_value (builder, "from");
						json_builder_add_string_value (builder, "to");
						json_builder_add_string_value (builder, "cc");
						json_builder_add_string_value (builder, "bcc");
						json_builder_add_string_value (builder, "replyTo");
						json_builder_add_string_value (builder, "sentAt");
						json_builder_add_string_value (builder, "keywords");
						json_builder_add_string_value (builder, "bodyValues");
						json_builder_add_string_value (builder, "textBody");
						json_builder_add_string_value (builder, "htmlBody");
						json_builder_add_string_value (builder, "attachments");
						json_builder_add_string_value (builder, "headers");
					json_builder_end_array (builder);
				json_builder_end_object (builder);
				json_builder_add_string_value (builder, "c0");
			json_builder_end_array (builder);
		json_builder_end_array (builder);
	json_builder_end_object (builder);

	request = json_builder_get_root (builder);
	g_object_unref (builder);

	response = camel_jmap_store_call_sync (jmap_store, request, cancellable, error);
	json_node_unref (request);

	if (!response)
		return NULL;

	method_responses = json_object_get_array_member (
		json_node_get_object (response), "methodResponses");

	if (!method_responses || json_array_get_length (method_responses) == 0) {
		g_set_error (error, CAMEL_SERVICE_ERROR,
			CAMEL_SERVICE_ERROR_INVALID,
			_("Empty JMAP response for Email/get"));
		json_node_unref (response);
		return NULL;
	}

	get_response = json_array_get_object_element (
		json_array_get_array_element (method_responses, 0), 1);
	email_list = json_object_get_array_member (get_response, "list");

	if (!email_list || json_array_get_length (email_list) == 0) {
		g_set_error (error, CAMEL_FOLDER_ERROR,
			CAMEL_FOLDER_ERROR_INVALID_UID,
			_("Message not found: %s"), uid);
		json_node_unref (response);
		return NULL;
	}

	email_obj = json_array_get_object_element (email_list, 0);

	/* Build a CamelMimeMessage from the JMAP email object */
	message = camel_mime_message_new ();

	/* Subject */
	raw_message = json_object_get_string_member_with_default (email_obj, "subject", NULL);
	if (raw_message)
		camel_mime_message_set_subject (message, raw_message);

	/* Date */
	raw_message = json_object_get_string_member_with_default (email_obj, "sentAt", NULL);
	if (raw_message) {
		GDateTime *dt;
		dt = g_date_time_new_from_iso8601 (raw_message, NULL);
		if (dt) {
			camel_mime_message_set_date (message,
				(time_t) g_date_time_to_unix (dt), 0);
			g_date_time_unref (dt);
		}
	}

	/* From address */
	if (json_object_has_member (email_obj, "from")) {
		JsonArray *from_arr;
		from_arr = json_object_get_array_member (email_obj, "from");
		if (from_arr && json_array_get_length (from_arr) > 0) {
			JsonObject *addr_obj;
			const gchar *name, *addr;
			CamelInternetAddress *from_addr;

			addr_obj = json_array_get_object_element (from_arr, 0);
			name = json_object_get_string_member_with_default (addr_obj, "name", "");
			addr = json_object_get_string_member_with_default (addr_obj, "email", "");

			from_addr = camel_internet_address_new ();
			camel_internet_address_add (from_addr, name, addr);
			camel_mime_message_set_from (message, from_addr);
			g_object_unref (from_addr);
		}
	}

	/* To, Cc, Bcc */
	for (guint fi = 0; fi < G_N_ELEMENTS (jmap_addr_fields); fi++) {
		if (json_object_has_member (email_obj, jmap_addr_fields[fi].jmap_field)) {
			JsonArray *arr;
			arr = json_object_get_array_member (email_obj, jmap_addr_fields[fi].jmap_field);
			if (arr && json_array_get_length (arr) > 0) {
				CamelInternetAddress *addrs;
				guint jj, alen;

				addrs = camel_internet_address_new ();
				alen = json_array_get_length (arr);
				for (jj = 0; jj < alen; jj++) {
					JsonObject *addr_obj;
					const gchar *name, *addr;

					addr_obj = json_array_get_object_element (arr, jj);
					name = json_object_get_string_member_with_default (addr_obj, "name", "");
					addr = json_object_get_string_member_with_default (addr_obj, "email", "");
					camel_internet_address_add (addrs, name, addr);
				}

				camel_medium_set_header (
					CAMEL_MEDIUM (message),
					jmap_addr_fields[fi].camel_header,
					camel_address_encode (CAMEL_ADDRESS (addrs)));
				g_object_unref (addrs);
			}
		}
	}

	/* Build message body from bodyValues and textBody/htmlBody */
	if (json_object_has_member (email_obj, "bodyValues")) {
		JsonObject *body_values;
		JsonObject *text_body, *html_body;
		const gchar *text_part_id = NULL, *html_part_id = NULL;
		const gchar *text_content = NULL, *html_content = NULL;

		body_values = json_object_get_object_member (email_obj, "bodyValues");

		if (json_object_has_member (email_obj, "textBody")) {
			JsonArray *text_body_arr;
			text_body_arr = json_object_get_array_member (email_obj, "textBody");
			if (text_body_arr && json_array_get_length (text_body_arr) > 0) {
				text_body = json_array_get_object_element (text_body_arr, 0);
				text_part_id = json_object_get_string_member_with_default (
					text_body, "partId", NULL);
			}
		}

		if (json_object_has_member (email_obj, "htmlBody")) {
			JsonArray *html_body_arr;
			html_body_arr = json_object_get_array_member (email_obj, "htmlBody");
			if (html_body_arr && json_array_get_length (html_body_arr) > 0) {
				html_body = json_array_get_object_element (html_body_arr, 0);
				html_part_id = json_object_get_string_member_with_default (
					html_body, "partId", NULL);
			}
		}

		if (text_part_id && body_values &&
		    json_object_has_member (body_values, text_part_id)) {
			JsonObject *part_value;
			part_value = json_object_get_object_member (body_values, text_part_id);
			text_content = json_object_get_string_member_with_default (
				part_value, "value", NULL);
		}

		if (html_part_id && body_values &&
		    json_object_has_member (body_values, html_part_id)) {
			JsonObject *part_value;
			part_value = json_object_get_object_member (body_values, html_part_id);
			html_content = json_object_get_string_member_with_default (
				part_value, "value", NULL);
		}

		if (text_content && html_content) {
			/* Multipart/alternative */
			CamelMultipart *multipart;
			CamelMimePart *text_part_obj, *html_part_obj;

			multipart = camel_multipart_new ();
			camel_data_wrapper_set_mime_type (
				CAMEL_DATA_WRAPPER (multipart), "multipart/alternative");
			camel_multipart_set_boundary (multipart, NULL);

			text_part_obj = camel_mime_part_new ();
			camel_mime_part_set_content (text_part_obj, text_content,
				strlen (text_content), "text/plain; charset=utf-8");
			camel_multipart_add_part (multipart, text_part_obj);
			g_object_unref (text_part_obj);

			html_part_obj = camel_mime_part_new ();
			camel_mime_part_set_content (html_part_obj, html_content,
				strlen (html_content), "text/html; charset=utf-8");
			camel_multipart_add_part (multipart, html_part_obj);
			g_object_unref (html_part_obj);

			camel_medium_set_content (
				CAMEL_MEDIUM (message),
				CAMEL_DATA_WRAPPER (multipart));
			g_object_unref (multipart);
		} else if (html_content) {
			camel_mime_part_set_content (
				CAMEL_MIME_PART (message),
				html_content, strlen (html_content),
				"text/html; charset=utf-8");
		} else if (text_content) {
			camel_mime_part_set_content (
				CAMEL_MIME_PART (message),
				text_content, strlen (text_content),
				"text/plain; charset=utf-8");
		}
	}

	json_node_unref (response);
	return message;
}

static gboolean
jmap_folder_synchronize_sync (CamelFolder *folder,
                              gboolean expunge,
                              GCancellable *cancellable,
                              GError **error)
{
	/* JMAP is stateless over HTTP; flags are pushed immediately
	 * via set_message_flags/set_message_user_flag, nothing to flush. */
	return TRUE;
}

static gboolean
jmap_folder_append_message_sync (CamelFolder *folder,
                                 CamelMimeMessage *message,
                                 CamelMessageInfo *info,
                                 gchar **appended_uid,
                                 GCancellable *cancellable,
                                 GError **error)
{
	CamelJmapFolder *jmap_folder = CAMEL_JMAP_FOLDER (folder);
	CamelStore *parent_store;
	CamelJmapStore *jmap_store;
	CamelStream *stream;
	GByteArray *byte_array;
	gchar *raw_message;
	gchar *base64_message;
	JsonBuilder *builder;
	JsonNode *request, *response;
	JsonArray *method_responses;
	JsonObject *import_response, *created, *email_obj;
	const gchar *account_id, *new_id;

	parent_store = camel_folder_get_parent_store (folder);
	jmap_store = CAMEL_JMAP_STORE (parent_store);
	account_id = camel_jmap_store_get_account_id (jmap_store);

	if (!account_id) {
		g_set_error (error, CAMEL_SERVICE_ERROR,
			CAMEL_SERVICE_ERROR_NOT_CONNECTED,
			_("Not connected to JMAP server"));
		return FALSE;
	}

	/* Serialize the message to RFC 822 format */
	byte_array = g_byte_array_new ();
	stream = camel_stream_mem_new_with_byte_array (byte_array);
	if (camel_data_wrapper_write_to_stream_sync (
	    CAMEL_DATA_WRAPPER (message), stream, cancellable, error) == -1) {
		g_object_unref (stream);
		return FALSE;
	}
	g_object_unref (stream);

	raw_message = g_strndup ((gchar *) byte_array->data, byte_array->len);
	base64_message = g_base64_encode (byte_array->data, byte_array->len);
	g_free (raw_message);

	builder = json_builder_new ();
	json_builder_begin_object (builder);
		json_builder_set_member_name (builder, "using");
		json_builder_begin_array (builder);
			json_builder_add_string_value (builder, "urn:ietf:params:jmap:core");
			json_builder_add_string_value (builder, "urn:ietf:params:jmap:mail");
		json_builder_end_array (builder);
		json_builder_set_member_name (builder, "methodCalls");
		json_builder_begin_array (builder);
			json_builder_begin_array (builder);
				json_builder_add_string_value (builder, "Email/import");
				json_builder_begin_object (builder);
					json_builder_set_member_name (builder, "accountId");
					json_builder_add_string_value (builder, account_id);
					json_builder_set_member_name (builder, "emails");
					json_builder_begin_object (builder);
						json_builder_set_member_name (builder, "import-1");
						json_builder_begin_object (builder);
							json_builder_set_member_name (builder, "mailboxIds");
							json_builder_begin_object (builder);
								json_builder_set_member_name (builder,
									jmap_folder->mailbox_id);
								json_builder_add_boolean_value (builder, TRUE);
							json_builder_end_object (builder);
							json_builder_set_member_name (builder, "rawMessage");
							json_builder_add_string_value (builder, base64_message);
						json_builder_end_object (builder);
					json_builder_end_object (builder);
				json_builder_end_object (builder);
				json_builder_add_string_value (builder, "c0");
			json_builder_end_array (builder);
		json_builder_end_array (builder);
	json_builder_end_object (builder);

	g_free (base64_message);

	request = json_builder_get_root (builder);
	g_object_unref (builder);

	response = camel_jmap_store_call_sync (jmap_store, request, cancellable, error);
	json_node_unref (request);

	if (!response)
		return FALSE;

	method_responses = json_object_get_array_member (
		json_node_get_object (response), "methodResponses");
	import_response = json_array_get_object_element (
		json_array_get_array_element (method_responses, 0), 1);
	created = json_object_get_object_member (import_response, "created");

	new_id = NULL;
	if (created) {
		email_obj = json_object_get_object_member (created, "import-1");
		if (email_obj)
			new_id = json_object_get_string_member_with_default (email_obj, "id", NULL);
	}

	if (appended_uid && new_id)
		*appended_uid = g_strdup (new_id);

	json_node_unref (response);
	return TRUE;
}

static CamelFolderQuotaInfo *
jmap_folder_get_quota_info_sync (CamelFolder *folder,
                                 GCancellable *cancellable,
                                 GError **error)
{
	CamelStore *parent_store;
	CamelJmapStore *jmap_store;
	JsonBuilder *builder;
	JsonNode *request, *response;
	JsonArray *method_responses;
	JsonObject *get_response;
	JsonArray *quota_list;
	CamelFolderQuotaInfo *result = NULL, *last = NULL;
	const gchar *account_id;
	guint ii, len;

	parent_store = camel_folder_get_parent_store (folder);
	jmap_store = CAMEL_JMAP_STORE (parent_store);
	account_id = camel_jmap_store_get_account_id (jmap_store);

	if (!account_id) {
		g_set_error (error, CAMEL_SERVICE_ERROR,
			CAMEL_SERVICE_ERROR_NOT_CONNECTED,
			_("Not connected to JMAP server"));
		return NULL;
	}

	/* Build Quota/get request per RFC 9425 */
	builder = json_builder_new ();
	json_builder_begin_object (builder);
		json_builder_set_member_name (builder, "using");
		json_builder_begin_array (builder);
			json_builder_add_string_value (builder, "urn:ietf:params:jmap:core");
			json_builder_add_string_value (builder, "urn:ietf:params:jmap:quota");
		json_builder_end_array (builder);
		json_builder_set_member_name (builder, "methodCalls");
		json_builder_begin_array (builder);
			json_builder_begin_array (builder);
				json_builder_add_string_value (builder, "Quota/get");
				json_builder_begin_object (builder);
					json_builder_set_member_name (builder, "accountId");
					json_builder_add_string_value (builder, account_id);
					json_builder_set_member_name (builder, "ids");
					json_builder_add_null_value (builder);
				json_builder_end_object (builder);
				json_builder_add_string_value (builder, "c0");
			json_builder_end_array (builder);
		json_builder_end_array (builder);
	json_builder_end_object (builder);

	request = json_builder_get_root (builder);
	g_object_unref (builder);

	response = camel_jmap_store_call_sync (jmap_store, request, cancellable, error);
	json_node_unref (request);

	if (!response)
		return NULL;

	method_responses = json_object_get_array_member (
		json_node_get_object (response), "methodResponses");

	if (!method_responses || json_array_get_length (method_responses) == 0) {
		g_set_error (error, CAMEL_SERVICE_ERROR,
			CAMEL_SERVICE_ERROR_INVALID,
			_("Empty JMAP response for Quota/get"));
		json_node_unref (response);
		return NULL;
	}

	get_response = json_array_get_object_element (
		json_array_get_array_element (method_responses, 0), 1);

	quota_list = json_object_get_array_member (get_response, "list");
	if (!quota_list) {
		json_node_unref (response);
		return NULL;
	}

	len = json_array_get_length (quota_list);
	for (ii = 0; ii < len; ii++) {
		JsonObject *quota_obj;
		JsonArray *data_types;
		const gchar *name, *resource_type;
		guint64 used, hard_limit;
		gboolean applies_to_mail = FALSE;
		CamelFolderQuotaInfo *qi;
		guint jj, dlen;

		quota_obj = json_array_get_object_element (quota_list, ii);
		name = json_object_get_string_member_with_default (quota_obj, "name", NULL);
		resource_type = json_object_get_string_member_with_default (
			quota_obj, "resourceType", NULL);

		if (!name || !resource_type)
			continue;

		/* Only include quotas that apply to Mail data type */
		data_types = json_object_get_array_member (quota_obj, "dataTypes");
		if (data_types) {
			dlen = json_array_get_length (data_types);
			for (jj = 0; jj < dlen; jj++) {
				const gchar *dt;
				dt = json_array_get_string_element (data_types, jj);
				if (g_strcmp0 (dt, "Mail") == 0) {
					applies_to_mail = TRUE;
					break;
				}
			}
		}

		if (!applies_to_mail)
			continue;

		used = (guint64) json_object_get_int_member_with_default (quota_obj, "used", 0);

		/* hardLimit may be absent/null to indicate no limit */
		if (json_object_has_member (quota_obj, "hardLimit") &&
		    !JSON_NODE_HOLDS_NULL (json_object_get_member (quota_obj, "hardLimit")))
			hard_limit = (guint64) json_object_get_int_member_with_default (
				quota_obj, "hardLimit", 0);
		else
			hard_limit = 0;

		qi = camel_folder_quota_info_new (name, used, hard_limit);

		if (last)
			last->next = qi;
		else
			result = qi;
		last = qi;
	}

	json_node_unref (response);

	return result;
}

static gboolean
jmap_folder_expunge_sync (CamelFolder *folder,
                          GCancellable *cancellable,
                          GError **error)
{
	/* Permanently delete messages flagged for deletion.
	 * For JMAP, deletion is permanent via Email/set destroy. */
	return jmap_folder_synchronize_sync (folder, TRUE, cancellable, error);
}

static gint
jmap_folder_get_message_count (CamelFolder *folder)
{
	return (gint) camel_folder_summary_count (camel_folder_get_folder_summary (folder));
}

static GPtrArray *
jmap_folder_dup_uids (CamelFolder *folder)
{
	return camel_folder_summary_dup_uids (camel_folder_get_folder_summary (folder));
}

static void
jmap_folder_dispose (GObject *object)
{
	G_OBJECT_CLASS (camel_jmap_folder_parent_class)->dispose (object);
}

static void
jmap_folder_finalize (GObject *object)
{
	CamelJmapFolder *folder = CAMEL_JMAP_FOLDER (object);

	g_free (folder->mailbox_id);

	G_OBJECT_CLASS (camel_jmap_folder_parent_class)->finalize (object);
}

static void
camel_jmap_folder_class_init (CamelJmapFolderClass *class)
{
	GObjectClass *object_class;
	CamelFolderClass *folder_class;

	object_class = G_OBJECT_CLASS (class);
	object_class->dispose = jmap_folder_dispose;
	object_class->finalize = jmap_folder_finalize;

	folder_class = CAMEL_FOLDER_CLASS (class);
	folder_class->get_message_count = jmap_folder_get_message_count;
	folder_class->dup_uids = jmap_folder_dup_uids;
	folder_class->refresh_info_sync = jmap_folder_refresh_info_sync;
	folder_class->get_message_sync = jmap_folder_get_message_sync;
	folder_class->get_quota_info_sync = jmap_folder_get_quota_info_sync;
	folder_class->synchronize_sync = jmap_folder_synchronize_sync;
	folder_class->append_message_sync = jmap_folder_append_message_sync;
	folder_class->expunge_sync = jmap_folder_expunge_sync;
}

static void
camel_jmap_folder_init (CamelJmapFolder *folder)
{
}

/**
 * camel_jmap_folder_new:
 * @store: the parent #CamelJmapStore
 * @folder_name: the folder's full name
 * @mailbox_id: (nullable): the JMAP mailbox ID, or %NULL
 * @cancellable: optional #GCancellable
 * @error: return location for a #GError, or %NULL
 *
 * Creates a new #CamelJmapFolder for the given mailbox.
 *
 * Returns: (transfer full) (nullable): a new #CamelFolder, or %NULL on error
 */
CamelFolder *
camel_jmap_folder_new (CamelStore *store,
                       const gchar *folder_name,
                       const gchar *mailbox_id,
                       GCancellable *cancellable,
                       GError **error)
{
	CamelJmapFolder *folder;
	CamelFolderSummary *summary;
	gchar *summary_path;

	g_return_val_if_fail (CAMEL_IS_JMAP_STORE (store), NULL);
	g_return_val_if_fail (folder_name != NULL, NULL);

	folder = g_object_new (CAMEL_TYPE_JMAP_FOLDER,
		"full-name", folder_name,
		"display-name", strrchr (folder_name, '/') ?
			strrchr (folder_name, '/') + 1 : folder_name,
		"parent-store", store,
		NULL);

	folder->mailbox_id = g_strdup (mailbox_id);

	summary_path = g_strdup_printf ("%s/jmap-%s.cmeta",
		camel_service_get_user_cache_dir (CAMEL_SERVICE (store)),
		folder_name);

	summary = camel_folder_summary_new (CAMEL_FOLDER (folder));
	camel_folder_take_folder_summary (CAMEL_FOLDER (folder), summary);

	g_free (summary_path);

	return CAMEL_FOLDER (folder);
}
