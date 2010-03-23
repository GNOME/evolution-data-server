/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 * e-passwords.c
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 */

/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301
 * USA.
 */

/*
 * This looks a lot more complicated than it is, and than you'd think
 * it would need to be.  There is however, method to the madness.
 *
 * The code most cope with being called from any thread at any time,
 * recursively from the main thread, and then serialising every
 * request so that sane and correct values are always returned, and
 * duplicate requests are never made.
 *
 * To this end, every call is marshalled and queued and a dispatch
 * method invoked until that request is satisfied.  If mainloop
 * recursion occurs, then the sub-call will necessarily return out of
 * order, but will not be processed out of order.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <gtk/gtk.h>
#include <glib/gi18n-lib.h>

#include "e-passwords.h"
#include "libedataserver/e-flag.h"
#include "libedataserver/e-url.h"

#ifdef WITH_GNOME_KEYRING
#include <gnome-keyring.h>
#endif

#define d(x)

typedef struct _EPassMsg EPassMsg;

struct _EPassMsg {
	void (*dispatch) (EPassMsg *);
	EFlag *done;

	/* input */
	GtkWindow *parent;
	const gchar *component;
	const gchar *key;
	const gchar *title;
	const gchar *prompt;
	const gchar *oldpass;
	guint32 flags;

	/* output */
	gboolean *remember;
	gchar *password;
	GError *error;

	/* work variables */
	GtkWidget *entry;
	GtkWidget *check;
	guint ismain:1;
	guint noreply:1;	/* supress replies; when calling
				 * dispatch functions from others */
};

G_LOCK_DEFINE_STATIC (passwords);
static GThread *main_thread = NULL;
static GHashTable *password_cache = NULL;
static GtkDialog *password_dialog = NULL;
static GQueue message_queue = G_QUEUE_INIT;
static gint idle_id;
static gint ep_online_state = TRUE;

#ifdef WITH_GNOME_KEYRING
static gchar *default_keyring = NULL;
#endif

#define KEY_FILE_GROUP_PREFIX "Passwords-"
static GKeyFile *key_file = NULL;

static gboolean
check_key_file (const gchar *funcname)
{
	if (!key_file)
		g_message ("%s: Failed to create key file!", funcname);

	return key_file != NULL;
}

static gchar *
ep_key_file_get_filename (void)
{
#ifndef G_OS_WIN32
	const gchar *override;

	/* XXX It would be nice to someday move this data elsewhere, or else
	 * fully migrate to GNOME Keyring or whatever software supercedes it.
	 * Evolution is one of the few remaining GNOME-2 applications that
	 * still uses the deprecated ~/.gnome2_private directory. */

	override = g_getenv ("GNOME22_USER_DIR");
	if (override != NULL) {
		gchar resolved_path[PATH_MAX];

		/* Use realpath() to canonicalize the path, which
		 * strips off any trailing directory separators so
		 * we can safely tack on "_private". */
		return g_strdup_printf (
			"%s_private" G_DIR_SEPARATOR_S "Evolution",
			realpath (override, resolved_path));
	}
#endif
	return g_build_filename (
		g_get_home_dir (), ".gnome2_private", "Evolution", NULL);
}

static gchar *
ep_key_file_get_group (const gchar *component)
{
	return g_strconcat (KEY_FILE_GROUP_PREFIX, component, NULL);
}

static gchar *
ep_key_file_normalize_key (const gchar *key)
{
	/* XXX Previous code converted all slashes and equal signs in the
	 * key to underscores for use with "gnome-config" functions.  While
	 * it may not be necessary to convert slashes for use with GKeyFile,
	 * we continue to do the same for backward-compatibility. */

	gchar *normalized_key, *cp;

	normalized_key = g_strdup (key);
	for (cp = normalized_key; *cp != '\0'; cp++)
		if (*cp == '/' || *cp == '=')
			*cp = '_';

	return normalized_key;
}

static void
ep_key_file_load (void)
{
	gchar *filename;
	GError *error = NULL;

	if (!check_key_file (G_STRFUNC))
		return;

	filename = ep_key_file_get_filename ();

	if (!g_file_test (filename, G_FILE_TEST_EXISTS))
		goto exit;

	g_key_file_load_from_file (
		key_file, filename, G_KEY_FILE_KEEP_COMMENTS |
		G_KEY_FILE_KEEP_TRANSLATIONS, &error);

	if (error != NULL) {
		g_warning ("%s: %s", filename, error->message);
		g_error_free (error);
	}

exit:
	g_free (filename);
}

static void
ep_key_file_save (void)
{
	gchar *contents;
	gchar *filename;
	gchar *pathname;
	gsize length = 0;
	GError *error = NULL;

	if (!check_key_file (G_STRFUNC))
		return;

	filename = ep_key_file_get_filename ();
	contents = g_key_file_to_data (key_file, &length, &error);
	pathname = g_path_get_dirname (filename);

	if (!error) {
		g_mkdir_with_parents (pathname, 0700);
		g_file_set_contents (filename, contents, length, &error);
	}
	
	g_free (pathname);

	if (error != NULL) {
		g_warning ("%s: %s", filename, error->message);
		g_error_free (error);
	}

	g_free (contents);
	g_free (filename);
}

#ifdef WITH_GNOME_KEYRING

/* XXX Unfortunately, gnome-keyring doesn't use GErrors. */
#define EP_KEYRING_ERROR	(ep_keyring_error_domain ())

static GQuark
ep_keyring_error_domain (void)
{
	static GQuark quark = 0;

	if (G_UNLIKELY (quark == 0))
		quark = g_quark_from_static_string ("ep-keyring-error-quark");

	return quark;
}

