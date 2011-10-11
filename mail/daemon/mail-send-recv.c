/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with the program; if not, see <http://www.gnu.org/licenses/>
 *
 *
 * Authors:
 *		Michael Zucchi <NotZed@ximian.com>
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <string.h>

#include <glib/gi18n.h>

#include "libedataserver/e-account-list.h"

#include "libemail-utils/e-account-utils.h"
#include "libemail-utils/gconf-bridge.h"

#include "libemail-engine/e-mail-folder-utils.h"
#include "libemail-engine/e-mail-local.h"
#include "libemail-engine/e-mail-session.h"

#include "libemail-engine/e-mail-utils.h"
#include "libemail-engine/mail-folder-cache.h"
#include "libemail-utils/mail-mt.h"
#include "libemail-engine/mail-ops.h"
#include "libemail-engine/mail-tools.h"

#include "mail-send-recv.h"

#define d(x)

#define E_FILTER_SOURCE_INCOMING "incoming" /* performed on incoming email */
#define E_FILTER_SOURCE_OUTGOING  "outgoing"/* performed on outgoing mail */

/* ms between status updates to the gui */
#define STATUS_TIMEOUT (250)

/* pseudo-uri to key the send task on */
#define SEND_URI_KEY "send-task:"

/* Prefix for window size GConf keys */
#define GCONF_KEY_PREFIX "/apps/evolution/mail/send_recv"

/* send/receive email */

/* ********************************************************************** */
/*  This stuff below is independent of the stuff above */

/* this stuff is used to keep track of which folders filters have accessed, and
 * what not. the thaw/refreeze thing doesn't really seem to work though */
struct _folder_info {
	gchar *uri;
	CamelFolder *folder;
	time_t update;

	/* How many times updated, to slow it
	 * down as we go, if we have lots. */
	gint count;
};

struct _send_data {
	GList *infos;

	gint cancelled;

	/* Since we're never asked to update
	 * this one, do it ourselves. */
	CamelFolder *inbox;
	time_t inbox_update;

	GMutex *lock;
	GHashTable *folders;

	GHashTable *active;	/* send_info's by uri */
};

typedef enum {
	SEND_RECEIVE,		/* receiver */
	SEND_SEND,		/* sender */
	SEND_UPDATE,		/* imap-like 'just update folder info' */
	SEND_INVALID
} send_info_t;

typedef enum {
	SEND_ACTIVE,
	SEND_CANCELLED,
	SEND_COMPLETE
} send_state_t;

struct _send_info {
	send_info_t type;		/* 0 = fetch, 1 = send */
	EMailSession *session;
	GCancellable *cancellable;
	gchar *service_uid;
	gboolean keep_on_server;
	send_state_t state;

	gint again;		/* need to run send again */

	gint timeout_id;
	gchar *what;
	gint pc;

	gchar *send_url;

	/*time_t update;*/
	struct _send_data *data;
};

static CamelFolder *
		receive_get_folder		(CamelFilterDriver *d,
						 const gchar *uri,
						 gpointer data,
						 GError **error);

static struct _send_data *send_data = NULL;

static void
free_folder_info (struct _folder_info *info)
{
	mail_sync_folder (info->folder, NULL, NULL);
	g_object_unref (info->folder);
	g_free (info->uri);
	g_free (info);
}

static void
free_send_info (struct _send_info *info)
{
	if (info->session)
		g_object_unref (info->session);
	if (info->cancellable)
		g_object_unref (info->cancellable);
	g_free (info->service_uid);
	if (info->timeout_id != 0)
		g_source_remove (info->timeout_id);
	g_free (info->what);
	g_free (info->send_url);
	g_free (info);
}

static struct _send_data *
setup_send_data (void)
{
	struct _send_data *data;

	if (send_data == NULL) {
		send_data = data = g_malloc0 (sizeof (*data));
		data->lock = g_mutex_new ();
		data->folders = g_hash_table_new_full (
			g_str_hash, g_str_equal,
			(GDestroyNotify) NULL,
			(GDestroyNotify) free_folder_info);
		data->inbox = e_mail_local_get_folder (
			E_MAIL_LOCAL_FOLDER_LOCAL_INBOX);
		g_object_ref (data->inbox);
		data->active = g_hash_table_new_full (
			g_str_hash, g_str_equal,
			(GDestroyNotify) NULL,
			(GDestroyNotify) free_send_info);
	}
	return send_data;
}

