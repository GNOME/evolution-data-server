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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
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
#include <libgnome/gnome-config.h>
#include <glib/gi18n-lib.h>
#include <gtk/gtkversion.h>
#include <gtk/gtkentry.h>
#include <gtk/gtkvbox.h>
#include <gtk/gtkcheckbutton.h>
#include <gtk/gtkmessagedialog.h>

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
#define LOCK() pthread_mutex_lock(&lock)
#define UNLOCK() pthread_mutex_unlock(&lock)
#else
#define LOCK()
#define UNLOCK()
#endif

#define d(x) x

struct _EPassMsg {
	EMsg msg;

	void (*dispatch)(struct _EPassMsg *);

	/* input */
	struct _GtkWindow *parent;	
	const char *component;
	const char *key;
	const char *title;
	const char *prompt;
	const char *oldpass;
	guint32 flags;

	/* output */
	gboolean *remember;
	char *password;

	/* work variables */
	GtkWidget *entry;
	GtkWidget *check;
	guint ismain:1;
	guint noreply:1;	/* supress replies; when calling
				 * dispatch functions from others */
};

typedef struct _EPassMsg EPassMsg;

static GHashTable *passwords = NULL;
static GtkDialog *password_dialog;
static EDList request_list = E_DLIST_INITIALISER(request_list);
static int idle_id;
static int ep_online_state = TRUE;

static char *decode_base64 (char *base64);
static int base64_encode_close(unsigned char *in, int inlen, gboolean break_lines, unsigned char *out, int *state, int *save);
static int base64_encode_step(unsigned char *in, int len, gboolean break_lines, unsigned char *out, int *state, int *save);

static gboolean
ep_idle_dispatch(void *data)
{
	EPassMsg *msg;

	/* As soon as a password window is up we stop; it will
	   re-invoke us when it has been closed down */
	LOCK();
	while (password_dialog == NULL && (msg = (EPassMsg *)e_dlist_remhead(&request_list))) {
		UNLOCK();

		msg->dispatch(msg);

		LOCK();
	}

	idle_id = 0;
	UNLOCK();

	return FALSE;
}

static EPassMsg *
ep_msg_new(void (*dispatch)(EPassMsg *))
{
	EPassMsg *msg;

	e_passwords_init();

	msg = g_malloc0(sizeof(*msg));
	msg->dispatch = dispatch;
	msg->msg.reply_port = e_msgport_new();
#ifdef ENABLE_THREADS
	msg->ismain = pthread_equal(pthread_self(), main_thread);
#else
	msg->ismain = TRUE;
#endif
	return msg;
}

static void
ep_msg_free(EPassMsg *msg)
{
	e_msgport_destroy(msg->msg.reply_port);
	g_free(msg->password);
	g_free(msg);
}

static void
ep_msg_send(EPassMsg *msg)
{
	int needidle = 0;

	LOCK();
	e_dlist_addtail(&request_list, (EDListNode *)&msg->msg);
	if (!idle_id) {
		if (!msg->ismain)
			idle_id = g_idle_add(ep_idle_dispatch, NULL);
		else
			needidle = 1;
	}
	UNLOCK();

	if (msg->ismain) {
		EPassMsg *m;

		if (needidle)
			ep_idle_dispatch(NULL);
		while ((m = (EPassMsg *)e_msgport_get(msg->msg.reply_port)) == NULL)
			g_main_context_iteration(NULL, TRUE);
		g_assert(m == msg);
	} else {
		EMsg *reply_msg = e_msgport_wait(msg->msg.reply_port);
		g_assert(reply_msg == &msg->msg);
	}
}

/* the functions that actually do the work */
#ifdef WITH_GNOME_KEYRING
static void
ep_clear_passwords_keyring(EPassMsg *msg)
{
	GnomeKeyringAttributeList *attributes;
	GnomeKeyringAttribute attribute;
	GnomeKeyringResult result;
	GList *matches = NULL, *tmp;	
	char *path;
	char *default_keyring = NULL;

	result = gnome_keyring_get_default_keyring_sync (&default_keyring);
	if (!default_keyring) {
	        if (gnome_keyring_create_sync ("default", NULL) != GNOME_KEYRING_RESULT_OK) {
				if (!msg->noreply)
					e_msgport_reply(&msg->msg);
				return;
		}
	        default_keyring = g_strdup ("default");			
	}

	d(g_print("Get Default %d\n", result));
	
	/* Not called at all */
	path = g_strdup ("Evolution");
	attributes = gnome_keyring_attribute_list_new ();

	attribute.name = g_strdup ("application");
	attribute.type = GNOME_KEYRING_ATTRIBUTE_TYPE_STRING;
	attribute.value.string = path;
	g_array_append_val (attributes, attribute);

	result = gnome_keyring_find_items_sync (GNOME_KEYRING_ITEM_NETWORK_PASSWORD, attributes, &matches);
	d(g_print ("Find Items %d\n", result));
		
	gnome_keyring_attribute_list_free (attributes);

	if (result) {
		g_print ("Couldn't clear password");
	} else {
		for (tmp = matches; tmp; tmp = tmp->next) {
			result = gnome_keyring_item_delete_sync (default_keyring, ((GnomeKeyringFound *) tmp->data)->item_id);
			d(g_print("Delete Items %d\n", result));
		}
	}
	
	g_free (default_keyring);
	if (!msg->noreply)
		e_msgport_reply(&msg->msg);
}
#endif
static void
ep_clear_passwords_file(EPassMsg *msg)
{
	char *path;

	path = g_strdup_printf ("/Evolution/Passwords-%s", msg->component);

	gnome_config_private_clean_section (path);
	gnome_config_private_sync_file ("/Evolution");

	g_free (path);

	if (!msg->noreply)
		e_msgport_reply(&msg->msg);
}