static EUri *
ep_keyring_uri_new (const gchar *string,
                    GError **error)
{
	EUri *uri;

	uri = e_uri_new (string);
	g_return_val_if_fail (uri != NULL, NULL);

	/* LDAP URIs do not have usernames, so use the URI as the username. */
	if (uri->user == NULL && uri->protocol != NULL &&
			(strcmp (uri->protocol, "ldap") == 0|| strcmp (uri->protocol, "google") == 0))
		uri->user = g_strdelimit (g_strdup (string), "/=", '_');

	/* Make sure the URI has the required components. */
	if (uri->user == NULL && uri->host == NULL) {
		g_set_error (
			error, EP_KEYRING_ERROR,
			GNOME_KEYRING_RESULT_BAD_ARGUMENTS,
			_("Keyring key is unusable: no user or host name"));
		e_uri_free (uri);
		uri = NULL;
	}

	return uri;
}

static gboolean
ep_keyring_validate (const gchar *user,
                     const gchar *server,
                     const gchar *protocol,
                     GnomeKeyringAttributeList *attributes)
{
	const gchar *user_value = NULL;
	const gchar *server_value = NULL;
	const gchar *protocol_value = NULL;
	gint ii;

	g_return_val_if_fail (attributes != NULL, FALSE);

	/* Is there anything to validate? */
	if (user == NULL && server == NULL && protocol == NULL)
		return TRUE;

	/* Look for "user", "server", and "protocol" attributes. */
	for (ii = 0; ii < attributes->len; ii++) {
		GnomeKeyringAttribute *attr;

		attr = &g_array_index (attributes, GnomeKeyringAttribute, ii);

		/* Just assume the attribute values are strings. */
		if (strcmp (attr->name, "user") == 0)
			user_value = attr->value.string;
		else if (strcmp (attr->name, "server") == 0)
			server_value = attr->value.string;
		else if (strcmp (attr->name, "protocol") == 0)
			protocol_value = attr->value.string;
	}

	/* Is there a "user" attribute? */
	if (user != NULL && user_value == NULL)
		return FALSE;

	/* Does it match what we're looking for? */
	if (user != NULL && strcmp (user, user_value) != 0)
		return FALSE;

	/* Is there a "server" attribute? */
	if (server != NULL && server_value == NULL)
		return FALSE;

	/* Does it match what we're looking for? */
	if (server != NULL && strcmp (server, server_value) != 0)
		return FALSE;

	/* Is there a "protocol" attribute? */
	if (protocol != NULL && protocol_value == NULL)
		return FALSE;

	/* Does it match what we're looking for? */
	if (protocol != NULL && strcmp (protocol, protocol_value) != 0)
		return FALSE;

	return TRUE;
}

static gboolean
ep_keyring_delete_passwords (const gchar *user,
                             const gchar *server,
                             const gchar *protocol,
                             GList *passwords,
                             GError **error)
{
	while (passwords != NULL) {
		GnomeKeyringFound *found = passwords->data;
		GnomeKeyringResult result;

		/* Validate the item before deleting it. */
		if (!ep_keyring_validate (user, server, protocol, found->attributes)) {
			/* XXX We didn't always store protocols in the
			 *     keyring, so for backward-compatibility
			 *     try validating by user and server only. */
			if (!ep_keyring_validate (user, server, NULL, found->attributes)) {
				passwords = g_list_next (passwords);
				continue;
			}
		}

		result = gnome_keyring_item_delete_sync (NULL, found->item_id);
		if (result != GNOME_KEYRING_RESULT_OK) {
			g_set_error (
				error, EP_KEYRING_ERROR, result,
				"Unable to delete password in "
				"keyring (Keyring reports: %s)",
				gnome_keyring_result_to_message (result));
			return FALSE;
		}

		passwords = g_list_next (passwords);
	}

	return TRUE;
}

static gboolean
ep_keyring_insert_password (const gchar *user,
                            const gchar *server,
                            const gchar *protocol,
                            const gchar *display_name,
                            const gchar *password,
                            GError **error)
{
	GnomeKeyringAttributeList *attributes;
	GnomeKeyringResult result;
	guint32 item_id;

	g_return_val_if_fail (user != NULL, FALSE);
	g_return_val_if_fail (server != NULL, FALSE);
	g_return_val_if_fail (protocol != NULL, FALSE);
	g_return_val_if_fail (display_name != NULL, FALSE);
	g_return_val_if_fail (password != NULL, FALSE);

	attributes = gnome_keyring_attribute_list_new ();
	gnome_keyring_attribute_list_append_string (
		attributes, "application", "Evolution");
	gnome_keyring_attribute_list_append_string (
		attributes, "user", user);
	gnome_keyring_attribute_list_append_string (
		attributes, "server", server);
	gnome_keyring_attribute_list_append_string (
		attributes, "protocol", protocol);

	/* XXX We don't use item_id but gnome-keyring doesn't allow
	 *     for a NULL pointer.  In fact it doesn't even check! */
	result = gnome_keyring_item_create_sync (
		NULL, GNOME_KEYRING_ITEM_NETWORK_PASSWORD,
		display_name, attributes, password, TRUE, &item_id);
	if (result != GNOME_KEYRING_RESULT_OK) {
		g_set_error (
			error, EP_KEYRING_ERROR, result,
			"Unable to create password in "
			"keyring (Keyring reports: %s)",
			gnome_keyring_result_to_message (result));
	}

	gnome_keyring_attribute_list_free (attributes);

	return (result == GNOME_KEYRING_RESULT_OK);
}