static void
receive_cancel (struct _send_info *info)
{
	if (info->state == SEND_ACTIVE) {
		camel_operation_cancel (CAMEL_OPERATION (info->cancellable));
		info->state = SEND_CANCELLED;
	}
}

static void
free_send_data (void)
{
	struct _send_data *data = send_data;

	g_return_if_fail (g_hash_table_size (data->active) == 0);

	if (data->inbox) {
		mail_sync_folder (data->inbox, NULL, NULL);
		/*camel_folder_thaw (data->inbox);		*/
		g_object_unref (data->inbox);
	}

	g_list_free (data->infos);
	g_hash_table_destroy (data->active);
	g_hash_table_destroy (data->folders);
	g_mutex_free (data->lock);
	g_free (data);
	send_data = NULL;
}

#if 0
static void
cancel_send_info (gpointer key,
                  struct _send_info *info,
                  gpointer data)
{
	receive_cancel (info);
}

static void
hide_send_info (gpointer key,
                struct _send_info *info,
                gpointer data)
{
	if (info->timeout_id != 0) {
		g_source_remove (info->timeout_id);
		info->timeout_id = 0;
	}
}
#endif

static GStaticMutex status_lock = G_STATIC_MUTEX_INIT;
static gchar *format_url (EAccount *account, const gchar *internal_url);

static void
set_send_status (struct _send_info *info,
                 const gchar *desc,
                 gint pc)
{
	g_static_mutex_lock (&status_lock);

	g_free (info->what);
	info->what = g_strdup (desc);
	info->pc = pc;

	g_static_mutex_unlock (&status_lock);
}

static void
set_send_account (struct _send_info *info,
                  const gchar *account_url)
{
	g_static_mutex_lock (&status_lock);

	g_free (info->send_url);
	info->send_url = g_strdup (account_url);

	g_static_mutex_unlock (&status_lock);
}

/* for camel operation status */
static void
operation_status (CamelOperation *op,
                  const gchar *what,
                  gint pc,
                  struct _send_info *info)
{
	set_send_status (info, what, pc);
}

static gchar *
format_url (EAccount *account,
            const gchar *internal_url)
{
	CamelURL *url;
	gchar *pretty_url = NULL;

	url = camel_url_new (internal_url, NULL);

	if (account != NULL && account->name != NULL) {
		if (url->host && *url->host)
			pretty_url = g_strdup_printf (
				"<b>%s (%s)</b>: %s",
				account->name, url->protocol, url->host);
		else if (url->path)
			pretty_url = g_strdup_printf (
				"<b>%s (%s)</b>: %s",
				account->name, url->protocol, url->path);
		else
			pretty_url = g_strdup_printf (
				"<b>%s (%s)</b>",
				account->name, url->protocol);

	} else if (url) {
		if (url->host && *url->host)
			pretty_url = g_strdup_printf (
				"<b>%s</b>: %s",
				url->protocol, url->host);
		else if (url->path)
			pretty_url = g_strdup_printf (
				"<b>%s</b>: %s",
				url->protocol, url->path);
		else
			pretty_url = g_strdup_printf (
				"<b>%s</b>", url->protocol);
	}

	if (url)
		camel_url_free (url);

	return pretty_url;
}

static send_info_t
get_receive_type (CamelURL *url)
{
	CamelProvider *provider;

	/* mbox pointing to a file is a 'Local delivery' source
	 * which requires special processing */
	if (em_utils_is_local_delivery_mbox_file (url))
		return SEND_RECEIVE;

	provider = camel_provider_get (url->protocol, NULL);

	if (!provider)
		return SEND_INVALID;

	if (provider->object_types[CAMEL_PROVIDER_STORE]) {
		if (provider->flags & CAMEL_PROVIDER_IS_STORAGE)
			return SEND_UPDATE;
		else
			return SEND_RECEIVE;
	} else if (provider->object_types[CAMEL_PROVIDER_TRANSPORT]) {
		return SEND_SEND;
	}

	return SEND_INVALID;
}

