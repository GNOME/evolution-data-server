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

#include "e-credentials-prompter.h"
#include "e-credentials-prompter-impl-google.h"

#ifdef ENABLE_GOOGLE_AUTH
#include <webkit2/webkit2.h>

/* https://developers.google.com/identity/protocols/OAuth2InstalledApp */
#define GOOGLE_AUTH_URI "https://accounts.google.com/o/oauth2/auth"
#define GOOGLE_TOKEN_URI "https://www.googleapis.com/oauth2/v3/token"
#define GOOGLE_REDIRECT_URI "urn:ietf:wg:oauth:2.0:oob"

static const gchar *GOOGLE_SCOPE =
	/* GMail IMAP and SMTP access */
	"https://mail.google.com/ "
	/* Google Calendar API (CalDAV and GData) */
	"https://www.googleapis.com/auth/calendar "
	/* Google Contacts API (GData) */
	"https://www.google.com/m8/feeds/ "
	/* Google Contacts API (CardDAV) - undocumented */
	"https://www.googleapis.com/auth/carddav "
	/* Google Tasks - undocumented */
	"https://www.googleapis.com/auth/tasks";
#endif /* ENABLE_GOOGLE_AUTH */

struct _ECredentialsPrompterImplGooglePrivate {
	GMutex property_lock;

	gpointer prompt_id;
	ESource *auth_source;
	ESource *cred_source;
	gchar *error_text;
	ENamedParameters *credentials;
	gboolean refresh_failed_with_transport_error;

	GtkDialog *dialog;
#ifdef ENABLE_GOOGLE_AUTH
	WebKitWebView *web_view;
#endif
	gulong show_dialog_idle_id;

	GCancellable *cancellable;
};

G_DEFINE_TYPE (ECredentialsPrompterImplGoogle, e_credentials_prompter_impl_google, E_TYPE_CREDENTIALS_PROMPTER_IMPL)

#ifdef ENABLE_GOOGLE_AUTH
static gchar *
cpi_google_create_auth_uri (ESource *source)
{
	GHashTable *form;
	SoupURI *soup_uri;
	gchar *uri, *user = NULL;

	g_return_val_if_fail (E_IS_SOURCE (source), NULL);

	soup_uri = soup_uri_new (GOOGLE_AUTH_URI);
	form = g_hash_table_new (g_str_hash, g_str_equal);

	#define add_to_form(name, value) g_hash_table_insert (form, (gpointer) name, (gpointer) value)

	add_to_form ("response_type", "code");
	add_to_form ("client_id", GOOGLE_CLIENT_ID);
	add_to_form ("redirect_uri", GOOGLE_REDIRECT_URI);
	add_to_form ("scope", GOOGLE_SCOPE);

	if (e_source_has_extension (source, E_SOURCE_EXTENSION_AUTHENTICATION)) {
		ESourceAuthentication *auth_extension;

		auth_extension = e_source_get_extension (source, E_SOURCE_EXTENSION_AUTHENTICATION);
		user = e_source_authentication_dup_user (auth_extension);

		if (user && *user)
			add_to_form ("login_hint", user);
	}

	add_to_form ("include_granted_scopes", "false");

	#undef add_to_form

	soup_uri_set_query_from_form (soup_uri, form);

	uri = soup_uri_to_string (soup_uri, FALSE);

	soup_uri_free (soup_uri);
	g_hash_table_destroy (form);
	g_free (user);

	return uri;
}

static gchar *
cpi_google_create_token_post_data (const gchar *authorization_code)
{
	g_return_val_if_fail (authorization_code != NULL, NULL);

	return soup_form_encode (
		"code", authorization_code,
		"client_id", GOOGLE_CLIENT_ID,
		"client_secret", GOOGLE_CLIENT_SECRET,
		"redirect_uri", GOOGLE_REDIRECT_URI,
		"grant_type", "authorization_code",
		NULL);
}

static gchar *
cpi_google_create_refresh_token_post_data (const gchar *refresh_token)
{
	g_return_val_if_fail (refresh_token != NULL, NULL);

	return soup_form_encode (
		"refresh_token", refresh_token,
		"client_id", GOOGLE_CLIENT_ID,
		"client_secret", GOOGLE_CLIENT_SECRET,
		"grant_type", "refresh_token",
		NULL);
}

static void
cpi_google_setup_proxy_resolver (SoupSession *session,
				 ESourceRegistry *registry,
				 ESource *cred_source)
{
	ESourceAuthentication *auth_extension;
	ESource *source = NULL;
	gchar *uid;

	g_return_if_fail (SOUP_IS_SESSION (session));
	g_return_if_fail (E_IS_SOURCE_REGISTRY (registry));
	g_return_if_fail (E_IS_SOURCE (cred_source));

	if (!e_source_has_extension (cred_source, E_SOURCE_EXTENSION_AUTHENTICATION))
		return;

	auth_extension = e_source_get_extension (cred_source, E_SOURCE_EXTENSION_AUTHENTICATION);
	uid = e_source_authentication_dup_proxy_uid (auth_extension);
	if (uid != NULL) {
		source = e_source_registry_ref_source (registry, uid);

		g_free (uid);
	}

	if (source != NULL) {
		GProxyResolver *proxy_resolver;

		proxy_resolver = G_PROXY_RESOLVER (source);
		if (g_proxy_resolver_is_supported (proxy_resolver))
			g_object_set (session, SOUP_SESSION_PROXY_RESOLVER, proxy_resolver, NULL);

		g_object_unref (source);
	}
}

