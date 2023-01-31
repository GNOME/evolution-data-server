/*
 * Copyright (C) 2015 Red Hat, Inc. (www.redhat.com)
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

#include "evolution-data-server-config.h"

#include <glib.h>
#include <glib/gi18n-lib.h>

#include <gtk/gtk.h>

#include <libedataserver/libedataserver.h>

#include "libedataserverui-private.h"

#include "e-credentials-prompter.h"
#include "e-credentials-prompter-impl-oauth2.h"

#if GTK_CHECK_VERSION(4, 0, 0)
#ifdef ENABLE_OAUTH2_WEBKITGTK4
#define WITH_WEBKITGTK 1
#else
#undef WITH_WEBKITGTK
#endif
#else
#ifdef ENABLE_OAUTH2_WEBKITGTK
#define WITH_WEBKITGTK 1
#else
#undef WITH_WEBKITGTK
#endif
#endif

#ifdef WITH_WEBKITGTK
#include <webkit2/webkit2.h>
#endif /* WITH_WEBKITGTK */

struct _ECredentialsPrompterImplOAuth2Private {
	GMutex property_lock;

	EOAuth2Services *oauth2_services;

	gpointer prompt_id;
	ESource *auth_source;
	ESource *cred_source;
	EOAuth2Service *service;
	gchar *error_text;
	ENamedParameters *credentials;
	gboolean refresh_failed_with_transport_error;

	GtkDialog *dialog;
#ifdef WITH_WEBKITGTK
	WebKitWebView *web_view;
#endif
	GtkNotebook *notebook;
	GtkEntry *auth_code_entry;
	GtkLabel *error_text_label;
	gulong show_dialog_idle_id;

	GCancellable *cancellable;
};

G_DEFINE_TYPE_WITH_PRIVATE (ECredentialsPrompterImplOAuth2, e_credentials_prompter_impl_oauth2, E_TYPE_CREDENTIALS_PROMPTER_IMPL)

static gboolean
cpi_oauth2_get_debug (void)
{
	static gint oauth2_debug = -1;

	if (oauth2_debug == -1)
		oauth2_debug = g_strcmp0 (g_getenv ("OAUTH2_DEBUG"), "1") == 0 ? 1 : 0;

	return oauth2_debug == 1;
}

static gchar *
cpi_oauth2_create_auth_uri (EOAuth2Service *service,
			    ESource *source)
{
	GHashTable *uri_query;
	GUri *parsed_uri;
	gchar *uri, *query;

	g_return_val_if_fail (E_IS_OAUTH2_SERVICE (service), NULL);
	g_return_val_if_fail (E_IS_SOURCE (source), NULL);

	parsed_uri = g_uri_parse (e_oauth2_service_get_authentication_uri (service, source), SOUP_HTTP_URI_FLAGS | G_URI_FLAGS_PARSE_RELAXED, NULL);
	g_return_val_if_fail (parsed_uri != NULL, NULL);

	uri_query = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);

	e_oauth2_service_prepare_authentication_uri_query (service, source, uri_query);

	query = soup_form_encode_hash (uri_query);
	e_util_change_uri_component (&parsed_uri, SOUP_URI_QUERY, query);

	uri = g_uri_to_string_partial (parsed_uri, G_URI_HIDE_PASSWORD);

	g_uri_unref (parsed_uri);
	g_hash_table_destroy (uri_query);

	return uri;
}

static void
cpi_oauth2_show_error (ECredentialsPrompterImplOAuth2 *prompter_oauth2,
		       const gchar *title,
		       const gchar *body_text)
{
#ifdef WITH_WEBKITGTK
	gchar *html;

	g_return_if_fail (WEBKIT_IS_WEB_VIEW (prompter_oauth2->priv->web_view));
#endif /* WITH_WEBKITGTK */
	g_return_if_fail (title != NULL);
	g_return_if_fail (body_text != NULL);

#ifdef WITH_WEBKITGTK
	html = g_markup_printf_escaped (
		"<html>"
		"<head><title>%s</title></head>"
		"<body><div style=\"font-size:12pt; font-family:Helvetica,Arial;\">%s</div></body>"
		"</html>",
		title,
		body_text);
	webkit_web_view_load_html (prompter_oauth2->priv->web_view, html, "none-local://");
	g_free (html);
#endif /* WITH_WEBKITGTK */

	gtk_label_set_text (prompter_oauth2->priv->error_text_label, body_text);
}

static gboolean
e_credentials_prompter_impl_oauth2_finish_dialog_idle_cb (gpointer user_data)
{
	ECredentialsPrompterImplOAuth2 *prompter_oauth2 = user_data;

	if (g_source_is_destroyed (g_main_current_source ()))
		return FALSE;

	g_return_val_if_fail (E_IS_CREDENTIALS_PROMPTER_IMPL_OAUTH2 (prompter_oauth2), FALSE);

	g_mutex_lock (&prompter_oauth2->priv->property_lock);
	if (g_source_get_id (g_main_current_source ()) == prompter_oauth2->priv->show_dialog_idle_id) {
		prompter_oauth2->priv->show_dialog_idle_id = 0;
		g_mutex_unlock (&prompter_oauth2->priv->property_lock);

		g_warn_if_fail (prompter_oauth2->priv->dialog != NULL);

		if (prompter_oauth2->priv->error_text) {
			cpi_oauth2_show_error (prompter_oauth2,
				"Finished with error", prompter_oauth2->priv->error_text);

			gtk_widget_set_sensitive (GTK_WIDGET (prompter_oauth2->priv->notebook), TRUE);
		} else {
			gtk_dialog_response (prompter_oauth2->priv->dialog, GTK_RESPONSE_OK);
		}
	} else {
		g_warning ("%s: Source was cancelled? current:%d expected:%d", G_STRFUNC, (gint) g_source_get_id (g_main_current_source ()), (gint) prompter_oauth2->priv->show_dialog_idle_id);
		g_mutex_unlock (&prompter_oauth2->priv->property_lock);
	}

	return FALSE;
}

typedef struct {
	GWeakRef *prompter_oauth2; /* ECredentialsPrompterImplOAuth2 * */
	GCancellable *cancellable;
	ESource *cred_source;
	ESourceRegistry *registry;
	gchar *authorization_code;
	EOAuth2Service *service;
} AccessTokenThreadData;

static void
access_token_thread_data_free (gpointer user_data)
{
	AccessTokenThreadData *td = user_data;

	if (td) {
		e_weak_ref_free (td->prompter_oauth2);
		g_clear_object (&td->cancellable);
		g_clear_object (&td->cred_source);
		g_clear_object (&td->registry);
		g_clear_object (&td->service);
		g_free (td->authorization_code);
		g_slice_free (AccessTokenThreadData, td);
	}
}

static gpointer
cpi_oauth2_get_access_token_thread (gpointer user_data)
{
	AccessTokenThreadData *td = user_data;
	ECredentialsPrompterImplOAuth2 *prompter_oauth2;
	GError *local_error = NULL;
	gboolean success = FALSE;

	g_return_val_if_fail (td != NULL, NULL);

	if (!g_cancellable_set_error_if_cancelled (td->cancellable, &local_error)) {
		EOAuth2ServiceRefSourceFunc ref_source;

		ref_source = (EOAuth2ServiceRefSourceFunc) e_source_registry_ref_source;

		success = e_oauth2_service_receive_and_store_token_sync (td->service, td->cred_source,
			td->authorization_code, ref_source, td->registry, td->cancellable, &local_error);
	}

	prompter_oauth2 = g_weak_ref_get (td->prompter_oauth2);
	if (prompter_oauth2 && !g_cancellable_is_cancelled (td->cancellable)) {
		g_clear_pointer (&prompter_oauth2->priv->error_text, g_free);

		if (!success) {
			prompter_oauth2->priv->error_text = g_strdup_printf (
				_("Failed to obtain access token from address “%s”: %s"),
				e_oauth2_service_get_refresh_uri (td->service, td->cred_source),
				local_error ? local_error->message : _("Unknown error"));
		}

		g_mutex_lock (&prompter_oauth2->priv->property_lock);
		prompter_oauth2->priv->show_dialog_idle_id = g_idle_add (
			e_credentials_prompter_impl_oauth2_finish_dialog_idle_cb,
			prompter_oauth2);
		g_mutex_unlock (&prompter_oauth2->priv->property_lock);
	}

	g_clear_object (&prompter_oauth2);
	g_clear_error (&local_error);

	access_token_thread_data_free (td);

	return NULL;
}