static gint
operation_status_timeout (gpointer data)
{
	//struct _send_info *info = data;

	return FALSE;
}


static struct _send_data *
build_infra (EMailSession *session,
              EAccountList *accounts,
              CamelFolder *outbox,
              EAccount *outgoing_account,
              gboolean allow_send)
{
	gint row, num_sources;
	GList *list = NULL;
	struct _send_data *data;
	struct _send_info *info;
	EAccount *account;
	EIterator *iter;

	num_sources = 0;

	iter = e_list_get_iterator ((EList *) accounts);
	while (e_iterator_is_valid (iter)) {
		account = (EAccount *) e_iterator_get (iter);

		if (account->source->url)
			num_sources++;

		e_iterator_next (iter);
	}

	g_object_unref (iter);

	/* Check to see if we have to send any mails.
	 * If we don't, don't display the SMTP row in the table. */
	if (outbox && outgoing_account
	 && (camel_folder_get_message_count (outbox) -
		camel_folder_get_deleted_message_count (outbox)) == 0)
		num_sources--;

	data = setup_send_data ();

	row = 0;
	iter = e_list_get_iterator ((EList *) accounts);
	while (e_iterator_is_valid (iter)) {
		EAccountService *source;

		account = (EAccount *) e_iterator_get (iter);

		source = account->source;
		if (!account->enabled || !source->url) {
			e_iterator_next (iter);
			continue;
		}

		/* see if we have an outstanding download active */
		info = g_hash_table_lookup (data->active, account->uid);
		if (info == NULL) {
			CamelURL *url;
			send_info_t type = SEND_INVALID;

			url = camel_url_new (source->url, NULL);
			if (url != NULL) {
				type = get_receive_type (url);
				camel_url_free (url);
			}

			if (type == SEND_INVALID || type == SEND_SEND) {
				e_iterator_next (iter);
				continue;
			}

			info = g_malloc0 (sizeof (*info));
			info->type = type;
			info->session = g_object_ref (session);

			d(printf("adding source %s\n", source->url));

			info->service_uid = g_strdup (account->uid);
			info->keep_on_server = source->keep_on_server;
			info->cancellable = camel_operation_new ();
			info->state = allow_send ? SEND_ACTIVE : SEND_COMPLETE;
			info->timeout_id = g_timeout_add (
				STATUS_TIMEOUT, operation_status_timeout, info);

			g_signal_connect (
				info->cancellable, "status",
				G_CALLBACK (operation_status), info);

			g_hash_table_insert (
				data->active, info->service_uid, info);
			list = g_list_prepend (list, info);
		} else if (info->timeout_id == 0)
			info->timeout_id = g_timeout_add (
				STATUS_TIMEOUT, operation_status_timeout, info);

		info->data = data;

		e_iterator_next (iter);
		row = row + 2;
	}

	g_object_unref (iter);

	/* Skip displaying the SMTP row if we've got no outbox,
	 * outgoing account or unsent mails. */
	if (allow_send && outbox && outgoing_account
	 && (camel_folder_get_message_count (outbox) -
		camel_folder_get_deleted_message_count (outbox)) != 0) {
		info = g_hash_table_lookup (data->active, SEND_URI_KEY);
		if (info == NULL) {
			gchar *transport_uid;

			transport_uid = g_strconcat (
				outgoing_account->uid, "-transport", NULL);

			info = g_malloc0 (sizeof (*info));
			info->type = SEND_SEND;

			info->service_uid = g_strdup (transport_uid);
			info->keep_on_server = FALSE;
			info->cancellable = camel_operation_new ();
			info->state = SEND_ACTIVE;
			info->timeout_id = g_timeout_add (
				STATUS_TIMEOUT, operation_status_timeout, info);

			g_free (transport_uid);

			g_signal_connect (
				info->cancellable, "status",
				G_CALLBACK (operation_status), info);

			g_hash_table_insert (data->active, (gpointer) SEND_URI_KEY, info);
			list = g_list_prepend (list, info);
		} else if (info->timeout_id == 0)
			info->timeout_id = g_timeout_add (
				STATUS_TIMEOUT, operation_status_timeout, info);

		info->data = data;

	}

	data->infos = list;

	return data;
}