static void
cpi_google_abort_session_cb (GCancellable *cancellable,
			     SoupSession *session)
{
	soup_session_abort (session);
}

static guint
cpi_google_post_data_sync (const gchar *uri,
			   const gchar *post_data,
			   ESourceRegistry *registry,
			   ESource *cred_source,
			   GCancellable *cancellable,
			   gchar **out_response_data)
{
	SoupSession *session;
	SoupMessage *message;
	guint status_code = SOUP_STATUS_CANCELLED;

	g_return_val_if_fail (uri != NULL, SOUP_STATUS_MALFORMED);
	g_return_val_if_fail (post_data != NULL, SOUP_STATUS_MALFORMED);
	g_return_val_if_fail (E_IS_SOURCE_REGISTRY (registry), SOUP_STATUS_MALFORMED);
	g_return_val_if_fail (E_IS_SOURCE (cred_source), SOUP_STATUS_MALFORMED);
	g_return_val_if_fail (out_response_data != NULL, SOUP_STATUS_MALFORMED);

	*out_response_data = NULL;

	message = soup_message_new (SOUP_METHOD_POST, uri);
	g_return_val_if_fail (message != NULL, SOUP_STATUS_MALFORMED);

	soup_message_set_request (message, "application/x-www-form-urlencoded",
		SOUP_MEMORY_TEMPORARY, post_data, strlen (post_data));

	e_soup_ssl_trust_connect (message, cred_source);

	session = soup_session_new ();
	g_object_set (
		session,
		SOUP_SESSION_TIMEOUT, 90,
		SOUP_SESSION_SSL_STRICT, TRUE,
		SOUP_SESSION_SSL_USE_SYSTEM_CA_FILE, TRUE,
		SOUP_SESSION_ACCEPT_LANGUAGE_AUTO, TRUE,
		NULL);

	cpi_google_setup_proxy_resolver (session, registry, cred_source);

	soup_message_headers_append (message->request_headers, "Connection", "close");

	if (!g_cancellable_is_cancelled (cancellable)) {
		gulong cancel_handler_id = 0;

		if (cancellable)
			cancel_handler_id = g_cancellable_connect (cancellable, G_CALLBACK (cpi_google_abort_session_cb), session, NULL);

		status_code = soup_session_send_message (session, message);

		if (cancel_handler_id)
			g_cancellable_disconnect (cancellable, cancel_handler_id);
	}

	if (SOUP_STATUS_IS_SUCCESSFUL (status_code)) {
		if (message->response_body) {
			*out_response_data = g_strndup (message->response_body->data, message->response_body->length);
		} else {
			status_code = SOUP_STATUS_MALFORMED;
		}
	}

	g_object_unref (message);
	g_object_unref (session);

	return status_code;
}

static gboolean
cpi_google_update_prompter_credentials (GWeakRef *prompter_google_wr,
					const gchar *refresh_token,
					const gchar *access_token,
					const gchar *expires_in,
					GCancellable *cancellable)
{
	ECredentialsPrompterImplGoogle *prompter_google;
	gint64 expires_after_tm;
	gchar *expires_after;
	gboolean success = FALSE;

	g_return_val_if_fail (prompter_google_wr != NULL, FALSE);

	if (!refresh_token || !access_token || !expires_in)
		return FALSE;

	if (g_cancellable_is_cancelled (cancellable))
		return FALSE;

	expires_after_tm = g_get_real_time () / G_USEC_PER_SEC;
	expires_after_tm += g_ascii_strtoll (expires_in, NULL, 10);
	expires_after = g_strdup_printf ("%" G_GINT64_FORMAT, expires_after_tm);

	prompter_google = g_weak_ref_get (prompter_google_wr);
	if (prompter_google && !g_cancellable_is_cancelled (cancellable)) {
		gchar *secret = NULL;

		if (e_source_credentials_google_util_encode_to_secret (&secret,
			E_GOOGLE_SECRET_REFRESH_TOKEN, refresh_token,
			E_GOOGLE_SECRET_ACCESS_TOKEN, access_token,
			E_GOOGLE_SECRET_EXPIRES_AFTER, expires_after, NULL)) {
			e_named_parameters_set (prompter_google->priv->credentials,
				E_SOURCE_CREDENTIAL_GOOGLE_SECRET, secret);
			e_named_parameters_set (prompter_google->priv->credentials,
				E_SOURCE_CREDENTIAL_PASSWORD, access_token);

			success = TRUE;
		}

		g_free (secret);
	}
	g_clear_object (&prompter_google);

	g_free (expires_after);

	return success;
}