static void
cpi_oauth2_test_authorization_code (ECredentialsPrompterImplOAuth2 *prompter_oauth2,
				    gchar *authorization_code) /* (transfer full) */
{

	if (authorization_code) {
		ECredentialsPrompter *prompter;
		ECredentialsPrompterImpl *prompter_impl;
		AccessTokenThreadData *td;
		GThread *thread;

		cpi_oauth2_show_error (prompter_oauth2, "Checking returned code", _("Requesting access token, please wait…"));

		gtk_widget_set_sensitive (GTK_WIDGET (prompter_oauth2->priv->notebook), FALSE);

		e_named_parameters_set (prompter_oauth2->priv->credentials, E_SOURCE_CREDENTIAL_PASSWORD, NULL);

		prompter_impl = E_CREDENTIALS_PROMPTER_IMPL (prompter_oauth2);
		prompter = e_credentials_prompter_impl_get_credentials_prompter (prompter_impl);

		td = g_slice_new0 (AccessTokenThreadData);
		td->prompter_oauth2 = e_weak_ref_new (prompter_oauth2);
		td->service = g_object_ref (prompter_oauth2->priv->service);
		td->cancellable = g_object_ref (prompter_oauth2->priv->cancellable);
		td->cred_source = g_object_ref (prompter_oauth2->priv->cred_source);
		td->registry = g_object_ref (e_credentials_prompter_get_registry (prompter));
		td->authorization_code = authorization_code;

		thread = g_thread_new (G_STRFUNC, cpi_oauth2_get_access_token_thread, td);
		g_thread_unref (thread);
	} else {
		g_cancellable_cancel (prompter_oauth2->priv->cancellable);
		gtk_dialog_response (prompter_oauth2->priv->dialog, GTK_RESPONSE_CANCEL);
	}
}

#ifdef WITH_WEBKITGTK

static void
cpi_oauth2_extract_authentication_code (ECredentialsPrompterImplOAuth2 *prompter_oauth2,
					const gchar *page_title,
					const gchar *page_uri,
					const gchar *page_content)
{
	gchar *authorization_code = NULL;

	g_return_if_fail (E_IS_CREDENTIALS_PROMPTER_IMPL_OAUTH2 (prompter_oauth2));
	g_return_if_fail (prompter_oauth2->priv->service != NULL);

	if (!e_oauth2_service_extract_authorization_code (prompter_oauth2->priv->service,
		prompter_oauth2->priv->cred_source ? prompter_oauth2->priv->cred_source : prompter_oauth2->priv->auth_source,
		page_title, page_uri, page_content, &authorization_code)) {
		return;
	}

	cpi_oauth2_test_authorization_code (prompter_oauth2, authorization_code);
}

static void
cpi_oauth2_web_view_resource_get_data_done_cb (GObject *source_object,
					       GAsyncResult *result,
					       gpointer user_data)
{
	ECredentialsPrompterImplOAuth2 *prompter_oauth2 = user_data;
	GByteArray *page_content = NULL;
	const gchar *title, *uri;
	guchar *data;
	gsize len = 0;
	GError *local_error = NULL;

	g_return_if_fail (WEBKIT_IS_WEB_RESOURCE (source_object));
	g_return_if_fail (E_IS_CREDENTIALS_PROMPTER_IMPL_OAUTH2 (prompter_oauth2));

	data = webkit_web_resource_get_data_finish (WEBKIT_WEB_RESOURCE (source_object), result, &len, &local_error);
	if (data) {
		page_content = g_byte_array_new_take ((guint8 *) data, len);

		/* NULL-terminate the array, to be able to use it as a string */
		g_byte_array_append (page_content, (const guint8 *) "", 1);
	} else if (g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
		g_clear_error (&local_error);
		return;
	}

	g_clear_error (&local_error);

	title = webkit_web_view_get_title (prompter_oauth2->priv->web_view);
	uri = webkit_web_view_get_uri (prompter_oauth2->priv->web_view);

	cpi_oauth2_extract_authentication_code (prompter_oauth2, title, uri, page_content ? (const gchar *) page_content->data : NULL);

	if (page_content)
		g_byte_array_free (page_content, TRUE);
}

static gboolean
cpi_oauth2_decide_policy_cb (WebKitWebView *web_view,
			     WebKitPolicyDecision *decision,
			     WebKitPolicyDecisionType decision_type,
			     ECredentialsPrompterImplOAuth2 *prompter_oauth2)
{
	WebKitNavigationAction *navigation_action;
	WebKitURIRequest *request;

	g_return_val_if_fail (E_IS_CREDENTIALS_PROMPTER_IMPL_OAUTH2 (prompter_oauth2), FALSE);
	g_return_val_if_fail (WEBKIT_IS_POLICY_DECISION (decision), FALSE);

	if (decision_type != WEBKIT_POLICY_DECISION_TYPE_NAVIGATION_ACTION)
		return FALSE;

	navigation_action = webkit_navigation_policy_decision_get_navigation_action (WEBKIT_NAVIGATION_POLICY_DECISION (decision));
	if (!navigation_action)
		return FALSE;

	request = webkit_navigation_action_get_request (navigation_action);
	if (!request || !webkit_uri_request_get_uri (request))
		return FALSE;

	g_return_val_if_fail (prompter_oauth2->priv->service != NULL, FALSE);

	switch (e_oauth2_service_get_authentication_policy (prompter_oauth2->priv->service,
		prompter_oauth2->priv->cred_source ? prompter_oauth2->priv->cred_source : prompter_oauth2->priv->auth_source,
		webkit_uri_request_get_uri (request))) {
	case E_OAUTH2_SERVICE_NAVIGATION_POLICY_DENY:
		webkit_policy_decision_ignore (decision);
		break;
	case E_OAUTH2_SERVICE_NAVIGATION_POLICY_ALLOW:
		webkit_policy_decision_use (decision);
		break;
	case E_OAUTH2_SERVICE_NAVIGATION_POLICY_ABORT:
		g_cancellable_cancel (prompter_oauth2->priv->cancellable);
		gtk_dialog_response (prompter_oauth2->priv->dialog, GTK_RESPONSE_CANCEL);
		break;
	default:
		return FALSE;
	}

	return TRUE;
}

static void
cpi_oauth2_document_load_changed_cb (WebKitWebView *web_view,
				     WebKitLoadEvent load_event,
				     ECredentialsPrompterImplOAuth2 *prompter_oauth2)
{
	const gchar *title, *uri;

	g_return_if_fail (WEBKIT_IS_WEB_VIEW (web_view));
	g_return_if_fail (E_IS_CREDENTIALS_PROMPTER_IMPL_OAUTH2 (prompter_oauth2));

	if (load_event != WEBKIT_LOAD_FINISHED)
		return;

	title = webkit_web_view_get_title (web_view);
	uri = webkit_web_view_get_uri (web_view);
	if (!title || !uri)
		return;

	if (cpi_oauth2_get_debug ()) {
		e_util_debug_print ("OAuth2", "Loaded URI: '%s'\n", uri);
	}

	g_return_if_fail (prompter_oauth2->priv->service != NULL);

	if ((e_oauth2_service_get_flags (prompter_oauth2->priv->service) & E_OAUTH2_SERVICE_FLAG_EXTRACT_REQUIRES_PAGE_CONTENT) != 0) {
		WebKitWebResource *main_resource;

		main_resource = webkit_web_view_get_main_resource (web_view);
		if (main_resource) {
			webkit_web_resource_get_data (main_resource, prompter_oauth2->priv->cancellable,
				cpi_oauth2_web_view_resource_get_data_done_cb, prompter_oauth2);
		}
	} else {
		cpi_oauth2_extract_authentication_code (prompter_oauth2, title, uri, NULL);
	}
}