static void
update_folders (gchar *uri,
                struct _folder_info *info,
                gpointer data)
{
	time_t now = *((time_t *) data);

	d(printf("checking update for folder: %s\n", info->uri));

	/* let it flow through to the folders every 10 seconds */
	/* we back off slowly as we progress */
	if (now > info->update + 10 + info->count *5) {
		d(printf("upating a folder: %s\n", info->uri));
		/*camel_folder_thaw(info->folder);
		  camel_folder_freeze (info->folder);*/
		info->update = now;
		info->count++;
	}
}

static void
receive_status (CamelFilterDriver *driver,
                enum camel_filter_status_t status,
                gint pc,
                const gchar *desc,
                gpointer data)
{
	struct _send_info *info = data;
	time_t now = time (NULL);

	/* let it flow through to the folder, every now and then too? */
	g_hash_table_foreach (info->data->folders, (GHFunc) update_folders, &now);

	if (info->data->inbox && now > info->data->inbox_update + 20) {
		d(printf("updating inbox too\n"));
		/* this doesn't seem to work right :( */
		/*camel_folder_thaw(info->data->inbox);
		  camel_folder_freeze (info->data->inbox);*/
		info->data->inbox_update = now;
	}

	/* we just pile them onto the port, assuming it can handle it.
	 * We could also have a receiver port and see if they've been processed
	 * yet, so if this is necessary its not too hard to add */
	/* the mail_gui_port receiver will free everything for us */
	switch (status) {
	case CAMEL_FILTER_STATUS_START:
	case CAMEL_FILTER_STATUS_END:
		set_send_status (info, desc, pc);
		break;
	case CAMEL_FILTER_STATUS_ACTION:
		set_send_account (info, desc);
		break;
	default:
		break;
	}
}

/* when receive/send is complete */
static void
receive_done (gpointer data)
{
	struct _send_info *info = data;

	/* if we've been called to run again - run again */
	if (info->type == SEND_SEND && info->state == SEND_ACTIVE && info->again) {
		EMailSession *session;
		CamelFolder *local_outbox;
		CamelService *service;

		session = info->session;

		local_outbox = e_mail_local_get_folder (
			E_MAIL_LOCAL_FOLDER_OUTBOX);

		service = camel_session_get_service (
			CAMEL_SESSION (session),
			info->service_uid);

		g_return_if_fail (CAMEL_IS_TRANSPORT (service));

		info->again = 0;
		mail_send_queue (
			info->session,
			local_outbox,
			CAMEL_TRANSPORT (service),
			E_FILTER_SOURCE_OUTGOING,
			info->cancellable,
			receive_get_folder, info,
			receive_status, info,
			receive_done, info);
		return;
	}

	//FIXME Set SEND completed here
	/*	if (info->state == SEND_CANCELLED)
			text = _("Canceled.");
		else {
			text = _("Complete.");
			info->state = SEND_COMPLETE;
		}
	*/

	/* remove/free this active download */
	d(printf("%s: freeing info %p\n", G_STRFUNC, info));
	if (info->type == SEND_SEND)
		g_hash_table_steal (info->data->active, SEND_URI_KEY);
	else
		g_hash_table_steal (info->data->active, info->service_uid);
	info->data->infos = g_list_remove (info->data->infos, info);

	if (g_hash_table_size (info->data->active) == 0) {
		//FIXME: THIS MEANS SEND RECEIVE IS COMPLETED
		free_send_data ();
	}

	free_send_info (info);
}

/* although we dont do anythign smart here yet, there is no need for this interface to
 * be available to anyone else.
 * This can also be used to hook into which folders are being updated, and occasionally
 * let them refresh */