static void
e_credentials_prompter_impl_google_show_html (WebKitWebView *web_view,
					      const gchar *title,
					      const gchar *body_text)
{
	gchar *html;

	g_return_if_fail (WEBKIT_IS_WEB_VIEW (web_view));
	g_return_if_fail (title != NULL);
	g_return_if_fail (body_text != NULL);

	html = g_strdup_printf (
		"<html>"
		"<head><title>%s</title></head>"
		"<body><div style=\"font-size:12pt; font-family:Helvetica,Arial;\">%s</div></body>"
		"</html>",
		title,
		body_text);
	webkit_web_view_load_html (web_view, html, "none-local://");
	g_free (html);
}

static gboolean
e_credentials_prompter_impl_google_finish_dialog_idle_cb (gpointer user_data)
{
	ECredentialsPrompterImplGoogle *prompter_google = user_data;

	if (g_source_is_destroyed (g_main_current_source ()))
		return FALSE;

	g_return_val_if_fail (E_IS_CREDENTIALS_PROMPTER_IMPL_GOOGLE (prompter_google), FALSE);

	g_mutex_lock (&prompter_google->priv->property_lock);
	if (g_source_get_id (g_main_current_source ()) == prompter_google->priv->show_dialog_idle_id) {
		prompter_google->priv->show_dialog_idle_id = 0;
		g_mutex_unlock (&prompter_google->priv->property_lock);

		g_warn_if_fail (prompter_google->priv->dialog != NULL);

		if (e_named_parameters_exists (prompter_google->priv->credentials, E_SOURCE_CREDENTIAL_PASSWORD)) {
			gtk_dialog_response (prompter_google->priv->dialog, GTK_RESPONSE_OK);
		} else if (prompter_google->priv->error_text) {
			e_credentials_prompter_impl_google_show_html (prompter_google->priv->web_view,
				"Finished with error", prompter_google->priv->error_text);
		}
	} else {
		g_warning ("%s: Source was cancelled? current:%d expected:%d", G_STRFUNC, (gint) g_source_get_id (g_main_current_source ()), (gint) prompter_google->priv->show_dialog_idle_id);
		g_mutex_unlock (&prompter_google->priv->property_lock);
	}

	return FALSE;
}

typedef struct {
	GWeakRef *prompter_google; /* ECredentialsPrompterImplGoogle * */
	GCancellable *cancellable;
	ESource *cred_source;
	ESourceRegistry *registry;
	gchar *authorization_code;
} AccessTokenThreadData;

static void
access_token_thread_data_free (gpointer user_data)
{
	AccessTokenThreadData *td = user_data;

	if (td) {
		e_weak_ref_free (td->prompter_google);
		g_clear_object (&td->cancellable);
		g_clear_object (&td->cred_source);
		g_clear_object (&td->registry);
		g_free (td->authorization_code);
		g_free (td);
	}
}

static gpointer
cpi_google_get_access_token_thread (gpointer user_data)
{
	AccessTokenThreadData *td = user_data;
	ECredentialsPrompterImplGoogle *prompter_google;
	GCancellable *cancellable;
	gchar *post_data, *response_json = NULL;
	guint soup_status;
	gboolean success = FALSE;

	g_return_val_if_fail (td != NULL, NULL);

	cancellable = td->cancellable;

	if (g_cancellable_is_cancelled (cancellable)) {
		soup_status = SOUP_STATUS_CANCELLED;
		goto exit;
	}


	post_data = cpi_google_create_token_post_data (td->authorization_code);
	g_warn_if_fail (post_data != NULL);
	if (!post_data) {
		soup_status = SOUP_STATUS_NO_CONTENT;
		goto exit;
	}

	soup_status = cpi_google_post_data_sync (GOOGLE_TOKEN_URI, post_data,
		td->registry, td->cred_source, cancellable, &response_json);

	if (SOUP_STATUS_IS_SUCCESSFUL (soup_status) && response_json) {
		gchar *access_token = NULL, *refresh_token = NULL, *expires_in = NULL, *token_type = NULL;

		if (e_source_credentials_google_util_decode_from_secret (response_json,
			"access_token", &access_token,
			"refresh_token", &refresh_token,
			"expires_in", &expires_in,
			"token_type", &token_type,
			NULL) && access_token && refresh_token && expires_in && token_type) {

			g_warn_if_fail (g_str_equal (token_type, "Bearer"));

			if (!cpi_google_update_prompter_credentials (td->prompter_google,
				refresh_token, access_token, expires_in, cancellable)) {
				soup_status = SOUP_STATUS_MALFORMED;
			}
		} else {
			soup_status = SOUP_STATUS_MALFORMED;
		}

		g_free (access_token);
		g_free (refresh_token);
		g_free (expires_in);
		g_free (token_type);
	}

	g_free (response_json);
	g_free (post_data);

 exit:
	prompter_google = g_weak_ref_get (td->prompter_google);
	if (prompter_google && !g_cancellable_is_cancelled (cancellable)) {
		if (!success) {
			g_free (prompter_google->priv->error_text);
			prompter_google->priv->error_text = NULL;

			prompter_google->priv->error_text = g_strdup_printf (
				_("Failed to obtain access token from address “%s”. Error code %d (%s)"),
				GOOGLE_TOKEN_URI, soup_status, soup_status_get_phrase (soup_status));
		}

		g_mutex_lock (&prompter_google->priv->property_lock);
		prompter_google->priv->show_dialog_idle_id = g_idle_add (
			e_credentials_prompter_impl_google_finish_dialog_idle_cb,
			prompter_google);
		g_mutex_unlock (&prompter_google->priv->property_lock);
	}

	g_clear_object (&prompter_google);

	access_token_thread_data_free (td);

	return NULL;
}