static GList *
ep_keyring_lookup_passwords (const gchar *user,
                             const gchar *server,
                             const gchar *protocol,
                             GError **error)
{
	GnomeKeyringAttributeList *attributes;
	GnomeKeyringResult result;
	GList *passwords = NULL;

	attributes = gnome_keyring_attribute_list_new ();
	gnome_keyring_attribute_list_append_string (
		attributes, "application", "Evolution");
	if (user != NULL)
		gnome_keyring_attribute_list_append_string (
			attributes, "user", user);
	if (server != NULL)
		gnome_keyring_attribute_list_append_string (
			attributes, "server", server);
	if (protocol != NULL)
		gnome_keyring_attribute_list_append_string (
			attributes, "protocol", protocol);

	result = gnome_keyring_find_items_sync (
		GNOME_KEYRING_ITEM_NETWORK_PASSWORD, attributes, &passwords);
	if (result != GNOME_KEYRING_RESULT_OK) {
		g_set_error (
			error, EP_KEYRING_ERROR, result,
			"Unable to find password(s) in "
			"keyring (Keyring reports: %s)",
			gnome_keyring_result_to_message (result));
	}

	gnome_keyring_attribute_list_free (attributes);

	return passwords;
}
#endif

static gchar *
ep_password_encode (const gchar *password)
{
	/* XXX The previous Base64 encoding function did not encode the
	 * password's trailing nul byte.  This makes decoding the Base64
	 * string into a nul-terminated password more difficult, but we
	 * continue to do it this way for backward-compatibility. */

	gsize length = strlen (password);
	return g_base64_encode ((const guchar *) password, length);
}

static gchar *
ep_password_decode (const gchar *encoded_password)
{
	/* XXX The previous Base64 encoding function did not encode the
	 * password's trailing nul byte, so we have to append a nul byte
	 * to the decoded data to make it a nul-terminated string. */

	gchar *password;
	gsize length = 0;

	password = (gchar *) g_base64_decode (encoded_password, &length);
	password = g_realloc (password, length + 1);
	password[length] = '\0';

	return password;
}

static gboolean
ep_idle_dispatch (gpointer data)
{
	EPassMsg *msg;

	/* As soon as a password window is up we stop; it will
	   re-invoke us when it has been closed down */
	G_LOCK (passwords);
	while (password_dialog == NULL && (msg = g_queue_pop_head (&message_queue)) != NULL) {
		G_UNLOCK (passwords);

		msg->dispatch (msg);

		G_LOCK (passwords);
	}

	idle_id = 0;
	G_UNLOCK (passwords);

	return FALSE;
}

static EPassMsg *
ep_msg_new (void (*dispatch) (EPassMsg *))
{
	EPassMsg *msg;

	e_passwords_init ();

	msg = g_malloc0 (sizeof (*msg));
	msg->dispatch = dispatch;
	msg->done = e_flag_new ();
	msg->ismain = (g_thread_self () == main_thread);

	return msg;
}

static void
ep_msg_free (EPassMsg *msg)
{
	/* XXX We really should be passing this back to the caller, but
	 *     doing so will require breaking the password API. */
	if (msg->error != NULL) {
		g_warning ("%s", msg->error->message);
		g_error_free (msg->error);
	}

	e_flag_free (msg->done);
	g_free (msg->password);
	g_free (msg);
}

static void
ep_msg_send (EPassMsg *msg)
{
	gint needidle = 0;

	G_LOCK (passwords);
	g_queue_push_tail (&message_queue, msg);
	if (!idle_id) {
		if (!msg->ismain)
			idle_id = g_idle_add (ep_idle_dispatch, NULL);
		else
			needidle = 1;
	}
	G_UNLOCK (passwords);

	if (msg->ismain) {
		if (needidle)
			ep_idle_dispatch (NULL);
		while (!e_flag_is_set (msg->done))
			g_main_context_iteration (NULL, TRUE);
	} else
		e_flag_wait (msg->done);
}

/* the functions that actually do the work */
#ifdef WITH_GNOME_KEYRING
static void
ep_clear_passwords_keyring (EPassMsg *msg)
{
	GList *passwords;
	GError *error = NULL;

	/* Find all Evolution passwords and delete them. */
	passwords = ep_keyring_lookup_passwords (NULL, NULL, NULL, &error);
	if (passwords != NULL) {
		ep_keyring_delete_passwords (NULL, NULL, NULL, passwords, &error);
		gnome_keyring_found_list_free (passwords);
	}

	/* Not finding the requested key is acceptable, but we still
	 * want to leave an informational message on the terminal. */
	if (g_error_matches (error, EP_KEYRING_ERROR, GNOME_KEYRING_RESULT_NO_MATCH)) {
		g_message ("%s", error->message);
		g_error_free (error);

	} else if (error != NULL)
		g_propagate_error (&msg->error, error);
}
#endif

static void
ep_clear_passwords_keyfile (EPassMsg *msg)
{
	gchar *group;
	GError *error = NULL;

	if (!check_key_file (G_STRFUNC))
		return;

	group = ep_key_file_get_group (msg->component);

	if (g_key_file_remove_group (key_file, group, &error))
		ep_key_file_save ();

	/* Not finding the requested group is acceptable, but we still
	 * want to leave an informational message on the terminal. */
	else if (g_error_matches (error, G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_GROUP_NOT_FOUND)) {
		g_message ("%s", error->message);
		g_error_free (error);

	} else if (error != NULL)
		g_propagate_error (&msg->error, error);

	g_free (group);
}