static CamelFolder *
receive_get_folder (CamelFilterDriver *d,
                    const gchar *uri,
                    gpointer data,
                    GError **error)
{
	struct _send_info *info = data;
	CamelFolder *folder;
	EMailSession *session;
	struct _folder_info *oldinfo;
	gpointer oldkey, oldinfoptr;

	g_mutex_lock (info->data->lock);
	oldinfo = g_hash_table_lookup (info->data->folders, uri);
	g_mutex_unlock (info->data->lock);

	if (oldinfo) {
		g_object_ref (oldinfo->folder);
		return oldinfo->folder;
	}

	session = info->session;

	/* FIXME Not passing a GCancellable here. */
	folder = e_mail_session_uri_to_folder_sync (
		session, uri, 0, NULL, error);
	if (!folder)
		return NULL;

	/* we recheck that the folder hasn't snuck in while we were loading it... */
	/* and we assume the newer one is the same, but unref the old one anyway */
	g_mutex_lock (info->data->lock);

	if (g_hash_table_lookup_extended (
			info->data->folders, uri, &oldkey, &oldinfoptr)) {
		oldinfo = (struct _folder_info *) oldinfoptr;
		g_object_unref (oldinfo->folder);
		oldinfo->folder = folder;
	} else {
		oldinfo = g_malloc0 (sizeof (*oldinfo));
		oldinfo->folder = folder;
		oldinfo->uri = g_strdup (uri);
		g_hash_table_insert (info->data->folders, oldinfo->uri, oldinfo);
	}

	g_object_ref (folder);

	g_mutex_unlock (info->data->lock);

	return folder;
}

/* ********************************************************************** */

static void
get_folders (CamelStore *store,
             GPtrArray *folders,
             CamelFolderInfo *info)
{
	while (info) {
		if (camel_store_can_refresh_folder (store, info, NULL)) {
			if ((info->flags & CAMEL_FOLDER_NOSELECT) == 0) {
				gchar *folder_uri;

				folder_uri = e_mail_folder_uri_build (
					store, info->full_name);
				g_ptr_array_add (folders, folder_uri);
			}
		}

		get_folders (store, folders, info->child);
		info = info->next;
	}
}

static void
main_op_cancelled_cb (GCancellable *main_op,
                      GCancellable *refresh_op)
{
	g_cancellable_cancel (refresh_op);
}

struct _refresh_folders_msg {
	MailMsg base;

	struct _send_info *info;
	GPtrArray *folders;
	CamelStore *store;
	CamelFolderInfo *finfo;
};

static gchar *
refresh_folders_desc (struct _refresh_folders_msg *m)
{
	return g_strdup_printf(_("Checking for new mail"));
}

static void
refresh_folders_exec (struct _refresh_folders_msg *m,
                      GCancellable *cancellable,
                      GError **error)
{
	CamelFolder *folder;
	EMailSession *session;
	gint i;
	GError *local_error = NULL;
	gulong handler_id = 0;

	if (cancellable)
		handler_id = g_signal_connect (
			m->info->cancellable, "cancelled",
			G_CALLBACK (main_op_cancelled_cb), cancellable);

	get_folders (m->store, m->folders, m->finfo);

	camel_operation_push_message (m->info->cancellable, _("Updating..."));

	session = m->info->session;

	for (i = 0; i < m->folders->len; i++) {
		folder = e_mail_session_uri_to_folder_sync (
			session,
			m->folders->pdata[i], 0,
			cancellable, &local_error);
		if (folder) {
			/* FIXME Not passing a GError here. */
			camel_folder_synchronize_sync (
				folder, FALSE, cancellable, NULL);
			camel_folder_refresh_info_sync (folder, cancellable, NULL);
			g_object_unref (folder);
		} else if (local_error != NULL) {
			g_warning ("Failed to refresh folders: %s", local_error->message);
			g_clear_error (&local_error);
		}

		if (g_cancellable_is_cancelled (m->info->cancellable))
			break;

		if (m->info->state != SEND_CANCELLED)
			camel_operation_progress (
				m->info->cancellable, 100 * i / m->folders->len);
	}

	camel_operation_pop_message (m->info->cancellable);

	if (cancellable)
		g_signal_handler_disconnect (m->info->cancellable, handler_id);
}

static void
refresh_folders_done (struct _refresh_folders_msg *m)
{
	receive_done (m->info);
}