static void
cpi_oauth2_notify_estimated_load_progress_cb (WebKitWebView *web_view,
					      GParamSpec *param,
					      GtkProgressBar *progress_bar)
{
	gboolean visible;
	gdouble progress;

	g_return_if_fail (GTK_IS_PROGRESS_BAR (progress_bar));

	progress = webkit_web_view_get_estimated_load_progress (web_view);
	visible = progress > 1e-9 && progress < 1 - 1e-9;

	gtk_progress_bar_set_fraction (progress_bar, visible ? progress : 0.0);
}

#endif /* WITH_WEBKITGTK */

static void
credentials_prompter_impl_oauth2_get_prompt_strings (ESourceRegistry *registry,
						     ESource *source,
						     const gchar *service_display_name,
						     gchar **prompt_title,
						     GString **prompt_description)
{
	GString *description;
	gchar *message;
	gchar *display_name;

	/* Known types */
	enum {
		TYPE_UNKNOWN,
		TYPE_AMBIGUOUS,
		TYPE_ADDRESS_BOOK,
		TYPE_CALENDAR,
		TYPE_MAIL_ACCOUNT,
		TYPE_MAIL_TRANSPORT,
		TYPE_MEMO_LIST,
		TYPE_TASK_LIST
	} type = TYPE_UNKNOWN;

	if (e_source_has_extension (source, E_SOURCE_EXTENSION_ADDRESS_BOOK)) {
		type = TYPE_ADDRESS_BOOK;
	}

	if (e_source_has_extension (source, E_SOURCE_EXTENSION_CALENDAR)) {
		if (type == TYPE_UNKNOWN)
			type = TYPE_CALENDAR;
		else
			type = TYPE_AMBIGUOUS;
	}

	if (e_source_has_extension (source, E_SOURCE_EXTENSION_MAIL_ACCOUNT)) {
		if (type == TYPE_UNKNOWN)
			type = TYPE_MAIL_ACCOUNT;
		else
			type = TYPE_AMBIGUOUS;
	}

	if (e_source_has_extension (source, E_SOURCE_EXTENSION_MAIL_TRANSPORT)) {
		if (type == TYPE_UNKNOWN)
			type = TYPE_MAIL_TRANSPORT;
		else
			type = TYPE_AMBIGUOUS;
	}

	if (e_source_has_extension (source, E_SOURCE_EXTENSION_MEMO_LIST)) {
		if (type == TYPE_UNKNOWN)
			type = TYPE_MEMO_LIST;
		else
			type = TYPE_AMBIGUOUS;
	}

	if (e_source_has_extension (source, E_SOURCE_EXTENSION_TASK_LIST)) {
		if (type == TYPE_UNKNOWN)
			type = TYPE_TASK_LIST;
		else
			type = TYPE_AMBIGUOUS;
	}

	switch (type) {
		case TYPE_ADDRESS_BOOK:
			/* Translators: The %s is replaced with an OAuth2 service display name, like the strings from "OAuth2Service" translation context,
			   thus it can form a string like "Google Address Book authentication request". */
			message = g_strdup_printf (_("%s Address Book authentication request"), service_display_name);
			break;
		case TYPE_CALENDAR:
			/* Translators: The %s is replaced with an OAuth2 service display name, like the strings from "OAuth2Service" translation context,
			   thus it can form a string like "Google Calendar authentication request". */
			message = g_strdup_printf (_("%s Calendar authentication request"), service_display_name);
			break;
		case TYPE_MEMO_LIST:
			/* Translators: The %s is replaced with an OAuth2 service display name, like the strings from "OAuth2Service" translation context,
			   thus it can form a string like "Google Memo List authentication request". */
			message = g_strdup_printf (_("%s Memo List authentication request"), service_display_name);
			break;
		case TYPE_TASK_LIST:
			/* Translators: The %s is replaced with an OAuth2 service display name, like the strings from "OAuth2Service" translation context,
			   thus it can form a string like "Google Task List authentication request". */
			message = g_strdup_printf (_("%s Task List authentication request"), service_display_name);
			break;
		case TYPE_MAIL_ACCOUNT:
		case TYPE_MAIL_TRANSPORT:
			/* Translators: The %s is replaced with an OAuth2 service display name, like the strings from "OAuth2Service" translation context,
			   thus it can form a string like "Google Mail authentication request". */
			message = g_strdup_printf (_("%s Mail authentication request"), service_display_name);
			break;
		default:  /* generic account prompt */
			/* Translators: The %s is replaced with an OAuth2 service display name, like the strings from "OAuth2Service" translation context,
			   thus it can form a string like "Google account authentication request". */
			message = g_strdup_printf (_("%s account authentication request"), service_display_name);
			break;
	}

	display_name = e_util_get_source_full_name (registry, source);
	description = g_string_sized_new (256);

	g_string_append_printf (description, "<big><b>%s</b></big>\n\n", message);
	switch (type) {
		case TYPE_ADDRESS_BOOK:
			g_string_append_printf (description,
				/* Translators: The first %s is replaced with an OAuth2 service display name, like the strings from "OAuth2Service" translation context,
				   thus it can form a string like "Login to your Google account and...". The second %s is the actual source display name,
				   like "On This Computer : Personal". */
				_("Login to your %s account and accept conditions in order to access your address book “%s”."), service_display_name, display_name);
			break;
		case TYPE_CALENDAR:
			g_string_append_printf (description,
				/* Translators: The first %s is replaced with an OAuth2 service display name, like the strings from "OAuth2Service" translation context,
				   thus it can form a string like "Login to your Google account and...". The second %s is the actual source display name,
				   like "On This Computer : Personal". */
				_("Login to your %s account and accept conditions in order to access your calendar “%s”."), service_display_name, display_name);
			break;
		case TYPE_MAIL_ACCOUNT:
			g_string_append_printf (description,
				/* Translators: The first %s is replaced with an OAuth2 service display name, like the strings from "OAuth2Service" translation context,
				   thus it can form a string like "Login to your Google account and...". The second %s is the actual source display name,
				   like "On This Computer : Personal". */
				_("Login to your %s account and accept conditions in order to access your mail account “%s”."), service_display_name, display_name);
			break;
		case TYPE_MAIL_TRANSPORT:
			g_string_append_printf (description,
				/* Translators: The first %s is replaced with an OAuth2 service display name, like the strings from "OAuth2Service" translation context,
				   thus it can form a string like "Login to your Google account and...". The second %s is the actual source display name,
				   like "On This Computer : Personal". */
				_("Login to your %s account and accept conditions in order to access your mail transport “%s”."), service_display_name, display_name);
			break;
		case TYPE_MEMO_LIST:
			g_string_append_printf (description,
				/* Translators: The first %s is replaced with an OAuth2 service display name, like the strings from "OAuth2Service" translation context,
				   thus it can form a string like "Login to your Google account and...". The second %s is the actual source display name,
				   like "On This Computer : Personal". */
				_("Login to your %s account and accept conditions in order to access your memo list “%s”."), service_display_name, display_name);
			break;
		case TYPE_TASK_LIST:
			g_string_append_printf (description,
				/* Translators: The first %s is replaced with an OAuth2 service display name, like the strings from "OAuth2Service" translation context,
				   thus it can form a string like "Login to your Google account and...". The second %s is the actual source display name,
				   like "On This Computer : Personal". */
				_("Login to your %s account and accept conditions in order to access your task list “%s”."), service_display_name, display_name);
			break;
		default:  /* generic account prompt */
			g_string_append_printf (description,
				/* Translators: The first %s is replaced with an OAuth2 service display name, like the strings from "OAuth2Service" translation context,
				   thus it can form a string like "Login to your Google account and...". The second %s is the actual source display name,
				   like "On This Computer : Personal". */
				_("Login to your %s account and accept conditions in order to access your account “%s”."), service_display_name, display_name);
			break;
	}

	*prompt_title = message;
	*prompt_description = description;

	g_free (display_name);
}