static gboolean
free_entry (gpointer key, gpointer value, gpointer user_data)
{
	g_free (key);
	memset (value, 0, strlen (value));
	g_free (value);
	return TRUE;
}

#ifdef WITH_GNOME_KEYRING
static void
ep_forget_passwords_keyring(EPassMsg *msg)
{
	GnomeKeyringAttributeList *attributes;
	GnomeKeyringAttribute attribute;
	GnomeKeyringResult result;
	GList *matches = NULL, *tmp;	
	char *path;
	char *default_keyring = NULL;

	result = gnome_keyring_get_default_keyring_sync (&default_keyring);
	if (!default_keyring) {
	        if (gnome_keyring_create_sync ("default", NULL) != GNOME_KEYRING_RESULT_OK) {
				if (!msg->noreply)
					e_msgport_reply(&msg->msg);
				return;
		}
	        default_keyring = g_strdup ("default");			
	}	
	d(g_print("Get Default %d\n", result));
	
	path = g_strdup ("Evolution");
	attributes = gnome_keyring_attribute_list_new ();

	attribute.name = g_strdup ("application");
	attribute.type = GNOME_KEYRING_ATTRIBUTE_TYPE_STRING;
	attribute.value.string = path;
	g_array_append_val (attributes, attribute);


	result = gnome_keyring_find_items_sync (GNOME_KEYRING_ITEM_NETWORK_PASSWORD, attributes, &matches);
	d(g_print ("Find Items %d\n", result));
		
	gnome_keyring_attribute_list_free (attributes);

	if (result) {
		g_print ("Couldn't clear password");
	} else {
		for (tmp = matches; tmp; tmp = tmp->next) {
			result = gnome_keyring_item_delete_sync (default_keyring, ((GnomeKeyringFound *) tmp->data)->item_id);
			d(g_print("Delete Items %d\n", result));
		}
	}
	
	g_free (default_keyring);

	/* free up the session passwords */
	g_hash_table_foreach_remove (passwords, free_entry, NULL);

	if (!msg->noreply)
		e_msgport_reply(&msg->msg);
}
#endif

static void
ep_forget_passwords_file(EPassMsg *msg)
{
	void *it;
	char *key;

	it = gnome_config_private_init_iterator_sections("/Evolution");
	while ( (it = gnome_config_iterator_next(it, &key, NULL)) ) {
		if (0 == strncmp(key, "Passwords-", 10)) {
			char *section = g_strdup_printf("/Evolution/%s", key);

			gnome_config_private_clean_section (section);
			g_free(section);
		}
		g_free(key);
	}

	gnome_config_private_sync_file ("/Evolution");

	/* free up the session passwords */
	g_hash_table_foreach_remove (passwords, free_entry, NULL);

	if (!msg->noreply)
		e_msgport_reply(&msg->msg);
}

static char *
password_path (const char *component_name, const char *key)
{
	char *keycopy, *path;
	int i;
	keycopy = g_strdup (key);

	for (i = 0; i < strlen (keycopy); i ++)
		if (keycopy[i] == '/' || keycopy[i] =='=')
			keycopy[i] = '_';
	
	path = g_strdup_printf ("/Evolution/Passwords-%s/%s", component_name, keycopy);

	g_free (keycopy);

	return path;
}