static void
refresh_folders_free (struct _refresh_folders_msg *m)
{
	gint i;

	for (i = 0; i < m->folders->len; i++)
		g_free (m->folders->pdata[i]);
	g_ptr_array_free (m->folders, TRUE);

	camel_store_free_folder_info (m->store, m->finfo);
	g_object_unref (m->store);
}

static MailMsgInfo refresh_folders_info = {
	sizeof (struct _refresh_folders_msg),
	(MailMsgDescFunc) refresh_folders_desc,
	(MailMsgExecFunc) refresh_folders_exec,
	(MailMsgDoneFunc) refresh_folders_done,
	(MailMsgFreeFunc) refresh_folders_free
};

static gboolean
receive_update_got_folderinfo (MailFolderCache *folder_cache,
                               CamelStore *store,
                               CamelFolderInfo *info,
                               gpointer data)
{
	if (info) {
		GPtrArray *folders = g_ptr_array_new ();
		struct _refresh_folders_msg *m;
		struct _send_info *sinfo = data;

		m = mail_msg_new (&refresh_folders_info);
		m->store = store;
		g_object_ref (store);
		m->folders = folders;
		m->info = sinfo;
		m->finfo = info;

		mail_msg_unordered_push (m);

		/* do not free folder info, we will free it later */
		return FALSE;
	} else {
		receive_done (data);
	}

	return TRUE;
}

static void
receive_update_got_store (CamelStore *store,
                          struct _send_info *info)
{
	EMailSession *session;
	MailFolderCache *folder_cache;

	session = info->session;
	folder_cache = e_mail_session_get_folder_cache (session);

	if (store) {
		mail_folder_cache_note_store (
			folder_cache,
			CAMEL_SESSION (session),
			store, info->cancellable,
			receive_update_got_folderinfo, info);
	} else {
		receive_done (info);
	}
}

static void
send_receive (EMailSession *session,
              gboolean allow_send)
{
	CamelFolder *local_outbox;
	struct _send_data *data;
	EAccountList *accounts;
	EAccount *account;
	GList *scan;
	
	if (send_data) /* Send Receive is already in progress */
		return;

	if (!camel_session_get_online (CAMEL_SESSION (session)))
		return;

	account = e_get_default_account ();
	if (!account || !account->transport->url)
		return;

	accounts = e_get_account_list ();

	local_outbox = e_mail_local_get_folder (E_MAIL_LOCAL_FOLDER_OUTBOX);
	data = build_infra (
		session, accounts,
		local_outbox, account, allow_send);

	for (scan = data->infos; scan != NULL; scan = scan->next) {
		struct _send_info *info = scan->data;
		CamelService *service;

		service = camel_session_get_service (
			CAMEL_SESSION (session), info->service_uid);

		if (!CAMEL_IS_SERVICE (service))
			continue;

		switch (info->type) {
		case SEND_RECEIVE:
			mail_fetch_mail (
				CAMEL_STORE (service),
				info->keep_on_server,
				E_FILTER_SOURCE_INCOMING,
				info->cancellable,
				receive_get_folder, info,
				receive_status, info,
				receive_done, info);
			break;
		case SEND_SEND:
			/* todo, store the folder in info? */
			mail_send_queue (
				session, local_outbox,
				CAMEL_TRANSPORT (service),
				E_FILTER_SOURCE_OUTGOING,
				info->cancellable,
				receive_get_folder, info,
				receive_status, info,
				receive_done, info);
			break;
		case SEND_UPDATE:
			receive_update_got_store (
				CAMEL_STORE (service), info);
			break;
		default:
			break;
		}
	}

	return ;
}

void
mail_send_receive (EMailSession *session)
{
	return send_receive (session, TRUE);
}

void
mail_receive (EMailSession *session)
{
	return send_receive (session, FALSE);
}

struct _auto_data {
	EAccount *account;
	EMailSession *session;
	gint period;		/* in seconds */
	gint timeout_id;
};

static GHashTable *auto_active;

static gboolean
auto_timeout (gpointer data)
{
	EMailSession *session;
	struct _auto_data *info = data;

	session = info->session;

	if (camel_session_get_online (CAMEL_SESSION (session)))
		mail_receive_account (info->session, info->account);

	return TRUE;
}