#ifdef WITH_WEBKITGTK

static gchar *
credentials_prompter_impl_oauth2_sanitize_host (gchar *host)
{
	if (!host || !*host)
		return host;

	if (*host == '[' && strchr (host, ':')) {
		gint len = strlen (host);

		if (len > 2 && host[len - 1] == ']') {
			memmove (host, host + 1, len - 2);
			host[len - 2] = '\0';
		}
	}

	return host;
}

static void
credentials_prompter_impl_oauth2_set_proxy (WebKitWebContext *web_context,
					    ESourceRegistry *registry,
					    ESource *auth_source)
{
	ESource *proxy_source = NULL;

	if (E_IS_SOURCE (auth_source) &&
	    e_source_has_extension (auth_source, E_SOURCE_EXTENSION_AUTHENTICATION)) {
		ESourceAuthentication *auth_extension;
		gchar *uid;

		auth_extension = e_source_get_extension (auth_source, E_SOURCE_EXTENSION_AUTHENTICATION);
		uid = e_source_authentication_dup_proxy_uid (auth_extension);

		if (uid) {
			proxy_source = e_source_registry_ref_source (registry, uid);
			g_free (uid);
		}
	}

	if (!proxy_source)
		proxy_source = e_source_registry_ref_builtin_proxy (registry);

	if (proxy_source && e_source_has_extension (proxy_source, E_SOURCE_EXTENSION_PROXY)) {
		ESourceProxy *proxy;
		WebKitWebsiteDataManager *data_manager;
		WebKitNetworkProxySettings *proxy_settings = NULL;
		GUri *guri;
		gchar **ignore_hosts = NULL;
		gchar *tmp;
		guint16 port;

		proxy = e_source_get_extension (proxy_source, E_SOURCE_EXTENSION_PROXY);
		data_manager = webkit_web_context_get_website_data_manager (web_context);

		switch (e_source_proxy_get_method (proxy)) {
		case E_PROXY_METHOD_DEFAULT:
			webkit_website_data_manager_set_network_proxy_settings (data_manager, WEBKIT_NETWORK_PROXY_MODE_DEFAULT, NULL);
			break;
		case E_PROXY_METHOD_MANUAL:
			ignore_hosts = e_source_proxy_dup_ignore_hosts (proxy);

			tmp = credentials_prompter_impl_oauth2_sanitize_host (e_source_proxy_dup_socks_host (proxy));
			if (tmp && *tmp) {
				port = e_source_proxy_get_socks_port (proxy);
				guri = g_uri_build (G_URI_FLAGS_PARSE_RELAXED | SOUP_HTTP_URI_FLAGS, "socks", NULL, tmp, port ? port : -1, "", NULL, NULL);
				g_free (tmp);
				tmp = g_uri_to_string_partial (guri, G_URI_HIDE_NONE);
				proxy_settings = webkit_network_proxy_settings_new (tmp, (const gchar * const *) ignore_hosts);
				webkit_network_proxy_settings_add_proxy_for_scheme (proxy_settings, "socks", tmp);
				g_uri_unref (guri);
			} else {
				proxy_settings = webkit_network_proxy_settings_new (NULL, (const gchar * const *) ignore_hosts);
			}
			g_free (tmp);

			tmp = credentials_prompter_impl_oauth2_sanitize_host (e_source_proxy_dup_http_host (proxy));
			if (tmp && *tmp) {
				port = e_source_proxy_get_http_port (proxy);
				if (e_source_proxy_get_http_use_auth (proxy)) {
					gchar *user, *password;

					user = e_source_proxy_dup_http_auth_user (proxy);
					password = e_source_proxy_dup_http_auth_password (proxy);

					guri = g_uri_build_with_user (G_URI_FLAGS_PARSE_RELAXED | SOUP_HTTP_URI_FLAGS, "http",
						user, password, NULL, tmp, port ? port : -1, "", NULL, NULL);

					e_util_safe_free_string (password);
					g_free (user);
				} else {
					guri = g_uri_build (G_URI_FLAGS_PARSE_RELAXED | SOUP_HTTP_URI_FLAGS, "http", NULL, tmp, port ? port : -1, "", NULL, NULL);
				}
				g_free (tmp);
				tmp = g_uri_to_string_partial (guri, G_URI_HIDE_NONE);
				webkit_network_proxy_settings_add_proxy_for_scheme (proxy_settings, "http", tmp);
				g_uri_unref (guri);
			}
			g_free (tmp);

			tmp = credentials_prompter_impl_oauth2_sanitize_host (e_source_proxy_dup_https_host (proxy));
			if (tmp && *tmp) {
				port = e_source_proxy_get_https_port (proxy);
				guri = g_uri_build (G_URI_FLAGS_PARSE_RELAXED | SOUP_HTTP_URI_FLAGS, "http", NULL, tmp, port ? port : -1, "", NULL, NULL);
				g_free (tmp);
				tmp = g_uri_to_string_partial (guri, G_URI_HIDE_NONE);
				webkit_network_proxy_settings_add_proxy_for_scheme (proxy_settings, "https", tmp);
				g_uri_unref (guri);
			}
			g_free (tmp);

			webkit_website_data_manager_set_network_proxy_settings (data_manager, WEBKIT_NETWORK_PROXY_MODE_CUSTOM, proxy_settings);
			break;
		case E_PROXY_METHOD_AUTO:
			/* not supported by WebKitGTK */
			break;
		case E_PROXY_METHOD_NONE:
			webkit_website_data_manager_set_network_proxy_settings (data_manager, WEBKIT_NETWORK_PROXY_MODE_NO_PROXY, NULL);
			break;
		}

		if (proxy_settings)
			webkit_network_proxy_settings_free (proxy_settings);

		g_strfreev (ignore_hosts);
	}

	g_clear_object (&proxy_source);
}
#endif /* WITH_WEBKITGTK */