static void
ep_clear_passwords (EPassMsg *msg)
{
#ifdef WITH_GNOME_KEYRING
	if (gnome_keyring_is_available ())
		ep_clear_passwords_keyring (msg);
	else
		ep_clear_passwords_keyfile (msg);
#else
	ep_clear_passwords_keyfile (msg);
#endif

	if (!msg->noreply)
		e_flag_set (msg->done);
}

#ifdef WITH_GNOME_KEYRING
static void
ep_forget_passwords_keyring (EPassMsg *msg)
{
	GList *passwords;
	GError *error = NULL;

	/* Find all Evolution passwords and delete them. */
	passwords = ep_keyring_lookup_passwords (NULL, NULL, NULL, &error);
	if (passwords != NULL) {
		ep_keyring_delete_passwords (NULL, NULL, NULL, passwords, &error);
		gnome_keyring_found_list_free (passwords);

	}

	if (error != NULL)
		g_propagate_error (&msg->error, error);
}
#endif

static void
ep_forget_passwords_keyfile (EPassMsg *msg)
{
	gchar **groups;
	gsize length = 0, ii;

	if (!check_key_file (G_STRFUNC))
		return;

	groups = g_key_file_get_groups (key_file, &length);

	if (!groups)
		return;

	for (ii = 0; ii < length; ii++) {
		GError *error = NULL;

		if (!g_str_has_prefix (groups[ii], KEY_FILE_GROUP_PREFIX))
			continue;

		g_key_file_remove_group (key_file, groups[ii], &error);

		/* Not finding the requested group is acceptable, but we still
		 * want to leave an informational message on the terminal. */
		if (g_error_matches (error, G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_GROUP_NOT_FOUND)) {
			g_message ("%s", error->message);
			g_error_free (error);

		/* Issue a warning if anything else goes wrong. */
		} else if (error != NULL) {
			g_warning ("%s", error->message);
			g_error_free (error);
		}
	}
	ep_key_file_save ();
	g_strfreev (groups);
}

static void
ep_forget_passwords (EPassMsg *msg)
{
	g_hash_table_remove_all (password_cache);

#ifdef WITH_GNOME_KEYRING
	if (gnome_keyring_is_available ())
		ep_forget_passwords_keyring (msg);
	else
		ep_forget_passwords_keyfile (msg);
#else
	ep_forget_passwords_keyfile (msg);
#endif

	if (!msg->noreply)
		e_flag_set (msg->done);
}

#ifdef WITH_GNOME_KEYRING
static void
ep_remember_password_keyring (EPassMsg *msg)
{
	gchar *password;
	EUri *uri;
	GError *error = NULL;

	password = g_hash_table_lookup (password_cache, msg->key);
	if (password == NULL) {
		g_warning ("Password for key \"%s\" not found", msg->key);
		return;
	}

	uri = ep_keyring_uri_new (msg->key, &msg->error);
	if (uri == NULL)
		return;

	/* Only remove the password from the session hash
	 * if the keyring insertion was successful. */
	if (ep_keyring_insert_password (uri->user, uri->host, uri->protocol, msg->key, password, &error))
		g_hash_table_remove (password_cache, msg->key);

	if (error != NULL)
		g_propagate_error (&msg->error, error);

	e_uri_free (uri);
}
#endif

static void
ep_remember_password_keyfile (EPassMsg *msg)
{
	gchar *group, *key, *password;

	password = g_hash_table_lookup (password_cache, msg->key);
	if (password == NULL) {
		g_warning ("Password for key \"%s\" not found", msg->key);
		return;
	}

	group = ep_key_file_get_group (msg->component);
	key = ep_key_file_normalize_key (msg->key);
	password = ep_password_encode (password);

	g_hash_table_remove (password_cache, msg->key);
	if (check_key_file (G_STRFUNC)) {
		g_key_file_set_string (key_file, group, key, password);
		ep_key_file_save ();
	}

	g_free (group);
	g_free (key);
	g_free (password);
}

static void
ep_remember_password (EPassMsg *msg)
{
#ifdef WITH_GNOME_KEYRING
	if (gnome_keyring_is_available ())
		ep_remember_password_keyring (msg);
	else
		ep_remember_password_keyfile (msg);
#else
	ep_remember_password_keyfile (msg);
#endif

	if (!msg->noreply)
		e_flag_set (msg->done);
}

#ifdef WITH_GNOME_KEYRING
static void
ep_forget_password_keyring (EPassMsg *msg)
{
	GList *passwords;
	EUri *uri;
	GError *error = NULL;

	uri = ep_keyring_uri_new (msg->key, &msg->error);
	if (uri == NULL)
		return;

	/* Find all Evolution passwords matching the URI and delete them.
	 *
	 * XXX We didn't always store protocols in the keyring, so for
	 *     backward-compatibility we need to lookup passwords by user
	 *     and host only (no protocol).  But we do send the protocol
	 *     to ep_keyring_delete_passwords(), which also knows about
	 *     the backward-compatibility issue and will filter the list
	 *     appropriately. */
	passwords = ep_keyring_lookup_passwords (uri->user, uri->host, NULL, &error);
	if (passwords != NULL) {
		ep_keyring_delete_passwords (uri->user, uri->host, uri->protocol, passwords, &error);
		gnome_keyring_found_list_free (passwords);
	}

	if (error != NULL)
		g_propagate_error (&msg->error, error);

	e_uri_free (uri);
}
#endif