static void
auto_account_removed (EAccountList *eal,
                      EAccount *ea,
                      gpointer dummy)
{
	struct _auto_data *info = g_object_get_data((GObject *)ea, "mail-autoreceive");

	g_return_if_fail (info != NULL);

	if (info->timeout_id) {
		g_source_remove (info->timeout_id);
		info->timeout_id = 0;
	}
}

static void
auto_account_finalized (struct _auto_data *info)
{
	if (info->session != NULL)
		g_object_unref (info->session);
	if (info->timeout_id)
		g_source_remove (info->timeout_id);
	g_free (info);
}

static void
auto_account_commit (struct _auto_data *info)
{
	gint period, check;

	check = info->account->enabled
		&& e_account_get_bool (info->account, E_ACCOUNT_SOURCE_AUTO_CHECK)
		&& e_account_get_string (info->account, E_ACCOUNT_SOURCE_URL);
	period = e_account_get_int (info->account, E_ACCOUNT_SOURCE_AUTO_CHECK_TIME) * 60;
	period = MAX (60, period);

	if (info->timeout_id
	    && (!check
		|| period != info->period)) {
		g_source_remove (info->timeout_id);
		info->timeout_id = 0;
	}
	info->period = period;
	if (check && info->timeout_id == 0)
		info->timeout_id = g_timeout_add_seconds (info->period, auto_timeout, info);
}

static void
auto_account_added (EAccountList *eal,
                    EAccount *ea,
                    EMailSession *session)
{
	struct _auto_data *info;

	info = g_malloc0 (sizeof (*info));
	info->account = ea;
	info->session = g_object_ref (session);
	g_object_set_data_full (
		G_OBJECT (ea), "mail-autoreceive", info,
		(GDestroyNotify) auto_account_finalized);
	auto_account_commit (info);
}

static void
auto_account_changed (EAccountList *eal,
                      EAccount *ea,
                      gpointer dummy)
{
	struct _auto_data *info = g_object_get_data((GObject *)ea, "mail-autoreceive");

	g_return_if_fail (info != NULL);

	auto_account_commit (info);
}

static void
auto_online (EMailSession *session)
{
	EIterator *iter;
	EAccountList *accounts;
	struct _auto_data *info;
	gboolean can_update_all;

	accounts = e_get_account_list ();
	for (iter = e_list_get_iterator ((EList *) accounts);
	     e_iterator_is_valid (iter);
	     e_iterator_next (iter)) {
		EAccount *account = (EAccount *) e_iterator_get (iter);

		if (!account || !account->enabled)
			continue;

		info = g_object_get_data (
			G_OBJECT (account), "mail-autoreceive");
		if (info && (info->timeout_id || can_update_all))
			auto_timeout (info);
	}

	if (iter)
		g_object_unref (iter);
}

/* call to setup initial, and after changes are made to the config */
/* FIXME: Need a cleanup funciton for when object is deactivated */
void
mail_autoreceive_init (EMailSession *session)
{
	EAccountList *accounts;
	EIterator *iter;

	if (auto_active)
		return;

	accounts = e_get_account_list ();
	auto_active = g_hash_table_new (g_str_hash, g_str_equal);

	g_signal_connect (
		accounts, "account-added",
		G_CALLBACK (auto_account_added), session);
	g_signal_connect (
		accounts, "account-removed",
		G_CALLBACK (auto_account_removed), NULL);
	g_signal_connect (
		accounts, "account-changed",
		G_CALLBACK (auto_account_changed), NULL);

	for (iter = e_list_get_iterator ((EList *) accounts);
	     e_iterator_is_valid (iter);
	     e_iterator_next (iter))
		auto_account_added (
			accounts, (EAccount *)
			e_iterator_get (iter), session);

	if (1) {
		auto_online (session);

		/* also flush outbox on start */
		mail_send (session);
	}

	/* FIXME: Check for online status and sync after online */
}

/* We setup the download info's in a hashtable, if we later
 * need to build the gui, we insert them in to add them. */
