/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 * e-passwords.c
 *
 * Copyright (C) 2001 Ximian, Inc.
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

#include <string.h>
#include <gtk/gtk.h>
#include <glib/gi18n-lib.h>

#include "e-passwords.h"
#include "libedataserver/e-msgport.h"
#include "libedataserver/e-url.h"

#ifdef WITH_GNOME_KEYRING
#include <gnome-keyring.h>
#endif

#ifndef ENABLE_THREADS
#define ENABLE_THREADS (1)
#endif

#ifdef ENABLE_THREADS
#include <pthread.h>

static pthread_t main_thread;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
#define LOCK() pthread_mutex_lock (&lock)
#define UNLOCK() pthread_mutex_unlock (&lock)
#else
#define LOCK()
#define UNLOCK()
#endif

#define d(x)

struct _EPassMsg {
	EMsg msg;

	void (*dispatch) (struct _EPassMsg *);

	/* input */
	struct _GtkWindow *parent;
	const gchar *component;
	const gchar *key;
	const gchar *title;
	const gchar *prompt;
	const gchar *oldpass;
	guint32 flags;

	/* output */
	gboolean *remember;
	gchar *password;

	/* work variables */
	GtkWidget *entry;
	GtkWidget *check;
	guint ismain:1;
	guint noreply:1;	/* supress replies; when calling
				 * dispatch functions from others */
};

typedef struct _EPassMsg EPassMsg;

static GHashTable *password_cache = NULL;
static GtkDialog *password_dialog = NULL;
static EDList request_list = E_DLIST_INITIALISER (request_list);
static gint idle_id;
static gint ep_online_state = TRUE;

#define KEY_FILE_GROUP_PREFIX "Passwords-"
static GKeyFile *key_file = NULL;