static void
cpi_google_document_load_changed_cb (WebKitWebView *web_view,
				     WebKitLoadEvent load_event,
				     ECredentialsPrompterImplGoogle *prompter_google)
{
	const gchar *title;

	g_return_if_fail (WEBKIT_IS_WEB_VIEW (web_view));
	g_return_if_fail (E_IS_CREDENTIALS_PROMPTER_IMPL_GOOGLE (prompter_google));

	if (load_event != WEBKIT_LOAD_FINISHED)
		return;

	title = webkit_web_view_get_title (web_view);
	if (!title)
		return;

	if (g_ascii_strncasecmp (title, "Denied ", 7) == 0) {
		g_cancellable_cancel (prompter_google->priv->cancellable);
		gtk_dialog_response (prompter_google->priv->dialog, GTK_RESPONSE_CANCEL);
		return;
	}

	if (g_ascii_strncasecmp (title, "Success code=", 13) == 0) {
		ECredentialsPrompter *prompter;
		ECredentialsPrompterImpl *prompter_impl;
		AccessTokenThreadData *td;
		GThread *thread;

		e_credentials_prompter_impl_google_show_html (web_view,
			"Checking returned code", _("Requesting access token, please wait..."));

		gtk_widget_set_sensitive (GTK_WIDGET (web_view), FALSE);

		e_named_parameters_set (prompter_google->priv->credentials, E_SOURCE_CREDENTIAL_PASSWORD, NULL);

		prompter_impl = E_CREDENTIALS_PROMPTER_IMPL (prompter_google);
		prompter = e_credentials_prompter_impl_get_credentials_prompter (prompter_impl);

		td = g_new0 (AccessTokenThreadData, 1);
		td->prompter_google = e_weak_ref_new (prompter_google);
		td->cancellable = g_object_ref (prompter_google->priv->cancellable);
		td->cred_source = g_object_ref (prompter_google->priv->cred_source);
		td->registry = g_object_ref (e_credentials_prompter_get_registry (prompter));
		td->authorization_code = g_strdup (title + 13);

		thread = g_thread_new (G_STRFUNC, cpi_google_get_access_token_thread, td);
		g_thread_unref (thread);

		return;
	}
}

static void
cpi_google_notify_estimated_load_progress_cb (WebKitWebView *web_view,
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

static void
credentials_prompter_impl_google_get_prompt_strings (ESourceRegistry *registry,
						     ESource *source,
						     gchar **prompt_title,
						     GString **prompt_description)
{
	GString *description;
	const gchar *message;
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
			message = _("Google Address book authentication request");
			break;
		case TYPE_CALENDAR:
			message = _("Google Calendar authentication request");
			break;
		case TYPE_MEMO_LIST:
			message = _("Google Memo List authentication request");
			break;
		case TYPE_TASK_LIST:
			message = _("Google Task List authentication request");
			break;
		case TYPE_MAIL_ACCOUNT:
		case TYPE_MAIL_TRANSPORT:
			message = _("Google Mail authentication request");
			break;
		default:  /* generic account prompt */
			message = _("Google account authentication request");
			break;
	}

	display_name = e_util_get_source_full_name (registry, source);
	description = g_string_sized_new (256);

	g_string_append_printf (description, "<big><b>%s</b></big>\n\n", message);
	switch (type) {
		case TYPE_ADDRESS_BOOK:
			g_string_append_printf (description,
				_("Login to your Google account and accept conditions in order to access your address book “%s”."), display_name);
			break;
		case TYPE_CALENDAR:
			g_string_append_printf (description,
				_("Login to your Google account and accept conditions in order to access your calendar “%s”."), display_name);
			break;
		case TYPE_MAIL_ACCOUNT:
			g_string_append_printf (description,
				_("Login to your Google account and accept conditions in order to access your mail account “%s”."), display_name);
			break;
		case TYPE_MAIL_TRANSPORT:
			g_string_append_printf (description,
				_("Login to your Google account and accept conditions in order to access your mail transport “%s”."), display_name);
			break;
		case TYPE_MEMO_LIST:
			g_string_append_printf (description,
				_("Login to your Google account and accept conditions in order to access your memo list “%s”."), display_name);
			break;
		case TYPE_TASK_LIST:
			g_string_append_printf (description,
				_("Login to your Google account and accept conditions in order to access your task list “%s”."), display_name);
			break;
		default:  /* generic account prompt */
			g_string_append_printf (description,
				_("Login to your Google account and accept conditions in order to access your account “%s”."), display_name);
			break;
	}

	*prompt_title = g_strdup (message);
	*prompt_description = description;

	g_free (display_name);
}
#endif /* ENABLE_GOOGLE_AUTH */