void
mail_receive_account (EMailSession *session,
                      EAccount *account)
{
	struct _send_info *info;
	struct _send_data *data;
	CamelFolder *local_outbox;
	CamelService *service;
	CamelURL *url;
	send_info_t type = SEND_INVALID;

	data = setup_send_data ();
	info = g_hash_table_lookup (data->active, account->uid);

	if (info != NULL)
		return;

	url = camel_url_new (account->source->url, NULL);
	if (url != NULL) {
		type = get_receive_type (url);
		camel_url_free (url);
	}

	if (type == SEND_INVALID || type == SEND_SEND)
		return;

	info = g_malloc0 (sizeof (*info));
	info->type = type;
	info->session = g_object_ref (session);
	info->service_uid = g_strdup (account->uid);
	info->keep_on_server = account->source->keep_on_server;
	info->cancellable = camel_operation_new ();
	info->data = data;
	info->state = SEND_ACTIVE;
	info->timeout_id = 0;

	g_signal_connect (
		info->cancellable, "status",
		G_CALLBACK (operation_status), info);

	d(printf("Adding new info %p\n", info));

	g_hash_table_insert (data->active, account->uid, info);

	service = camel_session_get_service (
		CAMEL_SESSION (session), account->uid);

	g_return_if_fail (CAMEL_IS_SERVICE (service));

	switch (info->type) {
	case SEND_RECEIVE:
		mail_fetch_mail (
			CAMEL_STORE (service),
			info->keep_on_server,
			E_FILTER_SOURCE_INCOMING,
			info->cancellable,
			receive_get_folder, info,
			receive_status, info,
			receive_done, info);
		break;
	case SEND_SEND:
		/* todo, store the folder in info? */
		local_outbox = e_mail_local_get_folder (
			E_MAIL_LOCAL_FOLDER_OUTBOX);
		mail_send_queue (
			info->session,
			local_outbox,
			CAMEL_TRANSPORT (service),
			E_FILTER_SOURCE_OUTGOING,
			info->cancellable,
			receive_get_folder, info,
			receive_status, info,
			receive_done, info);
		break;
	case SEND_UPDATE:
		receive_update_got_store (CAMEL_STORE (service), info);
		break;
	default:
		g_return_if_reached ();
	}
}

void
mail_send (EMailSession *session)
{
	CamelFolder *local_outbox;
	CamelService *service;
	EAccount *account;
	CamelURL *url;
	struct _send_info *info;
	struct _send_data *data;
	send_info_t type = SEND_INVALID;
	gchar *transport_uid;

	account = e_get_default_transport ();
	if (account == NULL || account->transport->url == NULL)
		return;

	data = setup_send_data ();
	info = g_hash_table_lookup (data->active, SEND_URI_KEY);
	if (info != NULL) {
		info->again++;
		d(printf("send of %s still in progress\n", transport->url));
		return;
	}

	d(printf("starting non-interactive send of '%s'\n", transport->url));

	url = camel_url_new (account->transport->url, NULL);
	if (url != NULL) {
		type = get_receive_type (url);
		camel_url_free (url);
	}

	if (type == SEND_INVALID)
		return;

	transport_uid = g_strconcat (account->uid, "-transport", NULL);

	info = g_malloc0 (sizeof (*info));
	info->type = SEND_SEND;
	info->session = g_object_ref (session);
	info->service_uid = g_strdup (transport_uid);
	info->keep_on_server = FALSE;
	info->cancellable = NULL;
	info->data = data;
	info->state = SEND_ACTIVE;
	info->timeout_id = 0;

	d(printf("Adding new info %p\n", info));

	g_hash_table_insert (data->active, (gpointer) SEND_URI_KEY, info);

	/* todo, store the folder in info? */
	local_outbox = e_mail_local_get_folder (E_MAIL_LOCAL_FOLDER_OUTBOX);

	service = camel_session_get_service (
		CAMEL_SESSION (session), transport_uid);

	g_free (transport_uid);

	g_return_if_fail (CAMEL_IS_TRANSPORT (service));

	mail_send_queue (
		session, local_outbox,
		CAMEL_TRANSPORT (service),
		E_FILTER_SOURCE_OUTGOING,
		info->cancellable,
		receive_get_folder, info,
		receive_status, info,
		receive_done, info);
}