static void
ep_forget_password_keyfile (EPassMsg *msg)
{
	gchar *group, *key;
	GError *error = NULL;

	if (!check_key_file (G_STRFUNC))
		return;

	group = ep_key_file_get_group (msg->component);
	key = ep_key_file_normalize_key (msg->key);

	if (g_key_file_remove_key (key_file, group, key, &error))
		ep_key_file_save ();

	/* Not finding the requested key is acceptable, but we still
	 * want to leave an informational message on the terminal. */
	else if (g_error_matches (error, G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_KEY_NOT_FOUND)) {
		g_message ("%s", error->message);
		g_error_free (error);

	/* Not finding the requested group is also acceptable. */
	} else if (g_error_matches (error, G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_GROUP_NOT_FOUND)) {
		g_message ("%s", error->message);
		g_error_free (error);

	} else if (error != NULL)
		g_propagate_error (&msg->error, error);

	g_free (group);
	g_free (key);
}

static void
ep_forget_password (EPassMsg *msg)
{
	g_hash_table_remove (password_cache, msg->key);

#ifdef WITH_GNOME_KEYRING
	if (gnome_keyring_is_available ())
		ep_forget_password_keyring (msg);
	else
		ep_forget_password_keyfile (msg);
#else
	ep_forget_password_keyfile (msg);
#endif

	if (!msg->noreply)
		e_flag_set (msg->done);
}

#ifdef WITH_GNOME_KEYRING
static void
ep_get_password_keyring (EPassMsg *msg)
{
	GList *passwords;
	EUri *uri;
	GError *error = NULL;

	uri = ep_keyring_uri_new (msg->key, &msg->error);
	if (uri == NULL)
		return;

	/* Find the first Evolution password that matches the URI. */
	passwords = ep_keyring_lookup_passwords (uri->user, uri->host, uri->protocol, &error);
	if (passwords != NULL) {
		GList *iter = passwords;

		while (iter != NULL) {
			GnomeKeyringFound *found = iter->data;

			if (default_keyring && strcmp(default_keyring, found->keyring) != 0) {
				g_message ("Received a password from keyring '%s'. But looking for the password from '%s' keyring\n", found->keyring, default_keyring);
				iter = g_list_next (iter);
				continue;
			}

			if (ep_keyring_validate (uri->user, uri->host, uri->protocol, found->attributes)) {
				msg->password = g_strdup (found->secret);
				break;
			}

			iter = g_list_next (iter);
		}

		gnome_keyring_found_list_free (passwords);
	}

	if (msg->password != NULL)
		goto done;

	/* Clear the previous error, if there was one.  If the error was
	 * something other than NO_MATCH then it's likely to occur again. */
	if (error != NULL)
		g_clear_error (&error);

	/* XXX We didn't always store protocols in the keyring, so for
	 *     backward-compatibility we also need to lookup passwords
	 *     by user and host only (no protocol). */
	passwords = ep_keyring_lookup_passwords (uri->user, uri->host, NULL, &error);
	if (passwords != NULL) {
		GList *iter = passwords;

		while (iter != NULL) {
			GnomeKeyringFound *found = iter->data;

			if (default_keyring && strcmp(default_keyring, found->keyring) != 0) {
				g_message ("Received a password from keyring '%s'. But looking for the password from '%s' keyring\n", found->keyring, default_keyring);
				iter = g_list_next (iter);
				continue;
			}
			if (ep_keyring_validate (uri->user, uri->host, NULL, found->attributes)) {
				msg->password = g_strdup (found->secret);
				break;
			}

			iter = g_list_next (iter);
		}

		gnome_keyring_found_list_free (passwords);
	}

done:
	/* Not finding the requested key is acceptable, but we still
	 * want to leave an informational message on the terminal. */
	if (g_error_matches (error, EP_KEYRING_ERROR, GNOME_KEYRING_RESULT_NO_MATCH)) {
		g_message ("%s", error->message);
		g_error_free (error);

	} else if (error != NULL)
		g_propagate_error (&msg->error, error);

	e_uri_free (uri);
}
#endif

static void
ep_get_password_keyfile (EPassMsg *msg)
{
	gchar *group, *key, *password;
	GError *error = NULL;

	if (!check_key_file (G_STRFUNC))
		return;

	group = ep_key_file_get_group (msg->component);
	key = ep_key_file_normalize_key (msg->key);

	password = g_key_file_get_string (key_file, group, key, &error);
	if (password != NULL) {
		msg->password = ep_password_decode (password);
		g_free (password);

	/* Not finding the requested key is acceptable, but we still
	 * want to leave an informational message on the terminal. */
	} else if (g_error_matches (error, G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_KEY_NOT_FOUND)) {
		g_message ("%s", error->message);
		g_error_free (error);

	/* Not finding the requested group is also acceptable. */
	} else if (g_error_matches (error, G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_GROUP_NOT_FOUND)) {
		g_message ("%s", error->message);
		g_error_free (error);

	} else if (error != NULL)
		g_propagate_error (&msg->error, error);

	g_free (group);
	g_free (key);
}

#ifdef WITH_GNOME_KEYRING
static void
ep_keyring_migrate_from_keyfile (EPassMsg *msg)
{
	/* Fetch the password from the keyfile. */
	ep_get_password_keyfile (msg);
	if (msg->password == NULL)
		return;

	/* Add it to the in-memory cache. */
	g_hash_table_insert (
		password_cache, g_strdup (msg->key),
		g_strdup (msg->password));

	/* Remember it in the keyring. */
	ep_remember_password_keyring (msg);

	/* Remove it from the in-memory cache. */
	g_hash_table_remove (password_cache, msg->key);

	/* Remove it from the keyfile only if the keyring
	 * insertion was successful. */
	if (msg->error == NULL)
		ep_forget_password_keyfile (msg);
}
#endif