static gboolean
e_credentials_prompter_impl_google_show_dialog (ECredentialsPrompterImplGoogle *prompter_google)
{
#ifdef ENABLE_GOOGLE_AUTH
	GtkWidget *dialog, *content_area, *widget, *progress_bar, *vbox;
	GtkGrid *grid;
	GtkScrolledWindow *scrolled_window;
	GtkWindow *dialog_parent;
	ECredentialsPrompter *prompter;
	gchar *title, *uri;
	GString *info_markup;
	gint row = 0;
	gboolean success;

	g_return_val_if_fail (E_IS_CREDENTIALS_PROMPTER_IMPL_GOOGLE (prompter_google), FALSE);
	g_return_val_if_fail (prompter_google->priv->prompt_id != NULL, FALSE);
	g_return_val_if_fail (prompter_google->priv->dialog == NULL, FALSE);

	prompter = e_credentials_prompter_impl_get_credentials_prompter (E_CREDENTIALS_PROMPTER_IMPL (prompter_google));
	g_return_val_if_fail (prompter != NULL, FALSE);

	dialog_parent = e_credentials_prompter_get_dialog_parent (prompter);

	credentials_prompter_impl_google_get_prompt_strings (e_credentials_prompter_get_registry (prompter),
		prompter_google->priv->auth_source, &title, &info_markup);
	if (prompter_google->priv->error_text && *prompter_google->priv->error_text) {
		gchar *escaped = g_markup_printf_escaped ("%s", prompter_google->priv->error_text);

		g_string_append_printf (info_markup, "\n\n%s", escaped);
		g_free (escaped);
	}

	dialog = gtk_dialog_new_with_buttons (title, dialog_parent, GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
		_("_Cancel"), GTK_RESPONSE_CANCEL,
		NULL);

	gtk_window_set_default_size (GTK_WINDOW (dialog), 320, 480);

	prompter_google->priv->dialog = GTK_DIALOG (dialog);
	gtk_window_set_resizable (GTK_WINDOW (dialog), TRUE);
	if (dialog_parent)
		gtk_window_set_transient_for (GTK_WINDOW (dialog), dialog_parent);
	gtk_window_set_position (GTK_WINDOW (dialog), GTK_WIN_POS_CENTER_ON_PARENT);
	gtk_container_set_border_width (GTK_CONTAINER (dialog), 12);

	content_area = gtk_dialog_get_content_area (prompter_google->priv->dialog);

	/* Override GtkDialog defaults */
	gtk_box_set_spacing (GTK_BOX (content_area), 12);
	gtk_container_set_border_width (GTK_CONTAINER (content_area), 0);

	grid = GTK_GRID (gtk_grid_new ());
	gtk_grid_set_column_spacing (grid, 12);
	gtk_grid_set_row_spacing (grid, 6);

	gtk_box_pack_start (GTK_BOX (content_area), GTK_WIDGET (grid), FALSE, TRUE, 0);

	/* Info Label */
	widget = gtk_label_new (NULL);
	gtk_label_set_line_wrap (GTK_LABEL (widget), TRUE);
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

	widget = gtk_scrolled_window_new (NULL, NULL);
	g_object_set (
		G_OBJECT (widget),
		"hexpand", TRUE,
		"vexpand", TRUE,
		"halign", GTK_ALIGN_FILL,
		"valign", GTK_ALIGN_FILL,
		"hscrollbar-policy", GTK_POLICY_AUTOMATIC,
		"vscrollbar-policy", GTK_POLICY_AUTOMATIC,
		NULL);

	gtk_box_pack_start (GTK_BOX (vbox), widget, TRUE, TRUE, 0);

	scrolled_window = GTK_SCROLLED_WINDOW (widget);

	widget = webkit_web_view_new ();
	g_object_set (
		G_OBJECT (widget),
		"hexpand", TRUE,
		"vexpand", TRUE,
		"halign", GTK_ALIGN_FILL,
		"valign", GTK_ALIGN_FILL,
		NULL);
	gtk_container_add (GTK_CONTAINER (scrolled_window), widget);

	prompter_google->priv->web_view = WEBKIT_WEB_VIEW (widget);

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
	gtk_style_context_add_class (gtk_widget_get_style_context (progress_bar), GTK_STYLE_CLASS_OSD);

	gtk_box_pack_start (GTK_BOX (vbox), progress_bar, FALSE, FALSE, 0);

	gtk_widget_show_all (GTK_WIDGET (grid));

	uri = cpi_google_create_auth_uri (prompter_google->priv->cred_source);
	if (!uri) {
		success = FALSE;
	} else {
		WebKitWebView *web_view = prompter_google->priv->web_view;
		gulong load_finished_handler_id, progress_handler_id;

		load_finished_handler_id = g_signal_connect (web_view, "load-changed",
			G_CALLBACK (cpi_google_document_load_changed_cb), prompter_google);
		progress_handler_id = g_signal_connect (web_view, "notify::estimated-load-progress",
			G_CALLBACK (cpi_google_notify_estimated_load_progress_cb), progress_bar);

		webkit_web_view_load_uri (web_view, uri);

		success = gtk_dialog_run (prompter_google->priv->dialog) == GTK_RESPONSE_OK;

		if (load_finished_handler_id)
			g_signal_handler_disconnect (web_view, load_finished_handler_id);
		if (progress_handler_id)
			g_signal_handler_disconnect (web_view, progress_handler_id);
	}

	if (prompter_google->priv->cancellable)
		g_cancellable_cancel (prompter_google->priv->cancellable);

	prompter_google->priv->web_view = NULL;
	prompter_google->priv->dialog = NULL;
	gtk_widget_destroy (dialog);

	g_string_free (info_markup, TRUE);
	g_free (title);

	return success;
#else /* ENABLE_GOOGLE_AUTH */
	return FALSE;
#endif /* ENABLE_GOOGLE_AUTH */
}