#ifdef WITH_GNOME_KEYRING
static void
ep_remember_password_keyring(EPassMsg *msg)
{
	gpointer okey, value;

	if (g_hash_table_lookup_extended (passwords, msg->key, &okey, &value)) {
		/* add it to the on-disk cache of passwords */
		GnomeKeyringAttributeList *attributes;
		GnomeKeyringAttribute attribute;
		GnomeKeyringResult result;
		EUri *uri = e_uri_new (okey);
		guint32 item_id;

		if (!strcmp (uri->protocol, "ldap") && !uri->user) {
			/* LDAP doesnt use username in url. Let the url be the user key. So safe it */
			char *keycopy = g_strdup (msg->key);
			int i;
			
			for (i = 0; i < strlen (keycopy); i ++)
				if (keycopy[i] == '/' || keycopy[i] =='=')
					keycopy[i] = '_';		
			uri->user = keycopy;
		}
		
		attributes = gnome_keyring_attribute_list_new ();

		attribute.name = g_strdup ("user");
		attribute.type = GNOME_KEYRING_ATTRIBUTE_TYPE_STRING;
		attribute.value.string = g_strdup (uri->user);
		g_array_append_val (attributes, attribute);
	
		attribute.name = g_strdup ("server");
		attribute.type = GNOME_KEYRING_ATTRIBUTE_TYPE_STRING;
		attribute.value.string = g_strdup (uri->host);
		g_array_append_val (attributes, attribute);
	 
		attribute.name = g_strdup ("application");
		attribute.type = GNOME_KEYRING_ATTRIBUTE_TYPE_STRING;
		attribute.value.string = g_strdup ("Evolution");
		g_array_append_val (attributes, attribute);		
		
		result = gnome_keyring_item_create_sync (NULL, /* Use default keyring */
						         GNOME_KEYRING_ITEM_NETWORK_PASSWORD, /* type */
				   			 msg->key, /* name */
				   			 attributes, /* attribute list */
				   			 value, /* password */
				   			 TRUE, /* Update if already exists */
				   			 &item_id);
	
		d(g_print("Remember %s: %d/%d\n", msg->key, result, item_id));
		/* now remove it from our session hash */
		g_hash_table_remove (passwords, msg->key);
		g_free (okey);
		g_free (value);
		
		e_uri_free (uri);
	}

	if (!msg->noreply)
		e_msgport_reply(&msg->msg);
}
#endif

static void
ep_remember_password_file(EPassMsg *msg)
{
	gpointer okey, value;
	char *path, *pass64;
	int len, state, save;

	if (g_hash_table_lookup_extended (passwords, msg->key, &okey, &value)) {
		/* add it to the on-disk cache of passwords */
		path = password_path (msg->component, okey);

		len = strlen (value);
		pass64 = g_malloc0 ((len + 2) * 4 / 3 + 1);
		state = save = 0;
		base64_encode_close (value, len, FALSE, (guchar *)pass64, &state, &save);

		gnome_config_private_set_string (path, pass64);
		g_free (path);
		g_free (pass64);

		/* now remove it from our session hash */
		g_hash_table_remove (passwords, msg->key);
		g_free (okey);
		g_free (value);

		gnome_config_private_sync_file ("/Evolution");
	}

	if (!msg->noreply)
		e_msgport_reply(&msg->msg);
}

#ifdef WITH_GNOME_KEYRING
static void
ep_forget_password_keyring (EPassMsg *msg)
{
	GnomeKeyringAttributeList *attributes;
	GnomeKeyringAttribute attribute;
	GnomeKeyringResult result;
	GList *matches = NULL, *tmp;	
	char *default_keyring = NULL;	
	gpointer okey, value;
	EUri *uri = e_uri_new (msg->key);

	if (!strcmp (uri->protocol, "ldap") && !uri->user) {
		/* LDAP doesnt use username in url. Let the url be the user key. So safe it */
		char *keycopy = g_strdup (msg->key);
		int i;

		for (i = 0; i < strlen (keycopy); i ++)
			if (keycopy[i] == '/' || keycopy[i] =='=')
				keycopy[i] = '_';		
		uri->user = keycopy;
	}
	    
	if (g_hash_table_lookup_extended (passwords, msg->key, &okey, &value)) {
		g_hash_table_remove (passwords, msg->key);
		memset (value, 0, strlen (value));
		g_free (okey);
		g_free (value);
	}

	if (!uri->host && !uri->user) {
		/* No need to remove from keyring for pass phrases */
		if (!msg->noreply)
			e_msgport_reply(&msg->msg);
		return;
	}
	
	result = gnome_keyring_get_default_keyring_sync (&default_keyring);
	if (!default_keyring) {
	        if (gnome_keyring_create_sync ("default", NULL) != GNOME_KEYRING_RESULT_OK) {
				if (!msg->noreply)
					e_msgport_reply(&msg->msg);
				return;
		}
	        default_keyring = g_strdup ("default");			
	}

	d(g_print("Get Default %d\n", result));
	
	attributes = gnome_keyring_attribute_list_new ();
	attribute.name = g_strdup ("user");
	attribute.type = GNOME_KEYRING_ATTRIBUTE_TYPE_STRING;
	attribute.value.string = g_strdup(uri->user);;
	g_array_append_val (attributes, attribute);

	attributes = gnome_keyring_attribute_list_new ();
	attribute.name = g_strdup ("server");
	attribute.type = GNOME_KEYRING_ATTRIBUTE_TYPE_STRING;
	attribute.value.string = g_strdup(uri->host);;
	g_array_append_val (attributes, attribute);

	attributes = gnome_keyring_attribute_list_new ();
	attribute.name = g_strdup ("application");
	attribute.type = GNOME_KEYRING_ATTRIBUTE_TYPE_STRING;
	attribute.value.string = g_strdup("Evolution");;
	g_array_append_val (attributes, attribute);	

	result = gnome_keyring_find_items_sync (GNOME_KEYRING_ITEM_NETWORK_PASSWORD, attributes, &matches);
	d(g_print ("Find Items %d\n", result));
		
	gnome_keyring_attribute_list_free (attributes);

	if (result) {
		g_print ("Couldn't clear password");
	} else {
		for (tmp = matches; tmp; tmp = tmp->next) {
			GArray *pattr = ((GnomeKeyringFound *) tmp->data)->attributes;
			int i;
			GnomeKeyringAttribute *attr;
			gboolean accept = TRUE;
			guint present = 0;

			for (i =0; (i < pattr->len) && accept; i++)
			{
				attr = &g_array_index (pattr, GnomeKeyringAttribute, i);
				if (!strcmp(attr->name, "user")) {
					present++;
					if (strcmp (attr->value.string, uri->user))
						accept = FALSE;
				} else if (!strcmp(attr->name, "server")) {
					present++;
					if (strcmp (attr->value.string, uri->host))
						accept = FALSE;						
				}
			}
				if (present == 2 && accept) {
					result = gnome_keyring_item_delete_sync (default_keyring, ((GnomeKeyringFound *) tmp->data)->item_id);
					d(g_print("Delete Items %s %s %d\n", uri->host, uri->user, result));			
				}
		}	

	}
	
	g_free (default_keyring);

	if (!msg->noreply)
		e_msgport_reply(&msg->msg);
}
#endif