static void
cpi_oauth2_url_entry_icon_release_cb (GtkEntry *entry,
				      GtkEntryIconPosition icon_position,
				      #if !GTK_CHECK_VERSION (4, 0, 0)
				      GdkEvent *event,
				      #endif
				      gpointer user_data)
{
	ECredentialsPrompterImplOAuth2 *prompter_oauth2 = user_data;
	gpointer toplevel;

	#if GTK_CHECK_VERSION (4, 0, 0)
	toplevel = GTK_WIDGET (entry);
	while (toplevel && !GTK_IS_WINDOW (toplevel)) {
		toplevel = gtk_widget_get_parent (toplevel);
	}
	#else
	toplevel = gtk_widget_get_toplevel (GTK_WIDGET (entry));
	toplevel = GTK_IS_WINDOW (toplevel) ? toplevel : NULL;
	#endif

	if (icon_position == GTK_ENTRY_ICON_SECONDARY) {
		#if !GTK_CHECK_VERSION (4, 0, 0)
		GError *error = NULL;
		#endif
		gchar *uri;

		uri = cpi_oauth2_create_auth_uri (prompter_oauth2->priv->service, prompter_oauth2->priv->cred_source);
		g_return_if_fail (uri != NULL);

		if (cpi_oauth2_get_debug ())
			e_util_debug_print ("OAuth2", "Opening URI in browser: '%s'\n", uri);

		#if GTK_CHECK_VERSION (4, 0, 0)
		gtk_show_uri (toplevel, uri, GDK_CURRENT_TIME);
		gtk_notebook_set_current_page (prompter_oauth2->priv->notebook, 0);
		#else
		if (!
			#if GTK_CHECK_VERSION (3, 22, 0)
			gtk_show_uri_on_window (toplevel ? GTK_WINDOW (toplevel) : NULL,
			#else
			gtk_show_uri (toplevel ? gtk_widget_get_screen (tolevel) : NULL,
			#endif
			uri, GDK_CURRENT_TIME, &error)
		) {
			gchar *msg = g_strdup_printf (_("Failed to open browser: %s"), error ? error->message : _("Unknown error"));
			cpi_oauth2_show_error (prompter_oauth2, "Failed to open browser", msg);
			g_free (msg);
		} else {
			gtk_notebook_set_current_page (prompter_oauth2->priv->notebook, 0);
		}

		g_clear_error (&error);
		#endif
		g_free (uri);
	}
}

static void
cpi_oauth2_manual_continue_clicked_cb (GtkButton *button,
				       gpointer user_data)
{
	ECredentialsPrompterImplOAuth2 *prompter_oauth2 = user_data;
	const gchar *auth_code;

	g_return_if_fail (E_IS_CREDENTIALS_PROMPTER_IMPL_OAUTH2 (prompter_oauth2));

	auth_code = _libedataserverui_entry_get_text (prompter_oauth2->priv->auth_code_entry);

	if (cpi_oauth2_get_debug ())
		e_util_debug_print ("OAuth2", "Continue with user-entered authorization code: '%s'\n", auth_code);

	cpi_oauth2_test_authorization_code (prompter_oauth2, g_strdup (auth_code));
}

static void
cpi_oauth2_auth_code_entry_changed_cb (GtkEntry *entry,
				       gpointer user_data)
{
	GtkWidget *button = user_data;
	const gchar *text;

	text = _libedataserverui_entry_get_text (entry);

	gtk_widget_set_sensitive (button, text && *text);
}

static gboolean
e_credentials_prompter_impl_oauth2_show_dialog (ECredentialsPrompterImplOAuth2 *prompter_oauth2)
{
	GtkWidget *dialog, *content_area, *widget, *vbox, *hbox, *url_entry;
	GtkStyleContext *style_context;
	GtkGrid *grid;
	GtkWindow *dialog_parent;
	ECredentialsPrompter *prompter;
#ifdef WITH_WEBKITGTK
	GtkScrolledWindow *scrolled_window;
	GtkWidget *progress_bar;
	WebKitSettings *webkit_settings;
	WebKitWebContext *web_context;
#endif /* WITH_WEBKITGTK */
	gchar *title, *uri;
	GString *info_markup;
	gint row = 0;
	gboolean success;
#if !GTK_CHECK_VERSION(4, 0, 0)
	GtkCssProvider *css_provider;
	GError *error = NULL;
#endif

	g_return_val_if_fail (E_IS_CREDENTIALS_PROMPTER_IMPL_OAUTH2 (prompter_oauth2), FALSE);
	g_return_val_if_fail (prompter_oauth2->priv->prompt_id != NULL, FALSE);
	g_return_val_if_fail (prompter_oauth2->priv->dialog == NULL, FALSE);
	g_return_val_if_fail (prompter_oauth2->priv->service != NULL, FALSE);

	prompter = e_credentials_prompter_impl_get_credentials_prompter (E_CREDENTIALS_PROMPTER_IMPL (prompter_oauth2));
	g_return_val_if_fail (prompter != NULL, FALSE);

	dialog_parent = e_credentials_prompter_get_dialog_parent_full (prompter, prompter_oauth2->priv->auth_source);

	credentials_prompter_impl_oauth2_get_prompt_strings (e_credentials_prompter_get_registry (prompter),
		prompter_oauth2->priv->auth_source,
		e_oauth2_service_get_display_name (prompter_oauth2->priv->service),
		&title, &info_markup);
	if (prompter_oauth2->priv->error_text && *prompter_oauth2->priv->error_text) {
		gchar *escaped = g_markup_printf_escaped ("%s", prompter_oauth2->priv->error_text);

		g_string_append_printf (info_markup, "\n\n%s", escaped);
		g_free (escaped);
	}

	dialog = gtk_dialog_new_with_buttons (title, dialog_parent, GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
		_("_Cancel"), GTK_RESPONSE_CANCEL,
		NULL);

#ifdef WITH_WEBKITGTK
	gtk_window_set_default_size (GTK_WINDOW (dialog), 400, 680);
#endif
	gtk_widget_set_name (dialog, "oauth2-prompt");

	#if !GTK_CHECK_VERSION(4, 0, 0)
	css_provider = gtk_css_provider_new ();
	gtk_css_provider_load_from_data (css_provider,
		"#oauth2-prompt { -GtkDialog-action-area-border:0px; -GtkDialog-content-area-border:0px; }",
		-1, &error);
	style_context = gtk_widget_get_style_context (dialog);
	if (error == NULL) {
		gtk_style_context_add_provider (
			style_context,
			GTK_STYLE_PROVIDER (css_provider),
			GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
	} else {
		g_warning ("%s: %s", G_STRFUNC, error->message);
		g_clear_error (&error);
	}
	g_object_unref (css_provider);
	#endif

	prompter_oauth2->priv->dialog = GTK_DIALOG (dialog);
	gtk_window_set_resizable (GTK_WINDOW (dialog), TRUE);
	if (dialog_parent)
		gtk_window_set_transient_for (GTK_WINDOW (dialog), dialog_parent);
#if !GTK_CHECK_VERSION(4, 0, 0)
	gtk_window_set_position (GTK_WINDOW (dialog), GTK_WIN_POS_CENTER_ON_PARENT);
	gtk_container_set_border_width (GTK_CONTAINER (dialog), 6);
#endif

	content_area = gtk_dialog_get_content_area (prompter_oauth2->priv->dialog);

	/* Override GtkDialog defaults */
	gtk_box_set_spacing (GTK_BOX (content_area), 12);
#if !GTK_CHECK_VERSION(4, 0, 0)
	gtk_container_set_border_width (GTK_CONTAINER (content_area), 0);
#endif

	grid = GTK_GRID (gtk_grid_new ());
	gtk_grid_set_column_spacing (grid, 12);
	gtk_grid_set_row_spacing (grid, 6);

	_libedataserverui_box_pack_start (GTK_BOX (content_area), GTK_WIDGET (grid), FALSE, TRUE, 0);

	/* Info Label */
	widget = gtk_label_new (NULL);
	gtk_label_set_markup (GTK_LABEL (widget), info_markup->str);
	g_object_set (
		G_OBJECT (widget),
		"hexpand", TRUE,
		"vexpand", FALSE,
		"halign", GTK_ALIGN_FILL,
		"valign", GTK_ALIGN_CENTER,
		"width-chars", 60,
		"max-width-chars", 80,
		"xalign", 0.0,
		"wrap", TRUE,
		NULL);

	gtk_grid_attach (grid, widget, 0, row, 1, 1);
	row++;

	vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 1);
	g_object_set (
		G_OBJECT (vbox),
		"hexpand", TRUE,
		"vexpand", TRUE,
		"halign", GTK_ALIGN_FILL,
		"valign", GTK_ALIGN_FILL,
		NULL);

	gtk_grid_attach (grid, vbox, 0, row, 1, 1);

	hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 2);

	_libedataserverui_box_pack_start (GTK_BOX (vbox), hbox, FALSE, FALSE, 0);

	widget = gtk_label_new (_("URL:"));
	g_object_set (
		G_OBJECT (widget),
		"hexpand", FALSE,
		"vexpand", FALSE,
		"halign", GTK_ALIGN_START,
		"valign", GTK_ALIGN_CENTER,
		"xalign", 0.0,
		NULL);

	_libedataserverui_box_pack_start (GTK_BOX (hbox), widget, FALSE, FALSE, 0);

	url_entry = gtk_entry_new ();
	g_object_set (
		G_OBJECT (url_entry),
#if !GTK_CHECK_VERSION(4, 0, 0)
		"can-default", FALSE,
#endif
		"can-focus", FALSE,
		"hexpand", TRUE,
		"vexpand", FALSE,
		"halign", GTK_ALIGN_FILL,
		"valign", GTK_ALIGN_CENTER,
		"editable", FALSE,
		NULL);

	style_context = gtk_widget_get_style_context (url_entry);
	gtk_style_context_add_class (style_context, "label");
	gtk_style_context_set_state (style_context, GTK_STATE_FLAG_INSENSITIVE);

	gtk_entry_set_icon_tooltip_text (GTK_ENTRY (url_entry), GTK_ENTRY_ICON_SECONDARY, _("Click here to open the URL"));
	gtk_entry_set_icon_from_icon_name (GTK_ENTRY (url_entry), GTK_ENTRY_ICON_SECONDARY, "go-jump");

	g_signal_connect_object (
		url_entry, "icon-release",
		G_CALLBACK (cpi_oauth2_url_entry_icon_release_cb), prompter_oauth2, 0);
#if GTK_CHECK_VERSION(4, 0, 0)
	_libedataserverui_box_pack_start (GTK_BOX (hbox), url_entry, FALSE, FALSE, 2);
#else
	_libedataserverui_box_pack_start (GTK_BOX (hbox), url_entry, TRUE, TRUE, 2);
#endif

	widget = gtk_notebook_new ();
	g_object_set (
		G_OBJECT (widget),
		"hexpand", TRUE,
		"vexpand", TRUE,
		"halign", GTK_ALIGN_FILL,
		"valign", GTK_ALIGN_FILL,
		"show-border", FALSE,
		"show-tabs", FALSE,
		NULL);

	_libedataserverui_box_pack_start (GTK_BOX (vbox), widget, TRUE, TRUE, 0);

	prompter_oauth2->priv->notebook = GTK_NOTEBOOK (widget);

	vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 1);
	g_object_set (
		G_OBJECT (vbox),
		"hexpand", TRUE,
		"vexpand", TRUE,
		"halign", GTK_ALIGN_FILL,
		"valign", GTK_ALIGN_FILL,
		NULL);

	gtk_notebook_append_page (prompter_oauth2->priv->notebook, vbox, NULL);

	widget = gtk_label_new (_("Open the above URL in a browser and go through the OAuth2 wizard there. Copy the resulting authorization code below to continue the authentication process."));
	g_object_set (
		G_OBJECT (widget),
		"hexpand", FALSE,
		"vexpand", FALSE,
		"halign", GTK_ALIGN_START,
		"valign", GTK_ALIGN_START,
		"margin-top", 12,
		"margin-bottom", 12,
		"width-chars", 60,
		"max-width-chars", 80,
		"wrap", TRUE,
		"xalign", 0.0,
		"yalign", 0.0,
		NULL);

	_libedataserverui_box_pack_start (GTK_BOX (vbox), widget, FALSE, FALSE, 0);

	hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 2);
	g_object_set (
		G_OBJECT (hbox),
		"margin-bottom", 12,
		NULL);

	_libedataserverui_box_pack_start (GTK_BOX (vbox), hbox, FALSE, FALSE, 0);

	widget = gtk_entry_new ();
	g_object_set (
		G_OBJECT (widget),
		"hexpand", FALSE,
		"vexpand", FALSE,
		"halign", GTK_ALIGN_START,
		"valign", GTK_ALIGN_CENTER,
		NULL);

	prompter_oauth2->priv->auth_code_entry = GTK_ENTRY (widget);

	widget = gtk_label_new_with_mnemonic (_("_Authorization code:"));
	g_object_set (
		G_OBJECT (widget),
		"hexpand", FALSE,
		"vexpand", FALSE,
		"halign", GTK_ALIGN_START,
		"valign", GTK_ALIGN_CENTER,
		"mnemonic-widget", prompter_oauth2->priv->auth_code_entry,
		NULL);

	_libedataserverui_box_pack_start (GTK_BOX (hbox), widget, FALSE, FALSE, 0);
	_libedataserverui_box_pack_start (GTK_BOX (hbox), GTK_WIDGET (prompter_oauth2->priv->auth_code_entry), FALSE, FALSE, 0);

	widget = gtk_button_new_with_mnemonic (_("C_ontinue"));
	g_object_set (
		G_OBJECT (widget),
		"sensitive", FALSE,
		NULL);
	_libedataserverui_box_pack_start (GTK_BOX (hbox), widget, FALSE, FALSE, 0);

	g_signal_connect_object (widget, "clicked",
		G_CALLBACK (cpi_oauth2_manual_continue_clicked_cb), prompter_oauth2, 0);

	g_signal_connect_object (prompter_oauth2->priv->auth_code_entry, "changed",
		G_CALLBACK (cpi_oauth2_auth_code_entry_changed_cb), widget, 0);

	widget = gtk_label_new ("");
	g_object_set (
		G_OBJECT (widget),
		"hexpand", TRUE,
		"vexpand", TRUE,
		"halign", GTK_ALIGN_FILL,
		"valign", GTK_ALIGN_FILL,
		"margin-start", 12,
		"justify", GTK_JUSTIFY_LEFT,
		"xalign", 0.0,
		"yalign", 0.0,
		"selectable", TRUE,
		"max-width-chars", 80,
		"width-chars", 60,
		"wrap-mode", PANGO_WRAP_WORD_CHAR,
		"wrap", TRUE,
		NULL);

	_libedataserverui_box_pack_start (GTK_BOX (vbox), widget, FALSE, FALSE, 0);

	prompter_oauth2->priv->error_text_label = GTK_LABEL (widget);