static void
e_credentials_prompter_impl_google_free_prompt_data (ECredentialsPrompterImplGoogle *prompter_google)
{
	g_return_if_fail (E_IS_CREDENTIALS_PROMPTER_IMPL_GOOGLE (prompter_google));

	prompter_google->priv->prompt_id = NULL;

	g_clear_object (&prompter_google->priv->auth_source);
	g_clear_object (&prompter_google->priv->cred_source);

	g_free (prompter_google->priv->error_text);
	prompter_google->priv->error_text = NULL;

	e_named_parameters_free (prompter_google->priv->credentials);
	prompter_google->priv->credentials = NULL;
}

static gboolean
e_credentials_prompter_impl_google_manage_dialog_idle_cb (gpointer user_data)
{
	ECredentialsPrompterImplGoogle *prompter_google = user_data;

	if (g_source_is_destroyed (g_main_current_source ()))
		return FALSE;

	g_return_val_if_fail (E_IS_CREDENTIALS_PROMPTER_IMPL_GOOGLE (prompter_google), FALSE);

	g_mutex_lock (&prompter_google->priv->property_lock);
	if (g_source_get_id (g_main_current_source ()) == prompter_google->priv->show_dialog_idle_id) {
		gboolean success, refresh_failed_with_transport_error;

		prompter_google->priv->show_dialog_idle_id = 0;
		refresh_failed_with_transport_error = prompter_google->priv->refresh_failed_with_transport_error;

		g_mutex_unlock (&prompter_google->priv->property_lock);

		g_warn_if_fail (prompter_google->priv->dialog == NULL);

		if (e_named_parameters_exists (prompter_google->priv->credentials, E_SOURCE_CREDENTIAL_PASSWORD))
			success = TRUE;
		else if (refresh_failed_with_transport_error)
			success = FALSE;
		else
			success = e_credentials_prompter_impl_google_show_dialog (prompter_google);

		e_credentials_prompter_impl_prompt_finish (
			E_CREDENTIALS_PROMPTER_IMPL (prompter_google),
			prompter_google->priv->prompt_id,
			success ? prompter_google->priv->credentials : NULL);

		e_credentials_prompter_impl_google_free_prompt_data (prompter_google);
	} else {
		gpointer prompt_id = prompter_google->priv->prompt_id;

		g_warning ("%s: Prompt's %p source cancelled? current:%d expected:%d", G_STRFUNC, prompt_id, (gint) g_source_get_id (g_main_current_source ()), (gint) prompter_google->priv->show_dialog_idle_id);

		if (!prompter_google->priv->show_dialog_idle_id)
			e_credentials_prompter_impl_google_free_prompt_data (prompter_google);

		g_mutex_unlock (&prompter_google->priv->property_lock);

		if (prompt_id)
			e_credentials_prompter_impl_prompt_finish (E_CREDENTIALS_PROMPTER_IMPL (prompter_google), prompt_id, NULL);
	}

	return FALSE;
}

#ifdef ENABLE_GOOGLE_AUTH
typedef struct {
	GWeakRef *prompter_google; /* ECredentialsPrompterImplGoogle * */
	GCancellable *cancellable;
	ESource *cred_source;
	ESourceRegistry *registry;
	gchar *secret;
} CheckExistingThreadData;

static void
check_existing_thread_data_free (gpointer user_data)
{
	CheckExistingThreadData *td = user_data;

	if (td) {
		e_weak_ref_free (td->prompter_google);
		g_clear_object (&td->cancellable);
		g_clear_object (&td->cred_source);
		g_clear_object (&td->registry);
		g_free (td->secret);
		g_free (td);
	}
}