static void
ep_forget_password_file (EPassMsg *msg)
{
	gpointer okey, value;
	char *path;

	if (g_hash_table_lookup_extended (passwords, msg->key, &okey, &value)) {
		g_hash_table_remove (passwords, msg->key);
		memset (value, 0, strlen (value));
		g_free (okey);
		g_free (value);
	}

	/* clear it in the on disk db */
	path = password_path (msg->component, msg->key);
	gnome_config_private_clean_key (path);
	gnome_config_private_sync_file ("/Evolution");
	g_free (path);
	
	if (!msg->noreply)
		e_msgport_reply(&msg->msg);
}


#ifdef WITH_GNOME_KEYRING
static void
ep_get_password_keyring (EPassMsg *msg)
{
	char *passwd;
	GnomeKeyringAttributeList *attributes;
	GnomeKeyringAttribute attribute;
	GnomeKeyringResult result;
	GList *matches = NULL, *tmp;	

	passwd = g_hash_table_lookup (passwords, msg->key);
	if (passwd) {
		msg->password = g_strdup(passwd);
	} else {
		EUri *uri = e_uri_new (msg->key);
		
		if (!strcmp (uri->protocol, "ldap") && !uri->user) {
			/* LDAP doesnt use username in url. Let the url be the user key. So safe it */
			char *keycopy = g_strdup (msg->key);
			int i;

			for (i = 0; i < strlen (keycopy); i ++)
				if (keycopy[i] == '/' || keycopy[i] =='=')
					keycopy[i] = '_';		
			uri->user = keycopy;
		}
		
		if (uri->host &&  uri->user) {
			/* We dont store passphrases.*/

			attributes = gnome_keyring_attribute_list_new ();
			attribute.name = g_strdup ("user");
			attribute.type = GNOME_KEYRING_ATTRIBUTE_TYPE_STRING;
			attribute.value.string = g_strdup(uri->user);;
			g_array_append_val (attributes, attribute);
			printf("get %s %s\n", attribute.value.string, msg->key);

			attributes = gnome_keyring_attribute_list_new ();
			attribute.name = g_strdup ("server");
			attribute.type = GNOME_KEYRING_ATTRIBUTE_TYPE_STRING;
			attribute.value.string = g_strdup(uri->host);;
			g_array_append_val (attributes, attribute);
		
			attributes = gnome_keyring_attribute_list_new ();
			attribute.name = g_strdup ("application");
			attribute.type = GNOME_KEYRING_ATTRIBUTE_TYPE_STRING;
			attribute.value.string = g_strdup("Evolution");
			g_array_append_val (attributes, attribute);	

			result = gnome_keyring_find_items_sync (GNOME_KEYRING_ITEM_NETWORK_PASSWORD, attributes, &matches);
			d(g_print ("Find Items %d\n", result));
			
			gnome_keyring_attribute_list_free (attributes);

			if (result) {
				g_print ("Couldn't Get password %d\n", result);
			} else {
				/* FIXME: What to do if this returns more than one? */
				for (tmp = matches; tmp; tmp = tmp->next) {
					GArray *pattr = ((GnomeKeyringFound *) tmp->data)->attributes;
					int i;
					GnomeKeyringAttribute *attr;
					gboolean accept = TRUE;
					guint present = 0;

					for (i =0; (i < pattr->len) && accept; i++)
					{
						attr = &g_array_index (pattr, GnomeKeyringAttribute, i);

						if (!strcmp(attr->name, "user")) {
							present++;
							if (strcmp (attr->value.string, uri->user))
								accept = FALSE;
						} else if (!strcmp(attr->name, "server")) {
							present++;
							if (strcmp (attr->value.string, uri->host))
								accept = FALSE;						
						}
					}
					if (present == 2 && accept)
						msg->password = g_strdup (((GnomeKeyringFound *) tmp->data)->secret);
				}	
			}
		}
		
	}

	if (!msg->noreply)
		e_msgport_reply(&msg->msg);
}	
#endif