#ifdef WITH_WEBKITGTK
	vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 1);
	g_object_set (
		G_OBJECT (vbox),
		"hexpand", TRUE,
		"vexpand", TRUE,
		"halign", GTK_ALIGN_FILL,
		"valign", GTK_ALIGN_FILL,
		NULL);

	gtk_notebook_append_page (prompter_oauth2->priv->notebook, vbox, NULL);

#if GTK_CHECK_VERSION(4, 0, 0)
	widget = gtk_scrolled_window_new ();
#else
	widget = gtk_scrolled_window_new (NULL, NULL);
#endif
	g_object_set (
		G_OBJECT (widget),
		"hexpand", TRUE,
		"vexpand", TRUE,
		"halign", GTK_ALIGN_FILL,
		"valign", GTK_ALIGN_FILL,
		"hscrollbar-policy", GTK_POLICY_AUTOMATIC,
		"vscrollbar-policy", GTK_POLICY_AUTOMATIC,
		NULL);

	_libedataserverui_box_pack_start (GTK_BOX (vbox), widget, TRUE, TRUE, 0);

	scrolled_window = GTK_SCROLLED_WINDOW (widget);

	webkit_settings = webkit_settings_new_with_settings (
		"auto-load-images", TRUE,
		"default-charset", "utf-8",
		"enable-html5-database", FALSE,
		"enable-dns-prefetching", FALSE,
		"enable-html5-local-storage", FALSE,
		"enable-offline-web-application-cache", FALSE,
		"enable-page-cache", FALSE,
		"enable-plugins", FALSE,
		"media-playback-allows-inline", FALSE,
		"hardware-acceleration-policy", WEBKIT_HARDWARE_ACCELERATION_POLICY_NEVER,
		NULL);

	web_context = webkit_web_context_new ();
	webkit_web_context_set_sandbox_enabled (web_context, TRUE);
	credentials_prompter_impl_oauth2_set_proxy (web_context,  e_credentials_prompter_get_registry (prompter), prompter_oauth2->priv->auth_source);

	widget = g_object_new (WEBKIT_TYPE_WEB_VIEW,
			"settings", webkit_settings,
			"web-context", web_context,
			NULL);

	g_object_set (
		G_OBJECT (widget),
		"hexpand", TRUE,
		"vexpand", TRUE,
		"halign", GTK_ALIGN_FILL,
		"valign", GTK_ALIGN_FILL,
		NULL);