static gpointer
cpi_google_check_existing_token_thread (gpointer user_data)
{
	CheckExistingThreadData *td = user_data;
	ECredentialsPrompterImplGoogle *prompter_google;
	GCancellable *cancellable;
	gchar *refresh_token = NULL;

	g_return_val_if_fail (td != NULL, NULL);

	cancellable = td->cancellable;

	prompter_google = g_weak_ref_get (td->prompter_google);
	if (!prompter_google)
		goto exit;

	if (g_cancellable_is_cancelled (cancellable))
		goto exit;

	if (!e_source_credentials_google_util_decode_from_secret (td->secret, E_GOOGLE_SECRET_REFRESH_TOKEN, &refresh_token, NULL))
		goto exit;

	g_mutex_lock (&prompter_google->priv->property_lock);
	prompter_google->priv->refresh_failed_with_transport_error = FALSE;
	g_mutex_unlock (&prompter_google->priv->property_lock);

	if (refresh_token) {
		gchar *post_data, *response_json = NULL;
		guint soup_status;

		post_data = cpi_google_create_refresh_token_post_data (refresh_token);
		g_warn_if_fail (post_data != NULL);
		if (!post_data)
			goto exit;

		soup_status = cpi_google_post_data_sync (GOOGLE_TOKEN_URI, post_data,
			td->registry, td->cred_source, cancellable, &response_json);

		if (SOUP_STATUS_IS_SUCCESSFUL (soup_status) && response_json) {
			gchar *access_token = NULL, *expires_in = NULL;

			if (e_source_credentials_google_util_decode_from_secret (response_json,
				"access_token", &access_token,
				"expires_in", &expires_in,
				NULL) && access_token && expires_in) {
				cpi_google_update_prompter_credentials (td->prompter_google,
					refresh_token, access_token, expires_in, cancellable);
			}

			g_free (access_token);
			g_free (expires_in);
		} else if (SOUP_STATUS_IS_TRANSPORT_ERROR (soup_status)) {
			g_mutex_lock (&prompter_google->priv->property_lock);
			prompter_google->priv->refresh_failed_with_transport_error = TRUE;
			g_mutex_unlock (&prompter_google->priv->property_lock);
		}

		g_free (response_json);
		g_free (post_data);
	}

 exit:
	if (prompter_google && !g_cancellable_is_cancelled (cancellable)) {
		g_mutex_lock (&prompter_google->priv->property_lock);
		prompter_google->priv->show_dialog_idle_id = g_idle_add (
			e_credentials_prompter_impl_google_manage_dialog_idle_cb,
			prompter_google);
		g_mutex_unlock (&prompter_google->priv->property_lock);
	}

	g_clear_object (&prompter_google);
	g_free (refresh_token);

	check_existing_thread_data_free (td);

	return NULL;
}
#endif /* ENABLE_GOOGLE_AUTH */

static void
e_credentials_prompter_impl_google_process_prompt (ECredentialsPrompterImpl *prompter_impl,
						   gpointer prompt_id,
						   ESource *auth_source,
						   ESource *cred_source,
						   const gchar *error_text,
						   const ENamedParameters *credentials)
{
	ECredentialsPrompterImplGoogle *prompter_google;

	g_return_if_fail (E_IS_CREDENTIALS_PROMPTER_IMPL_GOOGLE (prompter_impl));

	prompter_google = E_CREDENTIALS_PROMPTER_IMPL_GOOGLE (prompter_impl);
	g_return_if_fail (prompter_google->priv->prompt_id == NULL);

	g_mutex_lock (&prompter_google->priv->property_lock);
	if (prompter_google->priv->show_dialog_idle_id != 0) {
		g_mutex_unlock (&prompter_google->priv->property_lock);
		g_warning ("%s: Already processing other prompt", G_STRFUNC);
		return;
	}
	g_mutex_unlock (&prompter_google->priv->property_lock);

	prompter_google->priv->prompt_id = prompt_id;
	prompter_google->priv->auth_source = g_object_ref (auth_source);
	prompter_google->priv->cred_source = g_object_ref (cred_source);
	prompter_google->priv->error_text = g_strdup (error_text);
	prompter_google->priv->credentials = e_named_parameters_new_clone (credentials);
	prompter_google->priv->cancellable = g_cancellable_new ();

#ifdef ENABLE_GOOGLE_AUTH
	e_named_parameters_set (prompter_google->priv->credentials, E_SOURCE_CREDENTIAL_PASSWORD, NULL);

	/* Read stored secret directly from the secret store, if not provided, because
	   e_source_credentials_provider_impl_google_lookup_sync() returns the secret
	   only if the stored access token is not expired.
	 */
	if (!e_named_parameters_exists (prompter_google->priv->credentials, E_SOURCE_CREDENTIAL_GOOGLE_SECRET)) {
		gchar *secret_uid = NULL;

		if (e_source_credentials_google_util_generate_secret_uid (cred_source, &secret_uid)) {
			gchar *secret = NULL;

			if (e_secret_store_lookup_sync (secret_uid, &secret, prompter_google->priv->cancellable, NULL)) {
				e_named_parameters_set (prompter_google->priv->credentials,
					E_SOURCE_CREDENTIAL_GOOGLE_SECRET, secret);
			}

			g_free (secret);
		}

		g_free (secret_uid);
	}

	if (e_named_parameters_exists (prompter_google->priv->credentials, E_SOURCE_CREDENTIAL_GOOGLE_SECRET)) {
		ECredentialsPrompter *prompter;
		CheckExistingThreadData *td;
		GThread *thread;

		prompter = e_credentials_prompter_impl_get_credentials_prompter (prompter_impl);

		td = g_new0 (CheckExistingThreadData, 1);
		td->prompter_google = e_weak_ref_new (prompter_google);
		td->cancellable = g_object_ref (prompter_google->priv->cancellable);
		td->secret = g_strdup (e_named_parameters_get (prompter_google->priv->credentials, E_SOURCE_CREDENTIAL_GOOGLE_SECRET));
		td->cred_source = g_object_ref (cred_source);
		td->registry = g_object_ref (e_credentials_prompter_get_registry (prompter));

		thread = g_thread_new (G_STRFUNC, cpi_google_check_existing_token_thread, td);
		g_thread_unref (thread);
	} else {
#endif /* ENABLE_GOOGLE_AUTH */
		g_mutex_lock (&prompter_google->priv->property_lock);
		prompter_google->priv->refresh_failed_with_transport_error = FALSE;
		prompter_google->priv->show_dialog_idle_id = g_idle_add (
			e_credentials_prompter_impl_google_manage_dialog_idle_cb,
			prompter_google);
		g_mutex_unlock (&prompter_google->priv->property_lock);
#ifdef ENABLE_GOOGLE_AUTH
	}
#endif /* ENABLE_GOOGLE_AUTH */
}