static void
ep_get_password (EPassMsg *msg)
{
	gchar *password;

	/* Check the in-memory cache first. */
	password = g_hash_table_lookup (password_cache, msg->key);
	if (password != NULL) {
		msg->password = g_strdup (password);

#ifdef WITH_GNOME_KEYRING
	} else if (gnome_keyring_is_available ()) {
		ep_get_password_keyring (msg);

		/* Try looking for the password in the keyfile.
		 * If we find it, migrate it to the keyring. */
		if (msg->password == NULL && msg->error == NULL)
			ep_keyring_migrate_from_keyfile (msg);
#endif
	} else
		ep_get_password_keyfile (msg);

	if (!msg->noreply)
		e_flag_set (msg->done);
}

static void
ep_add_password (EPassMsg *msg)
{
	g_hash_table_insert (
		password_cache, g_strdup (msg->key),
		g_strdup (msg->oldpass));

	if (!msg->noreply)
		e_flag_set (msg->done);
}

static void ep_ask_password (EPassMsg *msg);

static void
pass_response (GtkDialog *dialog, gint response, gpointer data)
{
	EPassMsg *msg = data;
	gint type = msg->flags & E_PASSWORDS_REMEMBER_MASK;
	GList *iter, *trash = NULL;

	if (response == GTK_RESPONSE_OK) {
		msg->password = g_strdup (gtk_entry_get_text ((GtkEntry *)msg->entry));

		if (type != E_PASSWORDS_REMEMBER_NEVER) {
			gint noreply = msg->noreply;

			*msg->remember = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (msg->check));

			msg->noreply = 1;

			if (*msg->remember || type == E_PASSWORDS_REMEMBER_FOREVER) {
				msg->oldpass = msg->password;
				ep_add_password (msg);
			}
			if (*msg->remember && type == E_PASSWORDS_REMEMBER_FOREVER)
				ep_remember_password (msg);

			msg->noreply = noreply;
		}
	}

	gtk_widget_destroy ((GtkWidget *)dialog);
	password_dialog = NULL;

	/* ok, here things get interesting, we suck up any pending
	 * operations on this specific password, and return the same
	 * result or ignore other operations */

	G_LOCK (passwords);
	for (iter = g_queue_peek_head_link (&message_queue); iter != NULL; iter = iter->next) {
		EPassMsg *pending = iter->data;

		if ((pending->dispatch == ep_forget_password
		     || pending->dispatch == ep_get_password
		     || pending->dispatch == ep_ask_password)
		    && (strcmp (pending->component, msg->component) == 0
			&& strcmp (pending->key, msg->key) == 0)) {

			/* Satisfy the pending operation. */
			pending->password = g_strdup (msg->password);
			e_flag_set (pending->done);

			/* Mark the queue node for deletion. */
			trash = g_list_prepend (trash, iter);
		}
	}

	/* Expunge the message queue. */
	for (iter = trash; iter != NULL; iter = iter->next)
		g_queue_delete_link (&message_queue, iter->data);
	g_list_free (trash);

	G_UNLOCK (passwords);

	if (!msg->noreply)
		e_flag_set (msg->done);

	ep_idle_dispatch (NULL);
}

static gboolean
update_capslock_state (gpointer widget, gpointer event, GtkWidget *label)
{
	GdkModifierType mask = 0;
	gchar *markup = NULL;

	gdk_window_get_pointer (NULL, NULL, NULL, &mask);

	/* The space acts as a vertical placeholder. */
	markup = g_markup_printf_escaped (
		"<small>%s</small>", (mask & GDK_LOCK_MASK) ?
		 _("You have the Caps Lock key on.") : " ");
	gtk_label_set_markup (GTK_LABEL (label), markup);
	g_free (markup);

	return FALSE;
}