#if GTK_CHECK_VERSION(4, 0, 0)
	gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (scrolled_window), widget);
#else
	gtk_container_add (GTK_CONTAINER (scrolled_window), widget);
#endif
	g_object_unref (webkit_settings);
	g_object_unref (web_context);

	prompter_oauth2->priv->web_view = WEBKIT_WEB_VIEW (widget);

	e_binding_bind_property (
		prompter_oauth2->priv->web_view, "uri",
		url_entry, "text",
		G_BINDING_DEFAULT);

	e_binding_bind_property (
		prompter_oauth2->priv->web_view, "uri",
		url_entry, "tooltip-text",
		G_BINDING_DEFAULT);

	progress_bar = gtk_progress_bar_new ();
	g_object_set (
		G_OBJECT (progress_bar),
		"hexpand", TRUE,
		"vexpand", FALSE,
		"halign", GTK_ALIGN_FILL,
		"valign", GTK_ALIGN_START,
		"orientation", GTK_ORIENTATION_HORIZONTAL,
		"ellipsize", PANGO_ELLIPSIZE_END,
		"fraction", 0.0,
		NULL);

	_libedataserverui_box_pack_start (GTK_BOX (vbox), progress_bar, FALSE, FALSE, 0);
#endif /* WITH_WEBKITGTK */

#if !GTK_CHECK_VERSION(4, 0, 0)
	gtk_widget_show_all (GTK_WIDGET (grid));
#endif

#ifdef WITH_WEBKITGTK
	/* Switch to the last page, to prefer the built-in browser */
	gtk_notebook_set_current_page (prompter_oauth2->priv->notebook, -1);
#endif /* WITH_WEBKITGTK */

	uri = cpi_oauth2_create_auth_uri (prompter_oauth2->priv->service, prompter_oauth2->priv->cred_source);
	if (!uri) {
		success = FALSE;
	} else {
#ifdef WITH_WEBKITGTK
		WebKitWebView *web_view = prompter_oauth2->priv->web_view;
		gulong decide_policy_handler_id, load_finished_handler_id, progress_handler_id;

		decide_policy_handler_id = g_signal_connect (web_view, "decide-policy",
			G_CALLBACK (cpi_oauth2_decide_policy_cb), prompter_oauth2);
		load_finished_handler_id = g_signal_connect (web_view, "load-changed",
			G_CALLBACK (cpi_oauth2_document_load_changed_cb), prompter_oauth2);
		progress_handler_id = g_signal_connect (web_view, "notify::estimated-load-progress",
			G_CALLBACK (cpi_oauth2_notify_estimated_load_progress_cb), progress_bar);

		webkit_web_view_load_uri (web_view, uri);
#else /* WITH_WEBKITGTK */
		_libedataserverui_entry_set_text (GTK_ENTRY (url_entry), uri);
#endif /* WITH_WEBKITGTK */

		success = _libedataserverui_dialog_run (prompter_oauth2->priv->dialog) == GTK_RESPONSE_OK;

#ifdef WITH_WEBKITGTK
		if (decide_policy_handler_id)
			g_signal_handler_disconnect (web_view, decide_policy_handler_id);
		if (load_finished_handler_id)
			g_signal_handler_disconnect (web_view, load_finished_handler_id);
		if (progress_handler_id)
			g_signal_handler_disconnect (web_view, progress_handler_id);
#endif /* WITH_WEBKITGTK */
	}

	g_free (uri);

	if (prompter_oauth2->priv->cancellable)
		g_cancellable_cancel (prompter_oauth2->priv->cancellable);

#ifdef WITH_WEBKITGTK
	prompter_oauth2->priv->web_view = NULL;
#endif /* WITH_WEBKITGTK */
	prompter_oauth2->priv->dialog = NULL;
#if GTK_CHECK_VERSION(4, 0, 0)
	gtk_window_destroy (GTK_WINDOW (dialog));
#else
	gtk_widget_destroy (dialog);
#endif

	g_string_free (info_markup, TRUE);
	g_free (title);

	return success;
}

static void
e_credentials_prompter_impl_oauth2_free_prompt_data (ECredentialsPrompterImplOAuth2 *prompter_oauth2)
{
	g_return_if_fail (E_IS_CREDENTIALS_PROMPTER_IMPL_OAUTH2 (prompter_oauth2));

	prompter_oauth2->priv->prompt_id = NULL;

	g_clear_object (&prompter_oauth2->priv->auth_source);
	g_clear_object (&prompter_oauth2->priv->cred_source);
	g_clear_object (&prompter_oauth2->priv->service);

	g_free (prompter_oauth2->priv->error_text);
	prompter_oauth2->priv->error_text = NULL;

	e_named_parameters_free (prompter_oauth2->priv->credentials);
	prompter_oauth2->priv->credentials = NULL;
}

static gboolean
e_credentials_prompter_impl_oauth2_manage_dialog_idle_cb (gpointer user_data)
{
	ECredentialsPrompterImplOAuth2 *prompter_oauth2 = user_data;

	if (g_source_is_destroyed (g_main_current_source ()))
		return FALSE;

	g_return_val_if_fail (E_IS_CREDENTIALS_PROMPTER_IMPL_OAUTH2 (prompter_oauth2), FALSE);

	g_mutex_lock (&prompter_oauth2->priv->property_lock);
	if (g_source_get_id (g_main_current_source ()) == prompter_oauth2->priv->show_dialog_idle_id) {
		gboolean success, has_service;

		prompter_oauth2->priv->show_dialog_idle_id = 0;
		has_service = prompter_oauth2->priv->service != NULL;

		g_mutex_unlock (&prompter_oauth2->priv->property_lock);

		g_warn_if_fail (prompter_oauth2->priv->dialog == NULL);

		if (has_service)
			success = e_credentials_prompter_impl_oauth2_show_dialog (prompter_oauth2);
		else
			success = FALSE;

		e_credentials_prompter_impl_prompt_finish (
			E_CREDENTIALS_PROMPTER_IMPL (prompter_oauth2),
			prompter_oauth2->priv->prompt_id,
			success ? prompter_oauth2->priv->credentials : NULL);

		e_credentials_prompter_impl_oauth2_free_prompt_data (prompter_oauth2);
	} else {
		gpointer prompt_id = prompter_oauth2->priv->prompt_id;

		g_warning ("%s: Prompt's %p source cancelled? current:%d expected:%d", G_STRFUNC, prompt_id, (gint) g_source_get_id (g_main_current_source ()), (gint) prompter_oauth2->priv->show_dialog_idle_id);

		if (!prompter_oauth2->priv->show_dialog_idle_id)
			e_credentials_prompter_impl_oauth2_free_prompt_data (prompter_oauth2);

		g_mutex_unlock (&prompter_oauth2->priv->property_lock);

		if (prompt_id)
			e_credentials_prompter_impl_prompt_finish (E_CREDENTIALS_PROMPTER_IMPL (prompter_oauth2), prompt_id, NULL);
	}

	return FALSE;
}