static gchar *
ep_key_file_get_filename (void)
{
	/* XXX It would be nice to someday move this data elsewhere, or else
	 * fully migrate to GNOME Keyring or whatever software supercedes it.
	 * Evolution is one of the few remaining GNOME-2 applications that
	 * still uses the deprecated ~/.gnome2_private directory. */

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

	filename = ep_key_file_get_filename ();

	if (!g_file_test (filename, G_FILE_TEST_EXISTS))
		goto exit;

	g_key_file_load_from_file (
		key_file, filename, G_KEY_FILE_KEEP_COMMENTS |
		G_KEY_FILE_KEEP_TRANSLATIONS, &error);

	if (error != NULL) {
		g_warning ("%s", error->message);
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
	gsize length;
	GError *error = NULL;

	filename = ep_key_file_get_filename ();
	contents = g_key_file_to_data (key_file, &length, NULL);

	g_file_set_contents (filename, contents, length, &error);

	if (error != NULL) {
		g_warning ("%s", error->message);
		g_error_free (error);
	}

	g_free (contents);
	g_free (filename);
}

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
ep_idle_dispatch (void *data)
{
	EPassMsg *msg;

	/* As soon as a password window is up we stop; it will
	   re-invoke us when it has been closed down */
	LOCK ();
	while (password_dialog == NULL && (msg = (EPassMsg *) e_dlist_remhead (&request_list))) {
		UNLOCK ();

		msg->dispatch (msg);

		LOCK ();
	}

	idle_id = 0;
	UNLOCK ();

	return FALSE;
}

static EPassMsg *
ep_msg_new (void (*dispatch) (EPassMsg *))
{
	EPassMsg *msg;

	e_passwords_init ();

	msg = g_malloc0 (sizeof (*msg));
	msg->dispatch = dispatch;
	msg->msg.reply_port = e_msgport_new ();
#ifdef ENABLE_THREADS
	msg->ismain = pthread_equal (pthread_self (), main_thread);
#else
	msg->ismain = TRUE;
#endif
	return msg;
}

static void
ep_msg_free (EPassMsg *msg)
{
	e_msgport_destroy (msg->msg.reply_port);
	g_free (msg->password);
	g_free (msg);
}

static void
ep_msg_send (EPassMsg *msg)
{
	gint needidle = 0;

	LOCK ();
	e_dlist_addtail (&request_list, (EDListNode *) &msg->msg);
	if (!idle_id) {
		if (!msg->ismain)
			idle_id = g_idle_add (ep_idle_dispatch, NULL);
		else
			needidle = 1;
	}
	UNLOCK ();

	if (msg->ismain) {
		EPassMsg *m;

		if (needidle)
			ep_idle_dispatch (NULL);
		while ((m = (EPassMsg *) e_msgport_get (msg->msg.reply_port)) == NULL)
			g_main_context_iteration (NULL, TRUE);
		g_assert (m == msg);
	} else {
		EMsg *reply_msg = e_msgport_wait (msg->msg.reply_port);
		g_assert (reply_msg == &msg->msg);
	}
}

/* the functions that actually do the work */
#ifdef WITH_GNOME_KEYRING
static void
ep_clear_passwords_keyring (EPassMsg *msg)
{
	GnomeKeyringAttributeList *attributes;
	GnomeKeyringResult result;
	GList *matches = NULL, *tmp;
	gchar *default_keyring = NULL;

	result = gnome_keyring_get_default_keyring_sync (&default_keyring);
	if (!default_keyring) {
	        if (gnome_keyring_create_sync ("default", NULL) != GNOME_KEYRING_RESULT_OK)
			return;
	        default_keyring = g_strdup ("default");
	}

	d(g_print ("Get Default %d\n", result));

	/* Not called at all */
	attributes = gnome_keyring_attribute_list_new ();
	gnome_keyring_attribute_list_append_string (attributes, "application", "Evolution");

	result = gnome_keyring_find_items_sync (GNOME_KEYRING_ITEM_NETWORK_PASSWORD, attributes, &matches);
	d(g_print ("Find Items %d\n", result));

	gnome_keyring_attribute_list_free (attributes);

	if (result) {
		g_print ("Couldn't clear password");
	} else {
		for (tmp = matches; tmp; tmp = tmp->next) {
			result = gnome_keyring_item_delete_sync (default_keyring, ((GnomeKeyringFound *) tmp->data)->item_id);
			d(g_print ("Delete Items %d\n", result));
		}
	}

	g_free (default_keyring);
}
#endif

static void
ep_clear_passwords_keyfile (EPassMsg *msg)
{
	gchar *group;
	GError *error = NULL;

	group = ep_key_file_get_group (msg->component);

	g_key_file_remove_group (key_file, group, &error);
	if (error == NULL)
		ep_key_file_save ();
	else {
		if (error) {
			g_warning ("%s", error->message);
			g_error_free (error);
		}
	}

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
		e_msgport_reply (&msg->msg);
}

#ifdef WITH_GNOME_KEYRING
static void
ep_forget_passwords_keyring (EPassMsg *msg)
{
	GnomeKeyringAttributeList *attributes;
	GnomeKeyringResult result;
	GList *matches = NULL, *tmp;
	gchar *default_keyring = NULL;

	result = gnome_keyring_get_default_keyring_sync (&default_keyring);
	if (!default_keyring) {
	        if (gnome_keyring_create_sync ("default", NULL) != GNOME_KEYRING_RESULT_OK)
			return;
	        default_keyring = g_strdup ("default");
	}
	d(g_print ("Get Default %d\n", result));

	attributes = gnome_keyring_attribute_list_new ();
	gnome_keyring_attribute_list_append_string (attributes, "application", "Evolution");

	result = gnome_keyring_find_items_sync (GNOME_KEYRING_ITEM_NETWORK_PASSWORD, attributes, &matches);
	d(g_print ("Find Items %d\n", result));

	gnome_keyring_attribute_list_free (attributes);

	if (result) {
		g_print ("Couldn't clear password");
	} else {
		for (tmp = matches; tmp; tmp = tmp->next) {
			result = gnome_keyring_item_delete_sync (default_keyring, ((GnomeKeyringFound *) tmp->data)->item_id);
			d(g_print ("Delete Items %d\n", result));
		}
	}

	g_free (default_keyring);

	/* free up the session passwords */
	g_hash_table_remove_all (password_cache);
}
#endif

static void
ep_forget_passwords_keyfile (EPassMsg *msg)
{
	gchar **groups;
	gsize length, ii;

	g_hash_table_remove_all (password_cache);

	groups = g_key_file_get_groups (key_file, &length);
	for (ii = 0; ii < length; ii++) {
		if (!g_str_has_prefix (groups[ii], KEY_FILE_GROUP_PREFIX))
			continue;
		g_key_file_remove_group (key_file, groups[ii], NULL);
	}
	ep_key_file_save ();
	g_strfreev (groups);
}

static void
ep_forget_passwords (EPassMsg *msg)
{
#ifdef WITH_GNOME_KEYRING
	if (gnome_keyring_is_available ())
		ep_forget_passwords_keyring (msg);
	else
		ep_forget_passwords_keyfile (msg);
#else
	ep_forget_passwords_keyfile (msg);
#endif

	if (!msg->noreply)
		e_msgport_reply (&msg->msg);
}

#ifdef WITH_GNOME_KEYRING
static void
ep_remember_password_keyring (EPassMsg *msg)
{
	gchar *value;

	value = g_hash_table_lookup (password_cache, msg->key);
	if (value != NULL) {
		/* add it to the on-disk cache of passwords */
		GnomeKeyringAttributeList *attributes;
		GnomeKeyringResult result;
		EUri *uri = e_uri_new (msg->key);
		guint32 item_id;

		if (!strcmp (uri->protocol, "ldap") && !uri->user) {
			/* LDAP doesnt use username in url. Let the url be the user key. So safe it */
			gchar *keycopy = g_strdup (msg->key);
			gint i;

			for (i = 0; i < strlen (keycopy); i ++)
				if (keycopy[i] == '/' || keycopy[i] =='=')
					keycopy[i] = '_';
			uri->user = keycopy;
		}

		attributes = gnome_keyring_attribute_list_new ();
		gnome_keyring_attribute_list_append_string (attributes, "user", uri->user);
		gnome_keyring_attribute_list_append_string (attributes, "server", uri->host);
		gnome_keyring_attribute_list_append_string (attributes, "application", "Evolution");

		result = gnome_keyring_item_create_sync (NULL, /* Use default keyring */
						         GNOME_KEYRING_ITEM_NETWORK_PASSWORD, /* type */
				   			 msg->key, /* name */
				   			 attributes, /* attribute list */
				   			 value, /* password */
				   			 TRUE, /* Update if already exists */
				   			 &item_id);

		d(g_print ("Remember %s: %d/%d\n", msg->key, result, item_id));

		gnome_keyring_attribute_list_free (attributes);

		/* now remove it from our session hash */
		g_hash_table_remove (password_cache, msg->key);

		e_uri_free (uri);
	}
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
	g_key_file_set_string (key_file, group, key, password);
	ep_key_file_save ();

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
		e_msgport_reply (&msg->msg);
}

#ifdef WITH_GNOME_KEYRING
static void
ep_forget_password_keyring (EPassMsg *msg)
{
	GnomeKeyringAttributeList *attributes;
	GnomeKeyringResult result;
	GList *matches = NULL, *tmp;
	gchar *default_keyring = NULL;
	EUri *uri = e_uri_new (msg->key);

	if (!strcmp (uri->protocol, "ldap") && !uri->user) {
		/* LDAP doesnt use username in url. Let the url be the user key. So safe it */
		gchar *keycopy = g_strdup (msg->key);
		gint i;

		for (i = 0; i < strlen (keycopy); i ++)
			if (keycopy[i] == '/' || keycopy[i] =='=')
				keycopy[i] = '_';
		uri->user = keycopy;
	}

	g_hash_table_remove (password_cache, msg->key);

	if (!uri->host && !uri->user)
		/* No need to remove from keyring for pass phrases */
		goto exit;

	result = gnome_keyring_get_default_keyring_sync (&default_keyring);
	if (!default_keyring) {
	        if (gnome_keyring_create_sync ("default", NULL) != GNOME_KEYRING_RESULT_OK)
			goto exit;
	        default_keyring = g_strdup ("default");
	}

	d(g_print ("Get Default %d\n", result));

	attributes = gnome_keyring_attribute_list_new ();
	gnome_keyring_attribute_list_append_string (attributes, "user", uri->user);
	gnome_keyring_attribute_list_append_string (attributes, "server", uri->host);
	gnome_keyring_attribute_list_append_string (attributes, "application", "Evolution");

	result = gnome_keyring_find_items_sync (GNOME_KEYRING_ITEM_NETWORK_PASSWORD, attributes, &matches);
	d(g_print ("Find Items %d\n", result));

	gnome_keyring_attribute_list_free (attributes);

	if (result) {
		g_print ("Couldn't clear password");
	} else {
		for (tmp = matches; tmp; tmp = tmp->next) {
			GArray *pattr = ((GnomeKeyringFound *) tmp->data)->attributes;
			gint i;
			GnomeKeyringAttribute *attr;
			gboolean accept = TRUE;
			guint present = 0;

			for (i =0; (i < pattr->len) && accept; i++)
			{
				attr = &g_array_index (pattr, GnomeKeyringAttribute, i);
				if (!strcmp (attr->name, "user")) {
					present++;
					if (strcmp (attr->value.string, uri->user))
						accept = FALSE;
				} else if (!strcmp (attr->name, "server")) {
					present++;
					if (strcmp (attr->value.string, uri->host))
						accept = FALSE;
				}
			}
				if (present == 2 && accept) {
					result = gnome_keyring_item_delete_sync (default_keyring, ((GnomeKeyringFound *) tmp->data)->item_id);
					d(g_print ("Delete Items %s %s %d\n", uri->host, uri->user, result));
				}
		}

	}

	g_free (default_keyring);

exit:
	e_uri_free (uri);
}
#endif

static void
ep_forget_password_keyfile (EPassMsg *msg)
{
	gchar *group, *key;
	GError *error = NULL;

	g_hash_table_remove (password_cache, msg->key);

	group = ep_key_file_get_group (msg->component);
	key = ep_key_file_normalize_key (msg->key);

	g_key_file_remove_key (key_file, group, key, &error);
	if (error == NULL)
		ep_key_file_save ();
	else {
		g_warning ("%s", error->message);
		g_error_free (error);
	}

	g_free (group);
	g_free (key);
}

static void
ep_forget_password (EPassMsg *msg)
{
#ifdef WITH_GNOME_KEYRING
	if (gnome_keyring_is_available ())
		ep_forget_password_keyring (msg);
	else
		ep_forget_password_keyfile (msg);
#else
	ep_forget_password_keyfile (msg);
#endif

	if (!msg->noreply)
		e_msgport_reply (&msg->msg);
}

#ifdef WITH_GNOME_KEYRING
static void
ep_get_password_keyring (EPassMsg *msg)
{
	gchar *passwd;
	GnomeKeyringAttributeList *attributes;
	GnomeKeyringResult result;
	GList *matches = NULL, *tmp;

	passwd = g_hash_table_lookup (password_cache, msg->key);
	if (passwd) {
		msg->password = g_strdup (passwd);
	} else {
		EUri *uri = e_uri_new (msg->key);

		if (!strcmp (uri->protocol, "ldap") && !uri->user) {
			/* LDAP doesnt use username in url. Let the url be the user key. So safe it */
			gchar *keycopy = g_strdup (msg->key);
			gint i;

			for (i = 0; i < strlen (keycopy); i ++)
				if (keycopy[i] == '/' || keycopy[i] =='=')
					keycopy[i] = '_';
			uri->user = keycopy;
		}

		if (uri->host &&  uri->user) {
			/* We dont store passphrases.*/

			attributes = gnome_keyring_attribute_list_new ();
			gnome_keyring_attribute_list_append_string (attributes, "user", uri->user);
			gnome_keyring_attribute_list_append_string (attributes, "server", uri->host);
			gnome_keyring_attribute_list_append_string (attributes, "application", "Evolution");
			d(printf ("get %s %s\n", uri->user, msg->key));

			result = gnome_keyring_find_items_sync (GNOME_KEYRING_ITEM_NETWORK_PASSWORD, attributes, &matches);
			d(g_print ("Find Items %d\n", result));

			gnome_keyring_attribute_list_free (attributes);

			if (result) {
				g_print ("Couldn't Get password %d\n", result);
			} else {
				/* FIXME: What to do if this returns more than one? */
				for (tmp = matches; tmp; tmp = tmp->next) {
					GArray *pattr = ((GnomeKeyringFound *) tmp->data)->attributes;
					gint i;
					GnomeKeyringAttribute *attr;
					gboolean accept = TRUE;
					guint present = 0;

					for (i =0; (i < pattr->len) && accept; i++)
					{
						attr = &g_array_index (pattr, GnomeKeyringAttribute, i);

						if (!strcmp (attr->name, "user") && attr->value.string) {
							present++;
							if (strcmp (attr->value.string, uri->user))
								accept = FALSE;
						} else if (!strcmp (attr->name, "server") && attr->value.string) {
							present++;
							if (strcmp (attr->value.string, uri->host))
								accept = FALSE;
						}
					}
					if (present == 2 && accept) {
						msg->password = g_strdup (((GnomeKeyringFound *) tmp->data)->secret);
						break;
					}
				}
			}
		}

		e_uri_free (uri);
	}
}
#endif

static void
ep_get_password_keyfile (EPassMsg *msg)
{
	gchar *group, *key, *password;
	GError *error = NULL;

	password = g_hash_table_lookup (password_cache, msg->key);
	if (password != NULL) {
		msg->password = g_strdup (password);
		return;
	}

	group = ep_key_file_get_group (msg->component);
	key = ep_key_file_normalize_key (msg->key);

	password = g_key_file_get_string (key_file, group, key, &error);
	if (password != NULL) {
		msg->password = ep_password_decode (password);
		g_free (password);
	} else {
		if (error) {
			g_warning ("%s", error->message);
			g_error_free (error);
		}
	}

	g_free (group);
	g_free (key);
}

static void
ep_get_password (EPassMsg *msg)
{
#ifdef WITH_GNOME_KEYRING
	if (gnome_keyring_is_available ())
		ep_get_password_keyring (msg);
	else
		ep_get_password_keyfile (msg);
#else
	ep_get_password_keyfile (msg);
#endif

	if (!msg->noreply)
		e_msgport_reply (&msg->msg);
}

static void
ep_add_password (EPassMsg *msg)
{
	g_hash_table_insert (
		password_cache, g_strdup (msg->key),
		g_strdup (msg->oldpass));

	if (!msg->noreply)
		e_msgport_reply (&msg->msg);
}

static void ep_ask_password (EPassMsg *msg);

static void
pass_response (GtkDialog *dialog, gint response, void *data)
{
	EPassMsg *msg = data;
	gint type = msg->flags & E_PASSWORDS_REMEMBER_MASK;
	EDList pending = E_DLIST_INITIALISER (pending);
	EPassMsg *mw, *mn;

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

	LOCK ();
	mw = (EPassMsg *)request_list.head;
	mn = (EPassMsg *)mw->msg.ln.next;
	while (mn) {
		if ((mw->dispatch == ep_forget_password
		     || mw->dispatch == ep_get_password
		     || mw->dispatch == ep_ask_password)
		    && (strcmp (mw->component, msg->component) == 0
			&& strcmp (mw->key, msg->key) == 0)) {
			e_dlist_remove ((EDListNode *)mw);
			mw->password = g_strdup (msg->password);
			e_msgport_reply (&mw->msg);
		}
		mw = mn;
		mn = (EPassMsg *)mn->msg.ln.next;
	}
	UNLOCK ();

	if (!msg->noreply)
		e_msgport_reply (&msg->msg);

	ep_idle_dispatch (NULL);
}

static void
ep_ask_password (EPassMsg *msg)
{
	GtkWidget *widget;
	GtkWidget *container;
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
	gtk_container_set_border_width (GTK_CONTAINER (widget), 12);
	password_dialog = GTK_DIALOG (widget);

        /* Override GtkDialog defaults */
	widget = password_dialog->vbox;
	gtk_box_set_spacing (GTK_BOX (widget), 12);
	gtk_container_set_border_width (GTK_CONTAINER (widget), 0);
	widget = password_dialog->action_area;
	gtk_box_set_spacing (GTK_BOX (widget), 12);
	gtk_container_set_border_width (GTK_CONTAINER (widget), 0);

	/* Table */
	container = gtk_table_new (2, 3, FALSE);
	gtk_table_set_col_spacings (GTK_TABLE (container), 12);
	gtk_table_set_row_spacings (GTK_TABLE (container), 12);
	gtk_table_set_row_spacing (GTK_TABLE (container), 1, 6);
	gtk_widget_show (container);

	gtk_box_pack_start (
		GTK_BOX (password_dialog->vbox),
		container, FALSE, TRUE, 0);

	/* Question Image */
	widget = gtk_image_new_from_stock (
		GTK_STOCK_DIALOG_QUESTION,
		GTK_ICON_SIZE_DIALOG);
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
		1, 2, 0, 1, GTK_EXPAND | GTK_FILL, GTK_FILL, 0, 0);

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
		1, 2, 1, 2, GTK_EXPAND | GTK_FILL, GTK_FILL, 0, 0);

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
			1, 2, 2, 3, GTK_EXPAND | GTK_FILL, GTK_FILL, 0, 0);
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
	LOCK ();

	if (password_cache == NULL) {
		password_cache = g_hash_table_new_full (
			g_str_hash, g_str_equal,
			(GDestroyNotify) g_free,
			(GDestroyNotify) g_free);
#ifdef ENABLE_THREADS
		main_thread = pthread_self ();
#endif

#ifdef WITH_GNOME_KEYRING
		if (!gnome_keyring_is_available ()) {
			key_file = g_key_file_new ();
			ep_key_file_load ();
		}
#else
		key_file = g_key_file_new ();
		ep_key_file_load ();
#endif
	}

	UNLOCK ();
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

	LOCK ();
	while ((msg = (EPassMsg *)e_dlist_remhead (&request_list)))
		e_msgport_reply (&msg->msg);
	UNLOCK ();

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
	e_passwords_cancel ();

	if (password_cache != NULL) {
		g_hash_table_destroy (password_cache);
		password_cache = NULL;
	}
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
 * Return value: the password associated with @key, or %NULL.  Caller
 * must free the returned password.
 **/
char *
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
 * Return value: the password, which the caller must free, or %NULL if
 * the user cancelled the operation. *@remember will be set if the
 * return value is non-%NULL and @remember_type is not
 * E_PASSWORDS_DO_NOT_REMEMBER.
 **/
char *
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