static void
ep_ask_password (EPassMsg *msg)
{
	GtkWidget *widget;
	GtkWidget *container;
	GtkWidget *action_area;
	GtkWidget *content_area;
	gint type = msg->flags & E_PASSWORDS_REMEMBER_MASK;
	guint noreply = msg->noreply;
	gboolean visible;
	AtkObject *a11y;

	msg->noreply = 1;

	widget = gtk_dialog_new_with_buttons (
		msg->title, msg->parent, 0,
		GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
		GTK_STOCK_OK, GTK_RESPONSE_OK,
		NULL);
	gtk_dialog_set_has_separator (GTK_DIALOG (widget), FALSE);
	gtk_dialog_set_default_response (
		GTK_DIALOG (widget), GTK_RESPONSE_OK);
	gtk_window_set_resizable (GTK_WINDOW (widget), FALSE);
	gtk_window_set_transient_for (GTK_WINDOW (widget), msg->parent);
	gtk_window_set_position (GTK_WINDOW (widget), GTK_WIN_POS_CENTER_ON_PARENT);
	gtk_container_set_border_width (GTK_CONTAINER (widget), 12);
	password_dialog = GTK_DIALOG (widget);

	action_area = gtk_dialog_get_action_area (password_dialog);
	content_area = gtk_dialog_get_content_area (password_dialog);

	/* Override GtkDialog defaults */
	gtk_box_set_spacing (GTK_BOX (action_area), 12);
	gtk_container_set_border_width (GTK_CONTAINER (action_area), 0);
	gtk_box_set_spacing (GTK_BOX (content_area), 12);
	gtk_container_set_border_width (GTK_CONTAINER (content_area), 0);

	/* Table */
	container = gtk_table_new (2, 3, FALSE);
	gtk_table_set_col_spacings (GTK_TABLE (container), 12);
	gtk_table_set_row_spacings (GTK_TABLE (container), 6);
	gtk_table_set_row_spacing (GTK_TABLE (container), 0, 12);
	gtk_table_set_row_spacing (GTK_TABLE (container), 1, 0);
	gtk_widget_show (container);

	gtk_box_pack_start (
		GTK_BOX (content_area), container, FALSE, TRUE, 0);

	/* Password Image */
	widget = gtk_image_new_from_icon_name (
		"dialog-password", GTK_ICON_SIZE_DIALOG);
	gtk_misc_set_alignment (GTK_MISC (widget), 0.0, 0.0);
	gtk_widget_show (widget);

	gtk_table_attach (
		GTK_TABLE (container), widget,
		0, 1, 0, 3, GTK_FILL, GTK_EXPAND | GTK_FILL, 0, 0);

	/* Password Label */
	widget = gtk_label_new (NULL);
	gtk_label_set_line_wrap (GTK_LABEL (widget), TRUE);
	gtk_label_set_markup (GTK_LABEL (widget), msg->prompt);
	gtk_misc_set_alignment (GTK_MISC (widget), 0.0, 0.5);
	gtk_widget_show (widget);

	gtk_table_attach (
		GTK_TABLE (container), widget,
		1, 2, 0, 1, GTK_EXPAND | GTK_FILL, 0, 0, 0);

	/* Password Entry */
	widget = gtk_entry_new ();
	a11y = gtk_widget_get_accessible (widget);
	visible = !(msg->flags & E_PASSWORDS_SECRET);
	atk_object_set_description (a11y, msg->prompt);
	gtk_entry_set_visibility (GTK_ENTRY (widget), visible);
	gtk_entry_set_activates_default (GTK_ENTRY (widget), TRUE);
	gtk_widget_grab_focus (widget);
	gtk_widget_show (widget);
	msg->entry = widget;

	if ((msg->flags & E_PASSWORDS_REPROMPT)) {
		ep_get_password (msg);
		if (msg->password != NULL) {
			gtk_entry_set_text (GTK_ENTRY (widget), msg->password);
			g_free (msg->password);
			msg->password = NULL;
		}
	}

	gtk_table_attach (
		GTK_TABLE (container), widget,
		1, 2, 1, 2, GTK_EXPAND | GTK_FILL, 0, 0, 0);

	/* Caps Lock Label */
	widget = gtk_label_new (NULL);
	update_capslock_state (NULL, NULL, widget);
	gtk_widget_show (widget);

	gtk_table_attach (
		GTK_TABLE (container), widget,
		1, 2, 2, 3, GTK_EXPAND | GTK_FILL, 0, 0, 0);

	g_signal_connect (
		password_dialog, "key-release-event",
		G_CALLBACK (update_capslock_state), widget);
	g_signal_connect (
		password_dialog, "focus-in-event",
		G_CALLBACK (update_capslock_state), widget);

	/* static password, shouldn't be remembered between sessions,
	   but will be remembered within the session beyond our control */
	if (type != E_PASSWORDS_REMEMBER_NEVER) {
		if (msg->flags & E_PASSWORDS_PASSPHRASE) {
			widget = gtk_check_button_new_with_mnemonic (
				(type == E_PASSWORDS_REMEMBER_FOREVER)
				? _("_Remember this passphrase")
				: _("_Remember this passphrase for"
				    " the remainder of this session"));
		} else {
			widget = gtk_check_button_new_with_mnemonic (
				(type == E_PASSWORDS_REMEMBER_FOREVER)
				? _("_Remember this password")
				: _("_Remember this password for"
				    " the remainder of this session"));
		}

		gtk_toggle_button_set_active (
			GTK_TOGGLE_BUTTON (widget), *msg->remember);
		if (msg->flags & E_PASSWORDS_DISABLE_REMEMBER)
			gtk_widget_set_sensitive (widget, FALSE);
		gtk_widget_show (widget);
		msg->check = widget;

		gtk_table_attach (
			GTK_TABLE (container), widget,
			1, 2, 3, 4, GTK_EXPAND | GTK_FILL, GTK_FILL, 0, 0);
	}

	msg->noreply = noreply;

	g_signal_connect (password_dialog, "response", G_CALLBACK (pass_response), msg);

	if (msg->parent)
		gtk_dialog_run (GTK_DIALOG (password_dialog));
	else
		gtk_widget_show ((GtkWidget *)password_dialog);
}

/**
 * e_passwords_init:
 *
 * Initializes the e_passwords routines. Must be called before any other
 * e_passwords_* function.
 **/
void
e_passwords_init (void)
{
	G_LOCK (passwords);

	if (password_cache == NULL) {
		password_cache = g_hash_table_new_full (
			g_str_hash, g_str_equal,
			(GDestroyNotify) g_free,
			(GDestroyNotify) g_free);
		main_thread = g_thread_self ();

		/* Load the keyfile even if we're using the keyring.
		 * We might be able to extract passwords from it. */
		key_file = g_key_file_new ();
		ep_key_file_load ();
	}

#ifdef WITH_GNOME_KEYRING
	if (gnome_keyring_is_available ())
		gnome_keyring_get_default_keyring_sync (&default_keyring);
#endif
	G_UNLOCK (passwords);
}

/**
 * e_passwords_cancel:
 *
 * Cancel any outstanding password operations and close any dialogues
 * currently being shown.
 **/
void
e_passwords_cancel (void)
{
	EPassMsg *msg;

	G_LOCK (passwords);
	while ((msg = g_queue_pop_head (&message_queue)) != NULL)
		e_flag_set (msg->done);
	G_UNLOCK (passwords);

	if (password_dialog)
		gtk_dialog_response (password_dialog,GTK_RESPONSE_CANCEL);
}

/**
 * e_passwords_shutdown:
 *
 * Cleanup routine to call before exiting.
 **/