static void
ep_get_password_file (EPassMsg *msg)
{	
	char *path, *passwd;
	char *encoded = NULL;

	passwd = g_hash_table_lookup (passwords, msg->key);
	if (passwd) {
		msg->password = g_strdup(passwd);
	} else {
		/* not part of the session hash, look it up in the on disk db */
		path = password_path (msg->component, msg->key);
		encoded = gnome_config_private_get_string_with_default (path, NULL);
		g_free (path);
		if (encoded) {
			msg->password = decode_base64 (encoded);
			g_free (encoded);
		}
	}

	if (!msg->noreply)
		e_msgport_reply(&msg->msg);
}

static void
ep_add_password (EPassMsg *msg)
{
	gpointer okey, value;

	if (g_hash_table_lookup_extended (passwords, msg->key, &okey, &value)) {
		g_hash_table_remove (passwords, msg->key);
		g_free (okey);
		g_free (value);
	}

	g_hash_table_insert (passwords, g_strdup (msg->key), g_strdup (msg->oldpass));

	if (!msg->noreply)
		e_msgport_reply(&msg->msg);
}

static void ep_ask_password(EPassMsg *msg);

static void
pass_response(GtkDialog *dialog, int response, void *data)
{
	EPassMsg *msg = data;
	int type = msg->flags & E_PASSWORDS_REMEMBER_MASK;
	EDList pending = E_DLIST_INITIALISER(pending);
	EPassMsg *mw, *mn;

	if (response == GTK_RESPONSE_OK) {
		msg->password = g_strdup(gtk_entry_get_text((GtkEntry *)msg->entry));

		if (type != E_PASSWORDS_REMEMBER_NEVER) {
			int noreply = msg->noreply;

			*msg->remember = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (msg->check));

			msg->noreply = 1;

			if (*msg->remember || type == E_PASSWORDS_REMEMBER_FOREVER) {
				msg->oldpass = msg->password;
				ep_add_password(msg);
			}
#ifdef WITH_GNOME_KEYRING
			if (*msg->remember && type == E_PASSWORDS_REMEMBER_FOREVER) {
				if (gnome_keyring_is_available())
					ep_remember_password_keyring(msg);
				else
					ep_remember_password_file(msg);
			}
#else
			if (*msg->remember && type == E_PASSWORDS_REMEMBER_FOREVER)
				ep_remember_password_file(msg);				    
#endif				    

			msg->noreply = noreply;
		}
	}

	gtk_widget_destroy((GtkWidget *)dialog);
	password_dialog = NULL;

	/* ok, here things get interesting, we suck up any pending
	 * operations on this specific password, and return the same
	 * result or ignore other operations */

	LOCK();
	mw = (EPassMsg *)request_list.head;
	mn = (EPassMsg *)mw->msg.ln.next;
	while (mn) {
#ifdef WITH_GNOME_KEYRING		
		if ((mw->dispatch == (gnome_keyring_is_available() ? ep_forget_password_keyring : ep_forget_password_file)
#else
		if ((mw->dispatch == ep_forget_password_file		     
#endif
#ifdef WITH_GNOME_KEYRING				     
		     || mw->dispatch == (gnome_keyring_is_available() ? ep_get_password_keyring : ep_get_password_file)
#else
		     || mw->dispatch == ep_get_password_file		     
#endif
		     || mw->dispatch == ep_ask_password)		    
		    && (strcmp(mw->component, msg->component) == 0
			&& strcmp(mw->key, msg->key) == 0)) {
			e_dlist_remove((EDListNode *)mw);
			mw->password = g_strdup(msg->password);
			e_msgport_reply(&mw->msg);
		}
		mw = mn;
		mn = (EPassMsg *)mn->msg.ln.next;
	}
	UNLOCK();

	if (!msg->noreply)
		e_msgport_reply(&msg->msg);

	ep_idle_dispatch(NULL);
}

static void
ep_ask_password(EPassMsg *msg)
{
	GtkWidget *vbox;
	int type = msg->flags & E_PASSWORDS_REMEMBER_MASK;
	guint noreply = msg->noreply;
	AtkObject *a11y;

	msg->noreply = 1;

	/*password_dialog = (GtkDialog *)e_error_new(msg->parent, "mail:ask-session-password", msg->prompt, NULL);*/
	password_dialog = (GtkDialog *)gtk_message_dialog_new (msg->parent,
							       0,
							       GTK_MESSAGE_QUESTION,
							       GTK_BUTTONS_OK_CANCEL,
							       "%s", msg->prompt);
	gtk_window_set_title(GTK_WINDOW(password_dialog), msg->title);

	gtk_widget_ensure_style (GTK_WIDGET (password_dialog));
	gtk_container_set_border_width (GTK_CONTAINER (GTK_DIALOG (password_dialog)->vbox), 0);
	gtk_container_set_border_width (GTK_CONTAINER (GTK_DIALOG (password_dialog)->action_area), 12);

	gtk_dialog_set_default_response(password_dialog, GTK_RESPONSE_OK);

	vbox = gtk_vbox_new (FALSE, 12);
	gtk_widget_show (vbox);
	gtk_box_pack_start (GTK_BOX (GTK_DIALOG (password_dialog)->vbox), vbox, TRUE, FALSE, 0);
	gtk_container_set_border_width((GtkContainer *)vbox, 12);
	
	msg->entry = gtk_entry_new ();

	a11y = gtk_widget_get_accessible (msg->entry);
	atk_object_set_description (a11y, msg->prompt);
	gtk_entry_set_visibility ((GtkEntry *)msg->entry, !(msg->flags & E_PASSWORDS_SECRET));
	gtk_entry_set_activates_default((GtkEntry *)msg->entry, TRUE);
	gtk_box_pack_start (GTK_BOX (vbox), msg->entry, TRUE, FALSE, 3);
	gtk_widget_show (msg->entry);
	gtk_widget_grab_focus (msg->entry);
	
	if ((msg->flags & E_PASSWORDS_REPROMPT)) {
#ifdef WITH_GNOME_KEYRING
		if (gnome_keyring_is_available())
			ep_get_password_keyring(msg);
		else
			ep_get_password_file(msg);			
#else
		ep_get_password_file(msg);
#endif
		if (msg->password) {
			gtk_entry_set_text ((GtkEntry *) msg->entry, msg->password);
			g_free (msg->password);
			msg->password = NULL;
		}
	}

	/* static password, shouldn't be remembered between sessions,
	   but will be remembered within the session beyond our control */
	if (type != E_PASSWORDS_REMEMBER_NEVER) {
		if (msg->flags & E_PASSWORDS_PASSPHRASE) {
			msg->check = gtk_check_button_new_with_mnemonic(type == E_PASSWORDS_REMEMBER_FOREVER
									? _("_Remember this passphrase")
									: _("_Remember this passphrase for the remainder of this session"));
		} else {
			msg->check = gtk_check_button_new_with_mnemonic(type == E_PASSWORDS_REMEMBER_FOREVER
									? _("_Remember this password")
									: _("_Remember this password for the remainder of this session"));
			
		}
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (msg->check), *msg->remember);
		gtk_box_pack_start (GTK_BOX (vbox), msg->check, TRUE, FALSE, 3);
		if ((msg->flags & E_PASSWORDS_DISABLE_REMEMBER))
			gtk_widget_set_sensitive(msg->check, FALSE);
		gtk_widget_show (msg->check);
	}
	
	msg->noreply = noreply;

	g_signal_connect(password_dialog, "response", G_CALLBACK (pass_response), msg);
	gtk_widget_show((GtkWidget *)password_dialog);
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
	LOCK();

	if (!passwords) {
		/* create the per-session hash table */
		passwords = g_hash_table_new (g_str_hash, g_str_equal);
#ifdef ENABLE_THREADS
		main_thread = pthread_self();
#endif
	}

	UNLOCK();
}