static void
e_credentials_prompter_impl_oauth2_process_prompt (ECredentialsPrompterImpl *prompter_impl,
						   gpointer prompt_id,
						   ESource *auth_source,
						   ESource *cred_source,
						   const gchar *error_text,
						   const ENamedParameters *credentials)
{
	ECredentialsPrompterImplOAuth2 *prompter_oauth2;

	g_return_if_fail (E_IS_CREDENTIALS_PROMPTER_IMPL_OAUTH2 (prompter_impl));

	prompter_oauth2 = E_CREDENTIALS_PROMPTER_IMPL_OAUTH2 (prompter_impl);
	g_return_if_fail (prompter_oauth2->priv->prompt_id == NULL);

	g_mutex_lock (&prompter_oauth2->priv->property_lock);
	if (prompter_oauth2->priv->show_dialog_idle_id != 0) {
		g_mutex_unlock (&prompter_oauth2->priv->property_lock);
		g_warning ("%s: Already processing other prompt", G_STRFUNC);
		return;
	}
	g_mutex_unlock (&prompter_oauth2->priv->property_lock);

	prompter_oauth2->priv->prompt_id = prompt_id;
	prompter_oauth2->priv->auth_source = g_object_ref (auth_source);
	prompter_oauth2->priv->cred_source = g_object_ref (cred_source);
	prompter_oauth2->priv->service = e_oauth2_services_find (prompter_oauth2->priv->oauth2_services, cred_source);
	prompter_oauth2->priv->error_text = g_strdup (error_text);
	prompter_oauth2->priv->credentials = e_named_parameters_new_clone (credentials);
	prompter_oauth2->priv->cancellable = g_cancellable_new ();

	g_mutex_lock (&prompter_oauth2->priv->property_lock);
	prompter_oauth2->priv->refresh_failed_with_transport_error = FALSE;
	prompter_oauth2->priv->show_dialog_idle_id = g_idle_add (
		e_credentials_prompter_impl_oauth2_manage_dialog_idle_cb,
		prompter_oauth2);
	g_mutex_unlock (&prompter_oauth2->priv->property_lock);
}

static void
e_credentials_prompter_impl_oauth2_cancel_prompt (ECredentialsPrompterImpl *prompter_impl,
						  gpointer prompt_id)
{
	ECredentialsPrompterImplOAuth2 *prompter_oauth2;

	g_return_if_fail (E_IS_CREDENTIALS_PROMPTER_IMPL_OAUTH2 (prompter_impl));

	prompter_oauth2 = E_CREDENTIALS_PROMPTER_IMPL_OAUTH2 (prompter_impl);
	g_return_if_fail (prompter_oauth2->priv->prompt_id == prompt_id);

	if (prompter_oauth2->priv->cancellable)
		g_cancellable_cancel (prompter_oauth2->priv->cancellable);

	/* This also closes the dialog. */
	if (prompter_oauth2->priv->dialog)
		gtk_dialog_response (prompter_oauth2->priv->dialog, GTK_RESPONSE_CANCEL);
}

static void
e_credentials_prompter_impl_oauth2_constructed (GObject *object)
{
	ECredentialsPrompterImplOAuth2 *prompter_oauth2 = E_CREDENTIALS_PROMPTER_IMPL_OAUTH2 (object);

	/* Chain up to parent's method. */
	G_OBJECT_CLASS (e_credentials_prompter_impl_oauth2_parent_class)->constructed (object);

	if (prompter_oauth2->priv->oauth2_services) {
		ECredentialsPrompter *prompter;
		ECredentialsPrompterImpl *prompter_impl;
		GSList *services, *link;

		prompter_impl = E_CREDENTIALS_PROMPTER_IMPL (prompter_oauth2);
		prompter = E_CREDENTIALS_PROMPTER (e_extension_get_extensible (E_EXTENSION (prompter_impl)));

		services = e_oauth2_services_list (prompter_oauth2->priv->oauth2_services);

		for (link = services; link; link = g_slist_next (link)) {
			EOAuth2Service *service = link->data;

			if (service && e_oauth2_service_get_name (service)) {
				e_credentials_prompter_register_impl (prompter, e_oauth2_service_get_name (service), prompter_impl);
			}
		}

		g_slist_free_full (services, g_object_unref);
	}
}

static void
e_credentials_prompter_impl_oauth2_dispose (GObject *object)
{
	ECredentialsPrompterImplOAuth2 *prompter_oauth2 = E_CREDENTIALS_PROMPTER_IMPL_OAUTH2 (object);

	g_mutex_lock (&prompter_oauth2->priv->property_lock);
	if (prompter_oauth2->priv->show_dialog_idle_id) {
		g_source_remove (prompter_oauth2->priv->show_dialog_idle_id);
		prompter_oauth2->priv->show_dialog_idle_id = 0;
	}
	g_mutex_unlock (&prompter_oauth2->priv->property_lock);

	if (prompter_oauth2->priv->cancellable) {
		g_cancellable_cancel (prompter_oauth2->priv->cancellable);
		g_clear_object (&prompter_oauth2->priv->cancellable);
	}

	g_warn_if_fail (prompter_oauth2->priv->prompt_id == NULL);
	g_warn_if_fail (prompter_oauth2->priv->dialog == NULL);

	e_credentials_prompter_impl_oauth2_free_prompt_data (prompter_oauth2);

	/* Chain up to parent's method. */
	G_OBJECT_CLASS (e_credentials_prompter_impl_oauth2_parent_class)->dispose (object);
}

static void
e_credentials_prompter_impl_oauth2_finalize (GObject *object)
{
	ECredentialsPrompterImplOAuth2 *prompter_oauth2 = E_CREDENTIALS_PROMPTER_IMPL_OAUTH2 (object);

	g_clear_object (&prompter_oauth2->priv->oauth2_services);
	g_mutex_clear (&prompter_oauth2->priv->property_lock);

	/* Chain up to parent's method. */
	G_OBJECT_CLASS (e_credentials_prompter_impl_oauth2_parent_class)->finalize (object);
}

static void
e_credentials_prompter_impl_oauth2_class_init (ECredentialsPrompterImplOAuth2Class *class)
{
	/* No static known, rather figure them out in runtime */
	static const gchar *authentication_methods[] = {
		NULL
	};

	GObjectClass *object_class;
	ECredentialsPrompterImplClass *prompter_impl_class;

	object_class = G_OBJECT_CLASS (class);
	object_class->constructed = e_credentials_prompter_impl_oauth2_constructed;
	object_class->dispose = e_credentials_prompter_impl_oauth2_dispose;
	object_class->finalize = e_credentials_prompter_impl_oauth2_finalize;

	prompter_impl_class = E_CREDENTIALS_PROMPTER_IMPL_CLASS (class);
	prompter_impl_class->authentication_methods = (const gchar * const *) authentication_methods;
	prompter_impl_class->process_prompt = e_credentials_prompter_impl_oauth2_process_prompt;
	prompter_impl_class->cancel_prompt = e_credentials_prompter_impl_oauth2_cancel_prompt;
}

static void
e_credentials_prompter_impl_oauth2_init (ECredentialsPrompterImplOAuth2 *prompter_oauth2)
{
	prompter_oauth2->priv = e_credentials_prompter_impl_oauth2_get_instance_private (prompter_oauth2);

	g_mutex_init (&prompter_oauth2->priv->property_lock);

	prompter_oauth2->priv->oauth2_services = e_oauth2_services_new ();
}

/**
 * e_credentials_prompter_impl_oauth2_new:
 *
 * Creates a new instance of an #ECredentialsPrompterImplOAuth2.
 *
 * Returns: (transfer full): a newly created #ECredentialsPrompterImplOAuth2,
 *    which should be freed with g_object_unref() when no longer needed.
 *
 * Since: 3.28
 **/
ECredentialsPrompterImpl *
e_credentials_prompter_impl_oauth2_new (void)
{
	return g_object_new (E_TYPE_CREDENTIALS_PROMPTER_IMPL_OAUTH2, NULL);
}