void
e_passwords_shutdown (void)
{
	EPassMsg *msg;

	G_LOCK (passwords);

	while ((msg = g_queue_pop_head (&message_queue)) != NULL)
		e_flag_set (msg->done);

	if (password_cache != NULL) {
		g_hash_table_destroy (password_cache);
		password_cache = NULL;
	}

#ifdef WITH_GNOME_KEYRING
	g_free (default_keyring);
#endif

	G_UNLOCK (passwords);

	if (password_dialog != NULL)
		gtk_dialog_response (password_dialog, GTK_RESPONSE_CANCEL);
}

/**
 * e_passwords_set_online:
 * @state:
 *
 * Set the offline-state of the application.  This is a work-around
 * for having the backends fully offline aware, and returns a
 * cancellation response instead of prompting for passwords.
 *
 * FIXME: This is not a permanent api, review post 2.0.
 **/
void
e_passwords_set_online (gint state)
{
	ep_online_state = state;
	/* TODO: we could check that a request is open and close it, or maybe who cares */
}

/**
 * e_passwords_forget_passwords:
 *
 * Forgets all cached passwords, in memory and on disk.
 **/
void
e_passwords_forget_passwords (void)
{
	EPassMsg *msg = ep_msg_new (ep_forget_passwords);

	ep_msg_send (msg);
	ep_msg_free (msg);
}

/**
 * e_passwords_clear_passwords:
 *
 * Forgets all disk cached passwords for the component.
 **/
void
e_passwords_clear_passwords (const gchar *component_name)
{
	EPassMsg *msg = ep_msg_new (ep_clear_passwords);

	msg->component = component_name;
	ep_msg_send (msg);
	ep_msg_free (msg);
}

/**
 * e_passwords_remember_password:
 * @key: the key
 *
 * Saves the password associated with @key to disk.
 **/
void
e_passwords_remember_password (const gchar *component_name, const gchar *key)
{
	EPassMsg *msg;

	g_return_if_fail (component_name != NULL);
	g_return_if_fail (key != NULL);

	msg = ep_msg_new (ep_remember_password);
	msg->component = component_name;
	msg->key = key;

	ep_msg_send (msg);
	ep_msg_free (msg);
}

/**
 * e_passwords_forget_password:
 * @key: the key
 *
 * Forgets the password associated with @key, in memory and on disk.
 **/
void
e_passwords_forget_password (const gchar *component_name, const gchar *key)
{
	EPassMsg *msg;

	g_return_if_fail (component_name != NULL);
	g_return_if_fail (key != NULL);

	msg = ep_msg_new (ep_forget_password);
	msg->component = component_name;
	msg->key = key;

	ep_msg_send (msg);
	ep_msg_free (msg);
}

/**
 * e_passwords_get_password:
 * @key: the key
 *
 * Returns: the password associated with @key, or %NULL.  Caller
 * must free the returned password.
 **/
gchar *
e_passwords_get_password (const gchar *component_name, const gchar *key)
{
	EPassMsg *msg;
	gchar *passwd;

	g_return_val_if_fail (component_name != NULL, NULL);
	g_return_val_if_fail (key != NULL, NULL);

	msg = ep_msg_new (ep_get_password);
	msg->component = component_name;
	msg->key = key;

	ep_msg_send (msg);

	passwd = msg->password;
	msg->password = NULL;
	ep_msg_free (msg);

	return passwd;
}

/**
 * e_passwords_add_password:
 * @key: a key
 * @passwd: the password for @key
 *
 * This stores the @key/@passwd pair in the current session's password
 * hash.
 **/
void
e_passwords_add_password (const gchar *key, const gchar *passwd)
{
	EPassMsg *msg;

	g_return_if_fail (key != NULL);
	g_return_if_fail (passwd != NULL);

	msg = ep_msg_new (ep_add_password);
	msg->key = key;
	msg->oldpass = passwd;

	ep_msg_send (msg);
	ep_msg_free (msg);
}

/**
 * e_passwords_ask_password:
 * @title: title for the password dialog
 * @component_name: the name of the component for which we're storing
 * the password (e.g. Mail, Addressbook, etc.)
 * @key: key to store the password under
 * @prompt: prompt string
 * @type: whether or not to offer to remember the password,
 * and for how long.
 * @remember: on input, the default state of the remember checkbox.
 * on output, the state of the checkbox when the dialog was closed.
 * @parent: parent window of the dialog, or %NULL
 *
 * Asks the user for a password.
 *
 * Returns: the password, which the caller must free, or %NULL if
 * the user cancelled the operation. *@remember will be set if the
 * return value is non-%NULL and @remember_type is not
 * E_PASSWORDS_DO_NOT_REMEMBER.
 **/
gchar *
e_passwords_ask_password (const gchar *title, const gchar *component_name,
			  const gchar *key,
			  const gchar *prompt,
			  EPasswordsRememberType type,
			  gboolean *remember,
			  GtkWindow *parent)
{
	gchar *passwd;
	EPassMsg *msg;

	g_return_val_if_fail (component_name != NULL, NULL);
	g_return_val_if_fail (key != NULL, NULL);

	if ((type & E_PASSWORDS_ONLINE) && !ep_online_state)
		return NULL;

	msg = ep_msg_new (ep_ask_password);
	msg->title = title;
	msg->component = component_name;
	msg->key = key;
	msg->prompt = prompt;
	msg->flags = type;
	msg->remember = remember;
	msg->parent = parent;

	ep_msg_send (msg);
	passwd = msg->password;
	msg->password = NULL;
	ep_msg_free (msg);

	return passwd;
}