/**
 * e_passwords_cancel:
 * 
 * Cancel any outstanding password operations and close any dialogues
 * currently being shown.
 **/
void
e_passwords_cancel(void)
{
	EPassMsg *msg;

	LOCK();
	while ((msg = (EPassMsg *)e_dlist_remhead(&request_list)))
		e_msgport_reply(&msg->msg);
	UNLOCK();

	if (password_dialog)
		gtk_dialog_response(password_dialog,GTK_RESPONSE_CANCEL);
}

/**
 * e_passwords_shutdown:
 *
 * Cleanup routine to call before exiting.
 **/
void
e_passwords_shutdown (void)
{
#ifdef WITH_GNOME_KEYRING
	/* shouldn't need this really - everything is synchronous */
	if (!gnome_keyring_is_available())
		gnome_config_private_sync_file ("/Evolution");
#else
	gnome_config_private_sync_file ("/Evolution");
#endif
	e_passwords_cancel();

	if (passwords) {
		/* and destroy our per session hash */
		g_hash_table_foreach_remove (passwords, free_entry, NULL);
		g_hash_table_destroy (passwords);
		passwords = NULL;
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
e_passwords_set_online(int state)
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
#ifdef WITH_GNOME_KEYRING	
	EPassMsg *msg = ep_msg_new(gnome_keyring_is_available() ? ep_forget_passwords_keyring : ep_forget_passwords_file);
#else
	EPassMsg *msg = ep_msg_new(ep_forget_passwords_file);
#endif
	
	ep_msg_send(msg);
	ep_msg_free(msg);
}

/**
 * e_passwords_clear_passwords:
 *
 * Forgets all disk cached passwords for the component.
 **/
void
e_passwords_clear_passwords (const char *component_name)
{
#ifdef WITH_GNOME_KEYRING	
	EPassMsg *msg = ep_msg_new(gnome_keyring_is_available() ? ep_clear_passwords_keyring : ep_clear_passwords_file);
#else
	EPassMsg *msg = ep_msg_new(ep_clear_passwords_file);		
#endif

	msg->component = component_name;
	ep_msg_send(msg);
	ep_msg_free(msg);
}

/**
 * e_passwords_remember_password:
 * @key: the key
 *
 * Saves the password associated with @key to disk.
 **/
void
e_passwords_remember_password (const char *component_name, const char *key)
{
	EPassMsg *msg;

	g_return_if_fail(component_name != NULL);
	g_return_if_fail(key != NULL);

#ifdef WITH_GNOME_KEYRING
	msg = ep_msg_new(gnome_keyring_is_available() ? ep_remember_password_keyring : ep_remember_password_file);
#else
	msg = ep_msg_new(ep_remember_password_file);
#endif	
	msg->component = component_name;
	msg->key = key;

	ep_msg_send(msg);
	ep_msg_free(msg);
}

/**
 * e_passwords_forget_password:
 * @key: the key
 *
 * Forgets the password associated with @key, in memory and on disk.
 **/
void
e_passwords_forget_password (const char *component_name, const char *key)
{
	EPassMsg *msg;

	g_return_if_fail(component_name != NULL);
	g_return_if_fail(key != NULL);

#ifdef WITH_GNOME_KEYRING	
	msg = ep_msg_new(gnome_keyring_is_available() ? ep_forget_password_keyring : ep_forget_password_file);
#else	
	msg = ep_msg_new(ep_forget_password_file);
#endif	
	msg->component = component_name;
	msg->key = key;

	ep_msg_send(msg);
	ep_msg_free(msg);
}

/**
 * e_passwords_get_password:
 * @key: the key
 *
 * Return value: the password associated with @key, or %NULL.  Caller
 * must free the returned password.
 **/
char *
e_passwords_get_password (const char *component_name, const char *key)
{
	EPassMsg *msg;
	char *passwd;

	g_return_val_if_fail(component_name != NULL, NULL);
	g_return_val_if_fail(key != NULL, NULL);

#ifdef WITH_GNOME_KEYRING	
	msg = ep_msg_new(gnome_keyring_is_available() ? ep_get_password_keyring : ep_get_password_file);
#else
	msg = ep_msg_new(ep_get_password_file);
#endif	

	msg->component = component_name;
	msg->key = key;

	ep_msg_send(msg);

	passwd = msg->password;
	msg->password = NULL;
	ep_msg_free(msg);

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
e_passwords_add_password (const char *key, const char *passwd)
{
	EPassMsg *msg;

	g_return_if_fail(key != NULL);
	g_return_if_fail(passwd != NULL);

	msg = ep_msg_new(ep_add_password);
	msg->key = key;
	msg->oldpass = passwd;

	ep_msg_send(msg);
	ep_msg_free(msg);
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
e_passwords_ask_password (const char *title, const char *component_name,
			  const char *key,
			  const char *prompt,
			  EPasswordsRememberType type,
			  gboolean *remember,
			  GtkWindow *parent)
{
	char *passwd;
	EPassMsg *msg = ep_msg_new(ep_ask_password);

	if ((type & E_PASSWORDS_ONLINE) && !ep_online_state)
		return NULL;

	msg->title = title;
	msg->component = component_name;
	msg->key = key;
	msg->prompt = prompt;
	msg->flags = type;
	msg->remember = remember;
	msg->parent = parent;

	ep_msg_send(msg);
	passwd = msg->password;
	msg->password = NULL;
	ep_msg_free(msg);
	
	return passwd;
}



static char *base64_alphabet =
"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static unsigned char camel_mime_base64_rank[256] = {
	255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
	255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
	255,255,255,255,255,255,255,255,255,255,255, 62,255,255,255, 63,
	 52, 53, 54, 55, 56, 57, 58, 59, 60, 61,255,255,255,  0,255,255,
	255,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14,
	 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25,255,255,255,255,255,
	255, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
	 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51,255,255,255,255,255,
	255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
	255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
	255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
	255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
	255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
	255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
	255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
	255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
};

/* call this when finished encoding everything, to
   flush off the last little bit */
static int
base64_encode_close(unsigned char *in, int inlen, gboolean break_lines, unsigned char *out, int *state, int *save)
{
	int c1, c2;
	unsigned char *outptr = out;

	if (inlen>0)
		outptr += base64_encode_step(in, inlen, break_lines, outptr, state, save);

	c1 = ((unsigned char *)save)[1];
	c2 = ((unsigned char *)save)[2];
	
	switch (((char *)save)[0]) {
	case 2:
		outptr[2] = base64_alphabet[ ( (c2 &0x0f) << 2 ) ];
		g_assert(outptr[2] != 0);
		goto skip;
	case 1:
		outptr[2] = '=';
	skip:
		outptr[0] = base64_alphabet[ c1 >> 2 ];
		outptr[1] = base64_alphabet[ c2 >> 4 | ( (c1&0x3) << 4 )];
		outptr[3] = '=';
		outptr += 4;
		break;
	}
	if (break_lines)
		*outptr++ = '\n';

	*save = 0;
	*state = 0;

	return outptr-out;
}

/*
  performs an 'encode step', only encodes blocks of 3 characters to the
  output at a time, saves left-over state in state and save (initialise to
  0 on first invocation).
*/
static int
base64_encode_step(unsigned char *in, int len, gboolean break_lines, unsigned char *out, int *state, int *save)
{
	register unsigned char *inptr, *outptr;

	if (len<=0)
		return 0;

	inptr = in;
	outptr = out;

	if (len + ((char *)save)[0] > 2) {
		unsigned char *inend = in+len-2;
		register int c1, c2, c3;
		register int already;

		already = *state;

		switch (((char *)save)[0]) {
		case 1:	c1 = ((unsigned char *)save)[1]; goto skip1;
		case 2:	c1 = ((unsigned char *)save)[1];
			c2 = ((unsigned char *)save)[2]; goto skip2;
		}
		
		/* yes, we jump into the loop, no i'm not going to change it, it's beautiful! */
		while (inptr < inend) {
			c1 = *inptr++;
		skip1:
			c2 = *inptr++;
		skip2:
			c3 = *inptr++;
			*outptr++ = base64_alphabet[ c1 >> 2 ];
			*outptr++ = base64_alphabet[ c2 >> 4 | ( (c1&0x3) << 4 ) ];
			*outptr++ = base64_alphabet[ ( (c2 &0x0f) << 2 ) | (c3 >> 6) ];
			*outptr++ = base64_alphabet[ c3 & 0x3f ];
			/* this is a bit ugly ... */
			if (break_lines && (++already)>=19) {
				*outptr++='\n';
				already = 0;
			}
		}

		((char *)save)[0] = 0;
		len = 2-(inptr-inend);
		*state = already;
	}

	if (len>0) {
		register char *saveout;

		/* points to the slot for the next char to save */
		saveout = & (((char *)save)[1]) + ((char *)save)[0];

		/* len can only be 0 1 or 2 */
		switch(len) {
		case 2:	*saveout++ = *inptr++;
		case 1:	*saveout++ = *inptr++;
		}
		((char *)save)[0]+=len;
	}

	return outptr-out;
}


/**
 * base64_decode_step: decode a chunk of base64 encoded data
 * @in: input stream
 * @len: max length of data to decode
 * @out: output stream
 * @state: holds the number of bits that are stored in @save
 * @save: leftover bits that have not yet been decoded
 *
 * Decodes a chunk of base64 encoded data
 **/
static int
base64_decode_step(unsigned char *in, int len, unsigned char *out, int *state, unsigned int *save)
{
	register unsigned char *inptr, *outptr;
	unsigned char *inend, c;
	register unsigned int v;
	int i;

	inend = in+len;
	outptr = out;

	/* convert 4 base64 bytes to 3 normal bytes */
	v=*save;
	i=*state;
	inptr = in;
	while (inptr<inend) {
		c = camel_mime_base64_rank[*inptr++];
		if (c != 0xff) {
			v = (v<<6) | c;
			i++;
			if (i==4) {
				*outptr++ = v>>16;
				*outptr++ = v>>8;
				*outptr++ = v;
				i=0;
			}
		}
	}

	*save = v;
	*state = i;

	/* quick scan back for '=' on the end somewhere */
	/* fortunately we can drop 1 output char for each trailing = (upto 2) */
	i=2;
	while (inptr>in && i) {
		inptr--;
		if (camel_mime_base64_rank[*inptr] != 0xff) {
			if (*inptr == '=')
				outptr--;
			i--;
		}
	}

	/* if i!= 0 then there is a truncation error! */
	return outptr-out;
}

static char *
decode_base64 (char *base64)
{
	guchar *plain;
	char *pad = "==";
	int len, out, state;
	unsigned int save;
	
	len = strlen (base64);
	plain = g_malloc0 (len);
	state = save = 0;
	out = base64_decode_step ((guchar *)base64, len, plain, &state, &save);
	if (len % 4) {
		base64_decode_step ((guchar *)pad, 4 - len % 4, plain + out,
				    &state, &save);
	}
	
	return (char *)plain;
}