static void
e_credentials_prompter_impl_google_cancel_prompt (ECredentialsPrompterImpl *prompter_impl,
						  gpointer prompt_id)
{
	ECredentialsPrompterImplGoogle *prompter_google;

	g_return_if_fail (E_IS_CREDENTIALS_PROMPTER_IMPL_GOOGLE (prompter_impl));

	prompter_google = E_CREDENTIALS_PROMPTER_IMPL_GOOGLE (prompter_impl);
	g_return_if_fail (prompter_google->priv->prompt_id == prompt_id);

	if (prompter_google->priv->cancellable)
		g_cancellable_cancel (prompter_google->priv->cancellable);

	/* This also closes the dialog. */
	if (prompter_google->priv->dialog)
		gtk_dialog_response (prompter_google->priv->dialog, GTK_RESPONSE_CANCEL);
}

static void
e_credentials_prompter_impl_google_dispose (GObject *object)
{
	ECredentialsPrompterImplGoogle *prompter_google = E_CREDENTIALS_PROMPTER_IMPL_GOOGLE (object);

	g_mutex_lock (&prompter_google->priv->property_lock);
	if (prompter_google->priv->show_dialog_idle_id) {
		g_source_remove (prompter_google->priv->show_dialog_idle_id);
		prompter_google->priv->show_dialog_idle_id = 0;
	}
	g_mutex_unlock (&prompter_google->priv->property_lock);

	if (prompter_google->priv->cancellable) {
		g_cancellable_cancel (prompter_google->priv->cancellable);
		g_clear_object (&prompter_google->priv->cancellable);
	}

	g_warn_if_fail (prompter_google->priv->prompt_id == NULL);
	g_warn_if_fail (prompter_google->priv->dialog == NULL);

	e_credentials_prompter_impl_google_free_prompt_data (prompter_google);

	/* Chain up to parent's method. */
	G_OBJECT_CLASS (e_credentials_prompter_impl_google_parent_class)->dispose (object);
}

static void
e_credentials_prompter_impl_google_finalize (GObject *object)
{
	ECredentialsPrompterImplGoogle *prompter_google = E_CREDENTIALS_PROMPTER_IMPL_GOOGLE (object);

	g_mutex_clear (&prompter_google->priv->property_lock);

	/* Chain up to parent's method. */
	G_OBJECT_CLASS (e_credentials_prompter_impl_google_parent_class)->finalize (object);
}

static void
e_credentials_prompter_impl_google_class_init (ECredentialsPrompterImplGoogleClass *class)
{
	static const gchar *authentication_methods[] = {
		"Google",
		NULL
	};

	GObjectClass *object_class;
	ECredentialsPrompterImplClass *prompter_impl_class;

	g_type_class_add_private (class, sizeof (ECredentialsPrompterImplGooglePrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->dispose = e_credentials_prompter_impl_google_dispose;
	object_class->finalize = e_credentials_prompter_impl_google_finalize;

	prompter_impl_class = E_CREDENTIALS_PROMPTER_IMPL_CLASS (class);
	prompter_impl_class->authentication_methods = (const gchar * const *) authentication_methods;
	prompter_impl_class->process_prompt = e_credentials_prompter_impl_google_process_prompt;
	prompter_impl_class->cancel_prompt = e_credentials_prompter_impl_google_cancel_prompt;
}

static void
e_credentials_prompter_impl_google_init (ECredentialsPrompterImplGoogle *prompter_google)
{
	prompter_google->priv = G_TYPE_INSTANCE_GET_PRIVATE (prompter_google,
		E_TYPE_CREDENTIALS_PROMPTER_IMPL_GOOGLE, ECredentialsPrompterImplGooglePrivate);

	g_mutex_init (&prompter_google->priv->property_lock);
}

/**
 * e_credentials_prompter_impl_google_new:
 *
 * Creates a new instance of an #ECredentialsPrompterImplGoogle.
 *
 * Returns: (transfer full): a newly created #ECredentialsPrompterImplGoogle,
 *    which should be freed with g_object_unref() when no longer needed.
 *
 * Since: 3.20
 **/
ECredentialsPrompterImpl *
e_credentials_prompter_impl_google_new (void)
{
	return g_object_new (E_TYPE_CREDENTIALS_PROMPTER_IMPL_GOOGLE, NULL);
}
