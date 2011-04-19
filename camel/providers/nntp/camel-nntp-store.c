/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * Authors: Christopher Toshok <toshok@ximian.com>
 *	    Michael Zucchi <notzed@ximian.com>
 *
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
 * USA
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>

#include <glib/gi18n-lib.h>

#include "camel-nntp-summary.h"
#include "camel-nntp-store.h"
#include "camel-nntp-store-summary.h"
#include "camel-nntp-folder.h"
#include "camel-nntp-private.h"
#include "camel-nntp-resp-codes.h"

#ifdef G_OS_WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

#define w(x)
#define dd(x) (camel_debug("nntp")?(x):0)

#define NNTP_PORT  119
#define NNTPS_PORT 563

#define DUMP_EXTENSIONS

#define CAMEL_NNTP_STORE_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), CAMEL_TYPE_NNTP_STORE, CamelNNTPStorePrivate))

static gint camel_nntp_try_authenticate (CamelNNTPStore *store, GError **error);

G_DEFINE_TYPE (CamelNNTPStore, camel_nntp_store, CAMEL_TYPE_DISCO_STORE)

static void
nntp_store_dispose (GObject *object)
{
	CamelNNTPStore *nntp_store = CAMEL_NNTP_STORE (object);
	CamelDiscoStore *disco_store = CAMEL_DISCO_STORE (object);

	/* Only run this the first time. */
	if (nntp_store->summary != NULL)
		camel_service_disconnect (CAMEL_SERVICE (object), TRUE, NULL);

	if (nntp_store->summary != NULL) {
		camel_store_summary_save (
			CAMEL_STORE_SUMMARY (nntp_store->summary));
		g_object_unref (nntp_store->summary);
		nntp_store->summary = NULL;
	}

	if (nntp_store->mem != NULL) {
		g_object_unref (nntp_store->mem);
		nntp_store->mem = NULL;
	}

	if (nntp_store->stream != NULL) {
		g_object_unref (nntp_store->stream);
		nntp_store->stream = NULL;
	}

	if (nntp_store->cache != NULL) {
		g_object_unref (nntp_store->cache);
		nntp_store->cache = NULL;
	}

	if (disco_store->diary != NULL) {
		g_object_unref (disco_store->diary);
		disco_store->diary = NULL;
	}

	/* Chain up to parent's dispose() method. */
	G_OBJECT_CLASS (camel_nntp_store_parent_class)->dispose (object);
}

static void
nntp_store_finalize (GObject *object)
{
	CamelNNTPStore *nntp_store = CAMEL_NNTP_STORE (object);
	struct _xover_header *xover, *xn;

	g_free (nntp_store->base_url);
	g_free (nntp_store->storage_path);

	xover = nntp_store->xover;
	while (xover) {
		xn = xover->next;
		g_free(xover);
		xover = xn;
	}

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (camel_nntp_store_parent_class)->finalize (object);
}

static gboolean
nntp_can_work_offline(CamelDiscoStore *store)
{
	return TRUE;
}

static struct {
	const gchar *name;
	gint type;
} headers[] = {
	{ "subject", 0 },
	{ "from", 0 },
	{ "date", 0 },
	{ "message-id", 1 },
	{ "references", 0 },
	{ "bytes", 2 },
};

static gint
xover_setup(CamelNNTPStore *store, GError **error)
{
	gint ret, i;
	gchar *line;
	guint len;
	guchar c, *p;
	struct _xover_header *xover, *last;

	/* manual override */
	if (store->xover || getenv("CAMEL_NNTP_DISABLE_XOVER") != NULL)
		return 0;

	ret = camel_nntp_raw_command_auth(store, error, &line, "list overview.fmt");
	if (ret == -1) {
		return -1;
	} else if (ret != 215)
		/* unsupported command?  ignore */
		return 0;

	last = (struct _xover_header *)&store->xover;

	/* supported command */
	while ((ret = camel_nntp_stream_line(store->stream, (guchar **)&line, &len, error)) > 0) {
		p = (guchar *) line;
		xover = g_malloc0(sizeof(*xover));
		last->next = xover;
		last = xover;
		while ((c = *p++)) {
			if (c == ':') {
				p[-1] = 0;
				for (i = 0; i < G_N_ELEMENTS (headers); i++) {
					if (strcmp(line, headers[i].name) == 0) {
						xover->name = headers[i].name;
						if (strncmp((gchar *) p, "full", 4) == 0)
							xover->skip = strlen(xover->name)+1;
						else
							xover->skip = 0;
						xover->type = headers[i].type;
						break;
					}
				}
				break;
			} else {
				p[-1] = camel_tolower(c);
			}
		}
	}

	return ret;
}

enum {
	MODE_CLEAR,
	MODE_SSL,
	MODE_TLS
};

#ifdef CAMEL_HAVE_SSL
#define SSL_PORT_FLAGS (CAMEL_TCP_STREAM_SSL_ENABLE_SSL2 | CAMEL_TCP_STREAM_SSL_ENABLE_SSL3)
#define STARTTLS_FLAGS (CAMEL_TCP_STREAM_SSL_ENABLE_TLS)
#endif

static gboolean
connect_to_server (CamelService *service, const gchar *host, const gchar *serv, gint fallback_port, gint ssl_mode, GError **error)
{
	CamelNNTPStore *store = (CamelNNTPStore *) service;
	CamelDiscoStore *disco_store = (CamelDiscoStore*) service;
	CamelSession *session;
	gchar *socks_host;
	gint socks_port;
	CamelStream *tcp_stream;
	gboolean retval = FALSE;
	guchar *buf;
	guint len;
	gchar *path;

	camel_service_lock (CAMEL_SERVICE (store), CAMEL_SERVICE_REC_CONNECT_LOCK);

	if (ssl_mode != MODE_CLEAR) {
#ifdef CAMEL_HAVE_SSL
		if (ssl_mode == MODE_TLS) {
			tcp_stream = camel_tcp_stream_ssl_new_raw (service->session, service->url->host, STARTTLS_FLAGS);
		} else {
			tcp_stream = camel_tcp_stream_ssl_new (service->session, service->url->host, SSL_PORT_FLAGS);
		}
#else
		g_set_error (
			error, CAMEL_SERVICE_ERROR,
			CAMEL_SERVICE_ERROR_UNAVAILABLE,
			_("Could not connect to %s: %s"),
			service->url->host, _("SSL unavailable"));
		goto fail;
#endif /* CAMEL_HAVE_SSL */
	} else {
		tcp_stream = camel_tcp_stream_raw_new ();
	}

	session = camel_service_get_session (service);
	camel_session_get_socks_proxy (session, &socks_host, &socks_port);

	if (socks_host) {
		camel_tcp_stream_set_socks_proxy ((CamelTcpStream *) tcp_stream, socks_host, socks_port);
		g_free (socks_host);
	}

	if (camel_tcp_stream_connect ((CamelTcpStream *) tcp_stream, host, serv, fallback_port, error) == -1) {
		g_prefix_error (
			error, _("Could not connect to %s: "),
			service->url->host);
		g_object_unref (tcp_stream);

		goto fail;
	}

	store->stream = (CamelNNTPStream *) camel_nntp_stream_new (tcp_stream);
	g_object_unref (tcp_stream);

	/* Read the greeting, if any. */
	if (camel_nntp_stream_line (store->stream, &buf, &len, error) == -1) {
		g_prefix_error (
			error, _("Could not read greeting from %s: "),
			service->url->host);

		g_object_unref (store->stream);
		store->stream = NULL;

		goto fail;
	}

	len = strtoul ((gchar *) buf, (gchar **) &buf, 10);
	if (len != 200 && len != 201) {
		g_set_error (
			error, CAMEL_ERROR, CAMEL_ERROR_GENERIC,
			_("NNTP server %s returned error code %d: %s"),
			service->url->host, len, buf);

		g_object_unref (store->stream);
		store->stream = NULL;

		goto fail;
	}

	/* if we have username, try it here */
	if (service->url->user != NULL
	    && service->url->user[0]
	    && camel_nntp_try_authenticate(store, error) != NNTP_AUTH_ACCEPTED)
		goto fail;

	/* set 'reader' mode & ignore return code, also ping the server, inn goes offline very quickly otherwise */
	if (camel_nntp_raw_command_auth (store, error, (gchar **) &buf, "mode reader") == -1
	    || camel_nntp_raw_command_auth (store, error, (gchar **) &buf, "date") == -1)
		goto fail;

	if (xover_setup(store, error) == -1)
		goto fail;

	if (!disco_store->diary) {
		path = g_build_filename (store->storage_path, ".ev-journal", NULL);
		disco_store->diary = camel_disco_diary_new (disco_store, path, error);
		g_free (path);
	}

	retval = TRUE;

	g_free(store->current_folder);
	store->current_folder = NULL;

 fail:
	camel_service_unlock (CAMEL_SERVICE (store), CAMEL_SERVICE_REC_CONNECT_LOCK);
	return retval;
}

static struct {
	const gchar *value;
	const gchar *serv;
	gint fallback_port;
	gint mode;
} ssl_options[] = {
	{ "",              "nntps", NNTPS_PORT, MODE_SSL   },  /* really old (1.x) */
	{ "always",        "nntps", NNTPS_PORT, MODE_SSL   },
	{ "when-possible", "nntp",  NNTP_PORT, MODE_TLS   },
	{ "never",         "nntp",  NNTP_PORT, MODE_CLEAR },
	{ NULL,            "nntp",  NNTP_PORT, MODE_CLEAR },
};

static gboolean
nntp_connect_online (CamelService *service, GError **error)
{
	const gchar *ssl_mode;
	gint mode, i;
	gchar *serv;
	gint fallback_port;

	if ((ssl_mode = camel_url_get_param (service->url, "use_ssl"))) {
		for (i = 0; ssl_options[i].value; i++)
			if (!strcmp (ssl_options[i].value, ssl_mode))
				break;
		mode = ssl_options[i].mode;
		serv = (gchar *) ssl_options[i].serv;
		fallback_port = ssl_options[i].fallback_port;
	} else {
		mode = MODE_CLEAR;
		serv = (gchar *) "nntp";
		fallback_port = NNTP_PORT;
	}

	if (service->url->port) {
		serv = g_alloca (16);
		sprintf (serv, "%d", service->url->port);
		fallback_port = 0;
	}

	return connect_to_server (service, service->url->host, serv, fallback_port, mode, error);
}

static gboolean
nntp_connect_offline (CamelService *service, GError **error)
{
	CamelNNTPStore *nntp_store = CAMEL_NNTP_STORE(service);
	CamelDiscoStore *disco_store = (CamelDiscoStore *) nntp_store;
	gchar *path;

	if (nntp_store->storage_path == NULL)
		return FALSE;

	/* setup store-wide cache */
	if (nntp_store->cache == NULL) {
		nntp_store->cache = camel_data_cache_new (nntp_store->storage_path, error);
		if (nntp_store->cache == NULL)
			return FALSE;

		/* Default cache expiry - 2 weeks old, or not visited in 5 days */
		camel_data_cache_set_expire_age (nntp_store->cache, 60*60*24*14);
		camel_data_cache_set_expire_access (nntp_store->cache, 60*60*24*5);
	}

	if (disco_store->diary)
		return TRUE;

	path = g_build_filename (nntp_store->storage_path, ".ev-journal", NULL);
	disco_store->diary = camel_disco_diary_new (disco_store, path, error);
	g_free (path);

	if (!disco_store->diary)
		return FALSE;

	return TRUE;
}

static gboolean
nntp_disconnect_online (CamelService *service, gboolean clean, GError **error)
{
	CamelNNTPStore *store = CAMEL_NNTP_STORE (service);
	gchar *line;

	camel_service_lock (CAMEL_SERVICE (store), CAMEL_SERVICE_REC_CONNECT_LOCK);

	if (clean)
		camel_nntp_raw_command (store, NULL, &line, "quit");

	g_object_unref (store->stream);
	store->stream = NULL;
	g_free(store->current_folder);
	store->current_folder = NULL;

	camel_service_unlock (CAMEL_SERVICE (store), CAMEL_SERVICE_REC_CONNECT_LOCK);

	return TRUE;
}

static gboolean
nntp_disconnect_offline (CamelService *service, gboolean clean, GError **error)
{
	CamelDiscoStore *disco = CAMEL_DISCO_STORE(service);

	if (disco->diary) {
		g_object_unref (disco->diary);
		disco->diary = NULL;
	}

	return TRUE;
}

static gchar *
nntp_store_get_name (CamelService *service, gboolean brief)
{
	if (brief)
		return g_strdup_printf ("%s", service->url->host);
	else
		return g_strdup_printf (_("USENET News via %s"), service->url->host);

}

extern CamelServiceAuthType camel_nntp_password_authtype;

static GList *
nntp_store_query_auth_types (CamelService *service, GError **error)
{
	return g_list_append (NULL, &camel_nntp_password_authtype);
}

static CamelFolder *
nntp_get_folder(CamelStore *store, const gchar *folder_name, guint32 flags, GError **error)
{
	CamelNNTPStore *nntp_store = CAMEL_NNTP_STORE (store);
	CamelFolder *folder;

	camel_service_lock (CAMEL_SERVICE (nntp_store), CAMEL_SERVICE_REC_CONNECT_LOCK);

	folder = camel_nntp_folder_new(store, folder_name, error);

	camel_service_unlock (CAMEL_SERVICE (nntp_store), CAMEL_SERVICE_REC_CONNECT_LOCK);

	return folder;
}

/*
 * Converts a fully-fledged newsgroup name to a name in short dotted notation,
 * e.g. nl.comp.os.linux.programmeren becomes n.c.o.l.programmeren
 */

static gchar *
nntp_newsgroup_name_short (const gchar *name)
{
	gchar *resptr, *tmp;
	const gchar *ptr2;

	resptr = tmp = g_malloc0 (strlen (name) + 1);

	while ((ptr2 = strchr (name, '.'))) {
		if (ptr2 == name) {
			name++;
			continue;
		}

		*resptr++ = *name;
		*resptr++ = '.';
		name = ptr2 + 1;
	}

	strcpy (resptr, name);
	return tmp;
}

/*
 * This function converts a NNTPStoreSummary item to a FolderInfo item that
 * can be returned by the get_folders() call to the store. Both structs have
 * essentially the same fields.
 */

static CamelFolderInfo *
nntp_folder_info_from_store_info (CamelNNTPStore *store, gboolean short_notation, CamelStoreInfo *si)
{
	CamelURL *base_url = ((CamelService *) store)->url;
	CamelFolderInfo *fi;
	CamelURL *url;
	gchar *path;

	fi = camel_folder_info_new ();
	fi->full_name = g_strdup (si->path);

	if (short_notation)
		fi->name = nntp_newsgroup_name_short (si->path);
	else
		fi->name = g_strdup (si->path);

	fi->unread = si->unread;
	fi->total = si->total;
	fi->flags = si->flags;
	path = alloca(strlen(fi->full_name)+2);
	sprintf(path, "/%s", fi->full_name);
	url = camel_url_new_with_base (base_url, path);
	fi->uri = camel_url_to_string (url, CAMEL_URL_HIDE_ALL);
	camel_url_free (url);

	return fi;
}

static CamelFolderInfo *
nntp_folder_info_from_name (CamelNNTPStore *store, gboolean short_notation, const gchar *name)
{
	CamelURL *base_url = ((CamelService *)store)->url;
	CamelFolderInfo *fi;
	CamelURL *url;
	gchar *path;

	fi = camel_folder_info_new ();
	fi->full_name = g_strdup (name);

	if (short_notation)
		fi->name = nntp_newsgroup_name_short (name);
	else
		fi->name = g_strdup (name);

	fi->unread = -1;

	path = alloca(strlen(fi->full_name)+2);
	sprintf(path, "/%s", fi->full_name);
	url = camel_url_new_with_base (base_url, path);
	fi->uri = camel_url_to_string (url, CAMEL_URL_HIDE_ALL);
	camel_url_free (url);

	return fi;
}

/* handle list/newgroups response */
static CamelNNTPStoreInfo *
nntp_store_info_update(CamelNNTPStore *store, gchar *line)
{
	CamelStoreSummary *summ = (CamelStoreSummary *)store->summary;
	CamelURL *base_url = ((CamelService *)store)->url;
	CamelNNTPStoreInfo *si, *fsi;
	CamelURL *url;
	gchar *relpath, *tmp;
	guint32 last = 0, first = 0, new = 0;

	tmp = strchr(line, ' ');
	if (tmp)
		*tmp++ = 0;

	fsi = si = (CamelNNTPStoreInfo *)camel_store_summary_path((CamelStoreSummary *)store->summary, line);
	if (si == NULL) {
		si = (CamelNNTPStoreInfo*)camel_store_summary_info_new(summ);

		relpath = g_alloca(strlen(line)+2);
		sprintf(relpath, "/%s", line);
		url = camel_url_new_with_base (base_url, relpath);
		si->info.uri = camel_url_to_string (url, CAMEL_URL_HIDE_ALL);
		camel_url_free (url);

		si->info.path = g_strdup (line);
		si->full_name = g_strdup (line); /* why do we keep this? */
		camel_store_summary_add((CamelStoreSummary *)store->summary, &si->info);
	} else {
		first = si->first;
		last = si->last;
	}

	if (tmp && *tmp >= '0' && *tmp <= '9') {
		last = strtoul(tmp, &tmp, 10);
		if (*tmp == ' ' && tmp[1] >= '0' && tmp[1] <= '9') {
			first = strtoul(tmp+1, &tmp, 10);
			if (*tmp == ' ' && tmp[1] != 'y')
				si->info.flags |= CAMEL_STORE_INFO_FOLDER_READONLY;
		}
	}

	dd(printf("store info update '%s' first '%u' last '%u'\n", line, first, last));

	if (si->last) {
		if (last > si->last)
			new = last-si->last;
	} else {
		if (last > first)
			new = last - first;
	}

	si->info.total = last > first?last-first:0;
	si->info.unread += new;	/* this is a _guess_ */
	si->last = last;
	si->first = first;

	if (fsi)
		camel_store_summary_info_free((CamelStoreSummary *)store->summary, &fsi->info);
	else			/* TODO see if we really did touch it */
		camel_store_summary_touch ((CamelStoreSummary *)store->summary);

	return si;
}

static CamelFolderInfo *
nntp_store_get_subscribed_folder_info (CamelNNTPStore *store, const gchar *top, guint flags, GError **error)
{
	gint i;
	CamelStoreInfo *si;
	CamelFolderInfo *first = NULL, *last = NULL, *fi = NULL;

	/* since we do not do a tree, any request that is not for root is sure to give no results */
	if (top != NULL && top[0] != 0)
		return NULL;

	for (i=0;(si = camel_store_summary_index ((CamelStoreSummary *) store->summary, i));i++) {
		if (si == NULL)
			continue;

		if (si->flags & CAMEL_STORE_INFO_FOLDER_SUBSCRIBED) {
			/* slow mode?  open and update the folder, always! this will implictly update
			   our storeinfo too; in a very round-about way */
			if ((flags & CAMEL_STORE_FOLDER_INFO_FAST) == 0) {
				CamelNNTPFolder *folder;
				gchar *line;

				folder = (CamelNNTPFolder *)camel_store_get_folder((CamelStore *)store, si->path, 0, NULL);
				if (folder) {
					CamelFolderChangeInfo *changes = NULL;

					camel_service_lock (CAMEL_SERVICE (store), CAMEL_SERVICE_REC_CONNECT_LOCK);
					camel_nntp_command(store, NULL, folder, &line, NULL);
					if (camel_folder_change_info_changed(folder->changes)) {
						changes = folder->changes;
						folder->changes = camel_folder_change_info_new();
					}
					camel_service_unlock (CAMEL_SERVICE (store), CAMEL_SERVICE_REC_CONNECT_LOCK);
					if (changes) {
						camel_folder_changed (CAMEL_FOLDER (folder), changes);
						camel_folder_change_info_free(changes);
					}
					g_object_unref (folder);
				}
			}
			fi = nntp_folder_info_from_store_info (store, store->do_short_folder_notation, si);
			fi->flags |= CAMEL_FOLDER_NOINFERIORS | CAMEL_FOLDER_NOCHILDREN | CAMEL_FOLDER_SYSTEM;
			if (last)
				last->next = fi;
			else
				first = fi;
			last = fi;
		}
		camel_store_summary_info_free ((CamelStoreSummary *) store->summary, si);
	}

	return first;
}

static CamelFolderInfo *
tree_insert(CamelFolderInfo *root, CamelFolderInfo *last, CamelFolderInfo *fi)
{
	CamelFolderInfo *kfi;

	if (!root)
		root = fi;
	else if (!last) {
		kfi = root;
		while (kfi->next)
			kfi = kfi->next;
		kfi->next = fi;
		fi->parent = kfi->parent;
	} else {
		if (!last->child) {
			last->child = fi;
			fi->parent = last;
		} else {
			kfi = last->child;
			while (kfi->next)
				kfi = kfi->next;
			kfi->next = fi;
			fi->parent = last;
		}
	}
	return root;
}
/* returns new root */
static CamelFolderInfo *
nntp_push_to_hierarchy (CamelURL *base_url, CamelFolderInfo *root, CamelFolderInfo *pfi, GHashTable *known)
{
	CamelFolderInfo *fi, *last = NULL, *kfi;
	gchar *name, *dot;

	g_return_val_if_fail (pfi != NULL, root);
	g_return_val_if_fail (known != NULL, root);

	name = pfi->full_name;
	g_return_val_if_fail (name != NULL, root);

	while (dot = strchr (name, '.'), dot) {
		*dot = '\0';

		kfi = g_hash_table_lookup (known, pfi->full_name);
		if (!kfi) {
			gchar *path;
			CamelURL *url;

			fi = camel_folder_info_new ();
			fi->full_name = g_strdup (pfi->full_name);
			fi->name = g_strdup (name);

			fi->unread = 0;
			fi->total = 0;
			fi->flags = CAMEL_FOLDER_NOSELECT | CAMEL_FOLDER_CHILDREN;
			path = alloca (strlen (fi->full_name) + 2);
			sprintf (path, "/%s", fi->full_name);
			url = camel_url_new_with_base (base_url, path);
			fi->uri = camel_url_to_string (url, CAMEL_URL_HIDE_ALL);
			camel_url_free (url);

			g_hash_table_insert (known, fi->full_name, fi);
			root = tree_insert(root, last, fi);
			last = fi;
		} else {
			last = kfi;
		}

		*dot = '.';
		name = dot + 1;
	}

	g_free (pfi->name);
	pfi->name = g_strdup (name);

	return tree_insert(root, last, pfi);
}

/*
 * get folder info, using the information in our StoreSummary
 */
static CamelFolderInfo *
nntp_store_get_cached_folder_info (CamelNNTPStore *store, const gchar *orig_top, guint flags, GError **error)
{
	gint i;
	gint subscribed_or_flag = (flags & CAMEL_STORE_FOLDER_INFO_SUBSCRIBED) ? 0 : 1,
	    root_or_flag = (orig_top == NULL || orig_top[0] == '\0') ? 1 : 0,
	    recursive_flag = flags & CAMEL_STORE_FOLDER_INFO_RECURSIVE,
	    is_folder_list = flags & CAMEL_STORE_FOLDER_INFO_SUBSCRIPTION_LIST;
	CamelStoreInfo *si;
	CamelFolderInfo *first = NULL, *last = NULL, *fi = NULL;
	GHashTable *known; /* folder name to folder info */
	gchar *tmpname;
	gchar *top = g_strconcat(orig_top?orig_top:"", ".", NULL);
	gint toplen = strlen(top);

	known = g_hash_table_new (g_str_hash, g_str_equal);

	for (i = 0; (si = camel_store_summary_index ((CamelStoreSummary *) store->summary, i)); i++) {
		if ((subscribed_or_flag || (si->flags & CAMEL_STORE_INFO_FOLDER_SUBSCRIBED))
		    && (root_or_flag || strncmp (si->path, top, toplen) == 0)) {
			if (recursive_flag || is_folder_list || strchr (si->path + toplen, '.') == NULL) {
				/* add the item */
				fi = nntp_folder_info_from_store_info(store, FALSE, si);
				if (!fi)
					continue;
				if (store->folder_hierarchy_relative) {
					g_free (fi->name);
					fi->name = g_strdup (si->path + ((toplen == 1) ? 0 : toplen));
				}
			} else {
				/* apparently, this is an indirect subitem. if it's not a subitem of
				   the item we added last, we need to add a portion of this item to
				   the list as a placeholder */
				if (!last ||
				    strncmp(si->path, last->full_name, strlen(last->full_name)) != 0 ||
				    si->path[strlen(last->full_name)] != '.') {
					tmpname = g_strdup(si->path);
					*(strchr(tmpname + toplen, '.')) = '\0';
					fi = nntp_folder_info_from_name(store, FALSE, tmpname);
					if (!fi)
						continue;

					fi->flags |= CAMEL_FOLDER_NOSELECT;
					if (store->folder_hierarchy_relative) {
						g_free(fi->name);
						fi->name = g_strdup(tmpname + ((toplen==1) ? 0 : toplen));
					}
					g_free(tmpname);
				} else {
					continue;
				}
			}

			if (fi->full_name && g_hash_table_lookup (known, fi->full_name)) {
				/* a duplicate has been found above */
				camel_folder_info_free (fi);
				continue;
			}

			g_hash_table_insert (known, fi->full_name, fi);

			if (is_folder_list) {
				/* create a folder hierarchy rather than a flat list */
				first = nntp_push_to_hierarchy (((CamelService *)store)->url, first, fi, known);
			} else {
				if (last)
					last->next = fi;
				else
					first = fi;
				last = fi;
			}
		} else if (subscribed_or_flag && first) {
			/* we have already added subitems, but this item is no longer a subitem */
			camel_store_summary_info_free((CamelStoreSummary *)store->summary, si);
			break;
		}
		camel_store_summary_info_free((CamelStoreSummary *)store->summary, si);
	}

	g_hash_table_destroy (known);
	g_free(top);
	return first;
}

/* retrieves the date from the NNTP server */
static gboolean
nntp_get_date(CamelNNTPStore *nntp_store, GError **error)
{
	guchar *line;
	gint ret = camel_nntp_command(nntp_store, error, NULL, (gchar **)&line, "date");
	gchar *ptr;

	nntp_store->summary->last_newslist[0] = 0;

	if (ret == 111) {
		ptr = (gchar *) line + 3;
		while (*ptr == ' ' || *ptr == '\t')
			ptr++;

		if (strlen (ptr) == NNTP_DATE_SIZE) {
			memcpy (nntp_store->summary->last_newslist, ptr, NNTP_DATE_SIZE);
			return TRUE;
		}
	}
	return FALSE;
}

static void
store_info_remove(gpointer key, gpointer value, gpointer data)
{
	CamelStoreSummary *summary = data;
	CamelStoreInfo *si = value;

	camel_store_summary_remove(summary, si);
}

static gint
store_info_sort (gconstpointer a, gconstpointer b)
{
	return strcmp ((*(CamelNNTPStoreInfo**) a)->full_name, (*(CamelNNTPStoreInfo**) b)->full_name);
}

static CamelFolderInfo *
nntp_store_get_folder_info_all(CamelNNTPStore *nntp_store, const gchar *top, guint32 flags, gboolean online, GError **error)
{
	CamelNNTPStoreSummary *summary = nntp_store->summary;
	CamelNNTPStoreInfo *si;
	guint len;
	guchar *line;
	gint ret = -1;
	CamelFolderInfo *fi = NULL;

	camel_service_lock (CAMEL_SERVICE (nntp_store), CAMEL_SERVICE_REC_CONNECT_LOCK);

	if (top == NULL)
		top = "";

	if (online && (top == NULL || top[0] == 0)) {
		/* we may need to update */
		if (summary->last_newslist[0] != 0) {
			gchar date[14];
			memcpy(date, summary->last_newslist + 2, 6); /* YYMMDDD */
			date[6] = ' ';
			memcpy(date + 7, summary->last_newslist + 8, 6); /* HHMMSS */
			date[13] = '\0';

			/* Some servers don't support date (!), so fallback if they dont */
			if (!nntp_get_date (nntp_store, NULL))
				goto do_complete_list_nodate;

			ret = camel_nntp_command (nntp_store, error, NULL, (gchar **) &line, "newgroups %s", date);
			if (ret == -1)
				goto error;
			else if (ret != 231) {
				/* newgroups not supported :S so reload the complete list */
				summary->last_newslist[0] = 0;
				goto do_complete_list;
			}

			while ((ret = camel_nntp_stream_line (nntp_store->stream, &line, &len, error)) > 0)
				nntp_store_info_update(nntp_store, (gchar *) line);
		} else {
			GHashTable *all;
			gint i;

		do_complete_list:
			/* seems we do need a complete list */
			/* at first, we do a DATE to find out the last load occasion */
			nntp_get_date (nntp_store, NULL);
		do_complete_list_nodate:
			ret = camel_nntp_command (nntp_store, error, NULL, (gchar **)&line, "list");
			if (ret == -1)
				goto error;
			else if (ret != 215) {
				g_set_error (
					error, CAMEL_SERVICE_ERROR,
					CAMEL_SERVICE_ERROR_INVALID,
					_("Error retrieving newsgroups:\n\n%s"), line);
				goto error;
			}

			all = g_hash_table_new(g_str_hash, g_str_equal);
			for (i = 0; (si = (CamelNNTPStoreInfo *)camel_store_summary_index ((CamelStoreSummary *)nntp_store->summary, i)); i++)
				g_hash_table_insert(all, si->info.path, si);

			while ((ret = camel_nntp_stream_line(nntp_store->stream, &line, &len, error)) > 0) {
				si = nntp_store_info_update(nntp_store, (gchar *) line);
				g_hash_table_remove(all, si->info.path);
			}

			g_hash_table_foreach(all, store_info_remove, nntp_store->summary);
			g_hash_table_destroy(all);
		}

		/* sort the list */
		g_ptr_array_sort (CAMEL_STORE_SUMMARY (nntp_store->summary)->folders, store_info_sort);
		if (ret < 0)
			goto error;

		camel_store_summary_save ((CamelStoreSummary *) nntp_store->summary);
	}

	fi = nntp_store_get_cached_folder_info (nntp_store, top, flags, error);
 error:
	camel_service_unlock (CAMEL_SERVICE (nntp_store), CAMEL_SERVICE_REC_CONNECT_LOCK);

	return fi;
}

static CamelFolderInfo *
nntp_get_folder_info (CamelStore *store, const gchar *top, guint32 flags, gboolean online, GError **error)
{
	CamelNNTPStore *nntp_store = CAMEL_NNTP_STORE(store);
	CamelFolderInfo *first = NULL;

	dd(printf("g_f_i: fast %d subscr %d recursive %d online %d top \"%s\"\n",
		flags & CAMEL_STORE_FOLDER_INFO_FAST,
		flags & CAMEL_STORE_FOLDER_INFO_SUBSCRIBED,
		flags & CAMEL_STORE_FOLDER_INFO_RECURSIVE,
		online,
		top?top:""));

	if (flags & CAMEL_STORE_FOLDER_INFO_SUBSCRIBED)
		first = nntp_store_get_subscribed_folder_info (nntp_store, top, flags, error);
	else
		first = nntp_store_get_folder_info_all (nntp_store, top, flags, online, error);

	return first;
}

static CamelFolderInfo *
nntp_get_folder_info_online (CamelStore *store, const gchar *top, guint32 flags, GError **error)
{
	return nntp_get_folder_info (store, top, flags, TRUE, error);
}

static CamelFolderInfo *
nntp_get_folder_info_offline(CamelStore *store, const gchar *top, guint32 flags, GError **error)
{
	return nntp_get_folder_info (store, top, flags, FALSE, error);
}

static gboolean
nntp_store_folder_is_subscribed (CamelStore *store, const gchar *folder_name)
{
	CamelNNTPStore *nntp_store = CAMEL_NNTP_STORE (store);
	CamelStoreInfo *si;
	gint truth = FALSE;

	si = camel_store_summary_path ((CamelStoreSummary *) nntp_store->summary, folder_name);
	if (si) {
		truth = (si->flags & CAMEL_STORE_INFO_FOLDER_SUBSCRIBED) != 0;
		camel_store_summary_info_free ((CamelStoreSummary *) nntp_store->summary, si);
	}

	return truth;
}

static gboolean
nntp_store_subscribe_folder (CamelStore *store, const gchar *folder_name,
			     GError **error)
{
	CamelNNTPStore *nntp_store = CAMEL_NNTP_STORE(store);
	CamelStoreInfo *si;
	CamelFolderInfo *fi;
	gboolean success = TRUE;

	camel_service_lock (CAMEL_SERVICE (nntp_store), CAMEL_SERVICE_REC_CONNECT_LOCK);

	si = camel_store_summary_path(CAMEL_STORE_SUMMARY(nntp_store->summary), folder_name);
	if (!si) {
		g_set_error (
			error, CAMEL_FOLDER_ERROR,
			CAMEL_FOLDER_ERROR_INVALID,
			_("You cannot subscribe to this newsgroup:\n\n"
			  "No such newsgroup. The selected item is a "
			  "probably a parent folder."));
		success = FALSE;
	} else {
		if (!(si->flags & CAMEL_STORE_INFO_FOLDER_SUBSCRIBED)) {
			si->flags |= CAMEL_STORE_INFO_FOLDER_SUBSCRIBED;
			fi = nntp_folder_info_from_store_info(nntp_store, nntp_store->do_short_folder_notation, si);
			fi->flags |= CAMEL_FOLDER_NOINFERIORS | CAMEL_FOLDER_NOCHILDREN;
			camel_store_summary_touch ((CamelStoreSummary *) nntp_store->summary);
			camel_store_summary_save ((CamelStoreSummary *) nntp_store->summary);
			camel_service_unlock (CAMEL_SERVICE (nntp_store), CAMEL_SERVICE_REC_CONNECT_LOCK);
			camel_store_folder_subscribed (CAMEL_STORE (nntp_store), fi);
			camel_folder_info_free (fi);
			return TRUE;
		}
	}

	camel_service_unlock (CAMEL_SERVICE (nntp_store), CAMEL_SERVICE_REC_CONNECT_LOCK);

	return success;
}

static gboolean
nntp_store_unsubscribe_folder (CamelStore *store, const gchar *folder_name,
			       GError **error)
{
	CamelNNTPStore *nntp_store = CAMEL_NNTP_STORE(store);
	CamelFolderInfo *fi;
	CamelStoreInfo *fitem;
	gboolean success = TRUE;

	camel_service_lock (CAMEL_SERVICE (nntp_store), CAMEL_SERVICE_REC_CONNECT_LOCK);

	fitem = camel_store_summary_path(CAMEL_STORE_SUMMARY(nntp_store->summary), folder_name);

	if (!fitem) {
		g_set_error (
			error, CAMEL_FOLDER_ERROR,
			CAMEL_FOLDER_ERROR_INVALID,
			_("You cannot unsubscribe to this newsgroup:\n\n"
			  "newsgroup does not exist!"));
		success = FALSE;
	} else {
		if (fitem->flags & CAMEL_STORE_INFO_FOLDER_SUBSCRIBED) {
			fitem->flags &= ~CAMEL_STORE_INFO_FOLDER_SUBSCRIBED;
			fi = nntp_folder_info_from_store_info (nntp_store, nntp_store->do_short_folder_notation, fitem);
			camel_store_summary_touch ((CamelStoreSummary *) nntp_store->summary);
			camel_store_summary_save ((CamelStoreSummary *) nntp_store->summary);
			camel_service_unlock (CAMEL_SERVICE (nntp_store), CAMEL_SERVICE_REC_CONNECT_LOCK);
			camel_store_folder_unsubscribed (CAMEL_STORE (nntp_store), fi);
			camel_folder_info_free (fi);
			return TRUE;
		}
	}

	camel_service_unlock (CAMEL_SERVICE (nntp_store), CAMEL_SERVICE_REC_CONNECT_LOCK);

	return success;
}

/* stubs for various folder operations we're not implementing */

static CamelFolderInfo *
nntp_create_folder (CamelStore *store, const gchar *parent_name,
                    const gchar *folder_name, GError **error)
{
	g_set_error (
		error, CAMEL_FOLDER_ERROR,
		CAMEL_FOLDER_ERROR_INVALID,
		_("You cannot create a folder in a News store: "
		  "subscribe instead."));

	return NULL;
}

static gboolean
nntp_rename_folder (CamelStore *store, const gchar *old_name, const gchar *new_name_in, GError **error)
{
	g_set_error (
		error, CAMEL_FOLDER_ERROR,
		CAMEL_FOLDER_ERROR_INVALID,
		_("You cannot rename a folder in a News store."));

	return FALSE;
}

static gboolean
nntp_delete_folder (CamelStore *store, const gchar *folder_name, GError **error)
{
	nntp_store_unsubscribe_folder (store, folder_name, NULL);

	g_set_error (
		error, CAMEL_FOLDER_ERROR,
		CAMEL_FOLDER_ERROR_INVALID,
		_("You cannot remove a folder in a News store: "
		  "unsubscribe instead."));

	return FALSE;
}

static gboolean
nntp_can_refresh_folder (CamelStore *store, CamelFolderInfo *info, GError **error)
{
	/* any nntp folder can be refreshed */
	return TRUE;
}

/* construction function in which we set some basic store properties */
static gboolean
nntp_construct (CamelService *service, CamelSession *session,
		CamelProvider *provider, CamelURL *url,
		GError **error)
{
	CamelServiceClass *service_class;
	CamelNNTPStore *nntp_store = CAMEL_NNTP_STORE(service);
	CamelURL *summary_url;
	gchar *tmp;

	/* construct the parent first */
	service_class = CAMEL_SERVICE_CLASS (camel_nntp_store_parent_class);
	if (!service_class->construct (service, session, provider, url, error))
		return FALSE;

	/* find out the storage path, base url */
	nntp_store->storage_path = camel_session_get_storage_path (session, service, error);
	if (!nntp_store->storage_path)
		return FALSE;

	/* FIXME */
	nntp_store->base_url = camel_url_to_string (service->url, (CAMEL_URL_HIDE_PASSWORD |
								   CAMEL_URL_HIDE_PARAMS |
								   CAMEL_URL_HIDE_AUTH));

	tmp = g_build_filename (nntp_store->storage_path, ".ev-store-summary", NULL);
	nntp_store->summary = camel_nntp_store_summary_new ();
	camel_store_summary_set_filename ((CamelStoreSummary *) nntp_store->summary, tmp);
	summary_url = camel_url_new (nntp_store->base_url, NULL);
	camel_store_summary_set_uri_base ((CamelStoreSummary *) nntp_store->summary, summary_url);
	g_free (tmp);

	camel_url_free (summary_url);
	camel_store_summary_load ((CamelStoreSummary *)nntp_store->summary);

	/* get options */
	if (camel_url_get_param (url, "show_short_notation"))
		nntp_store->do_short_folder_notation = TRUE;
	else
		nntp_store->do_short_folder_notation = FALSE;
	if (camel_url_get_param (url, "folder_hierarchy_relative"))
		nntp_store->folder_hierarchy_relative = TRUE;
	else
		nntp_store->folder_hierarchy_relative = FALSE;

	/* setup store-wide cache */
	nntp_store->cache = camel_data_cache_new(nntp_store->storage_path, error);
	if (nntp_store->cache == NULL)
		return FALSE;

	/* Default cache expiry - 2 weeks old, or not visited in 5 days */
	camel_data_cache_set_expire_age(nntp_store->cache, 60*60*24*14);
	camel_data_cache_set_expire_access(nntp_store->cache, 60*60*24*5);

	return TRUE;
}

static void
camel_nntp_store_class_init (CamelNNTPStoreClass *class)
{
	GObjectClass *object_class;
	CamelServiceClass *service_class;
	CamelStoreClass *store_class;
	CamelDiscoStoreClass *disco_store_class;

	g_type_class_add_private (class, sizeof (CamelNNTPStorePrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->dispose = nntp_store_dispose;
	object_class->finalize = nntp_store_finalize;

	service_class = CAMEL_SERVICE_CLASS (class);
	service_class->construct = nntp_construct;
	service_class->query_auth_types = nntp_store_query_auth_types;
	service_class->get_name = nntp_store_get_name;

	store_class = CAMEL_STORE_CLASS (class);
	store_class->free_folder_info = camel_store_free_folder_info_full;
	store_class->folder_is_subscribed = nntp_store_folder_is_subscribed;
	store_class->subscribe_folder = nntp_store_subscribe_folder;
	store_class->unsubscribe_folder = nntp_store_unsubscribe_folder;
	store_class->create_folder = nntp_create_folder;
	store_class->delete_folder = nntp_delete_folder;
	store_class->rename_folder = nntp_rename_folder;
	store_class->can_refresh_folder = nntp_can_refresh_folder;

	disco_store_class = CAMEL_DISCO_STORE_CLASS (class);
	disco_store_class->can_work_offline = nntp_can_work_offline;
	disco_store_class->connect_online = nntp_connect_online;
	disco_store_class->connect_offline = nntp_connect_offline;
	disco_store_class->disconnect_online = nntp_disconnect_online;
	disco_store_class->disconnect_offline = nntp_disconnect_offline;
	disco_store_class->get_folder_online = nntp_get_folder;
	disco_store_class->get_folder_resyncing = nntp_get_folder;
	disco_store_class->get_folder_offline = nntp_get_folder;
	disco_store_class->get_folder_info_online = nntp_get_folder_info_online;
	disco_store_class->get_folder_info_resyncing = nntp_get_folder_info_online;
	disco_store_class->get_folder_info_offline = nntp_get_folder_info_offline;
}

static void
camel_nntp_store_init (CamelNNTPStore *nntp_store)
{
	CamelStore *store = CAMEL_STORE (nntp_store);

	store->flags = CAMEL_STORE_SUBSCRIPTIONS;

	nntp_store->mem = (CamelStreamMem *)camel_stream_mem_new();

	nntp_store->priv = CAMEL_NNTP_STORE_GET_PRIVATE (nntp_store);
}

static gint
camel_nntp_try_authenticate (CamelNNTPStore *store, GError **error)
{
	CamelService *service = (CamelService *) store;
	CamelSession *session = camel_service_get_session (service);
	gint ret;
	gchar *line = NULL;
	GError *local_error = NULL;

	if (!service->url->user) {
		g_set_error (
			error, CAMEL_ERROR, CAMEL_ERROR_GENERIC,
			_("Authentication requested but no username provided"));
		return -1;
	}

	/* if nessecary, prompt for the password */
	if (!service->url->passwd) {
		gchar *prompt, *base;
	retry:
		base = camel_session_build_password_prompt (
			"NNTP", service->url->user, service->url->host);
		if (line) {
			gchar *top = g_markup_printf_escaped (
				_("Cannot authenticate to server: %s"), line);

			prompt = g_strdup_printf("%s\n\n%s", top, base);
			g_free(top);
		} else {
			prompt = base;
			base = NULL;
		}

		service->url->passwd =
			camel_session_get_password (session, service, NULL,
						    prompt, "password", CAMEL_SESSION_PASSWORD_SECRET | (store->password_reprompt ? CAMEL_SESSION_PASSWORD_REPROMPT : 0), error);
		g_free(prompt);
		g_free(base);

		if (!service->url->passwd) {
			g_set_error (
				error, CAMEL_SERVICE_ERROR,
				CAMEL_SERVICE_ERROR_NEED_PASSWORD,
				_("Need password for authentication"));
			return -1;
		}

		store->password_reprompt = FALSE;
	}

	/* now, send auth info (currently, only authinfo user/pass is supported) */
	ret = camel_nntp_raw_command(store, &local_error, &line, "authinfo user %s", service->url->user);
	if (ret == NNTP_AUTH_CONTINUE)
		ret = camel_nntp_raw_command(store, &local_error, &line, "authinfo pass %s", service->url->passwd);

	if (ret != NNTP_AUTH_ACCEPTED) {
		if (ret != -1) {
			if (g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_CANCELLED) ||
			    g_error_matches (local_error, CAMEL_SERVICE_ERROR, CAMEL_SERVICE_ERROR_UNAVAILABLE)) {
				g_propagate_error (error, local_error);
				return ret;
			}

			/* To force password reprompt */
			store->password_reprompt = TRUE;
			g_free (service->url->passwd);
			service->url->passwd = NULL;
			goto retry;
		}
		return -1;
	}

	return ret;
}

/* Enter owning lock */
gint
camel_nntp_raw_commandv (CamelNNTPStore *store, GError **error, gchar **line, const gchar *fmt, va_list ap)
{
	GByteArray *byte_array;
	const guchar *p, *ps;
	guchar c;
	gchar *s;
	gint d;
	guint u, u2;

	g_assert(store->stream->mode != CAMEL_NNTP_STREAM_DATA);

	camel_nntp_stream_set_mode(store->stream, CAMEL_NNTP_STREAM_LINE);

	p = (const guchar *) fmt;
	ps = (const guchar *) p;

	while ((c = *p++)) {
		switch (c) {
		case '%':
			c = *p++;
			camel_stream_write ((CamelStream *) store->mem, (const gchar *) ps, p - ps - (c == '%' ? 1 : 2), NULL);
			ps = p;
			switch (c) {
			case 's':
				s = va_arg(ap, gchar *);
				camel_stream_write((CamelStream *)store->mem, s, strlen(s), NULL);
				break;
			case 'd':
				d = va_arg(ap, gint);
				camel_stream_printf((CamelStream *)store->mem, "%d", d);
				break;
			case 'u':
				u = va_arg(ap, guint);
				camel_stream_printf((CamelStream *)store->mem, "%u", u);
				break;
			case 'm':
				s = va_arg(ap, gchar *);
				camel_stream_printf((CamelStream *)store->mem, "<%s>", s);
				break;
			case 'r':
				u = va_arg(ap, guint);
				u2 = va_arg(ap, guint);
				if (u == u2)
					camel_stream_printf((CamelStream *)store->mem, "%u", u);
				else
					camel_stream_printf((CamelStream *)store->mem, "%u-%u", u, u2);
				break;
			default:
				g_warning("Passing unknown format to nntp_command: %c\n", c);
				g_assert(0);
			}
		}
	}

	camel_stream_write ((CamelStream *) store->mem, (const gchar *) ps, p-ps-1, NULL);
	camel_stream_write ((CamelStream *) store->mem, "\r\n", 2, NULL);

	byte_array = camel_stream_mem_get_byte_array (store->mem);

	if (camel_stream_write((CamelStream *) store->stream, (const gchar *) byte_array->data, byte_array->len, error) == -1)
		goto ioerror;

	/* FIXME: hack */
	camel_stream_reset ((CamelStream *) store->mem, NULL);
	g_byte_array_set_size (byte_array, 0);

	if (camel_nntp_stream_line (store->stream, (guchar **) line, &u, error) == -1)
		goto ioerror;

	u = strtoul (*line, NULL, 10);

	/* Handle all switching to data mode here, to make callers job easier */
	if (u == 215 || (u >= 220 && u <=224) || (u >= 230 && u <= 231))
		camel_nntp_stream_set_mode(store->stream, CAMEL_NNTP_STREAM_DATA);

	return u;

ioerror:
	g_prefix_error (error, _("NNTP Command failed: "));
	return -1;
}

gint
camel_nntp_raw_command(CamelNNTPStore *store, GError **error, gchar **line, const gchar *fmt, ...)
{
	gint ret;
	va_list ap;

	va_start(ap, fmt);
	ret = camel_nntp_raw_commandv(store, error, line, fmt, ap);
	va_end(ap);

	return ret;
}

/* use this where you also need auth to be handled, i.e. most cases where you'd try raw command */
gint
camel_nntp_raw_command_auth(CamelNNTPStore *store, GError **error, gchar **line, const gchar *fmt, ...)
{
	gint ret, retry, go;
	va_list ap;

	retry = 0;

	do {
		go = FALSE;
		retry++;

		va_start(ap, fmt);
		ret = camel_nntp_raw_commandv(store, error, line, fmt, ap);
		va_end(ap);

		if (ret == NNTP_AUTH_REQUIRED) {
			if (camel_nntp_try_authenticate(store, error) != NNTP_AUTH_ACCEPTED)
				return -1;
			go = TRUE;
		}
	} while (retry < 3 && go);

	return ret;
}

gint
camel_nntp_command (CamelNNTPStore *store, GError **error, CamelNNTPFolder *folder, gchar **line, const gchar *fmt, ...)
{
	const gchar *full_name = NULL;
	const guchar *p;
	va_list ap;
	gint ret, retry;
	guint u;
	GError *local_error = NULL;

	if (((CamelDiscoStore *)store)->status == CAMEL_DISCO_STORE_OFFLINE) {
		g_set_error (
			error, CAMEL_SERVICE_ERROR,
			CAMEL_SERVICE_ERROR_NOT_CONNECTED,
			_("Not connected."));
		return -1;
	}

	if (folder != NULL)
		full_name = camel_folder_get_full_name (CAMEL_FOLDER (folder));

	retry = 0;
	do {
		retry++;

		if (store->stream == NULL
		    && !camel_service_connect (CAMEL_SERVICE (store), error))
			return -1;

		/* Check for unprocessed data, !*/
		if (store->stream->mode == CAMEL_NNTP_STREAM_DATA) {
			g_warning("Unprocessed data left in stream, flushing");
			while (camel_nntp_stream_getd(store->stream, (guchar **)&p, &u) > 0)
				;
		}
		camel_nntp_stream_set_mode(store->stream, CAMEL_NNTP_STREAM_LINE);

		if (folder != NULL
		    && (store->current_folder == NULL || strcmp(store->current_folder, full_name) != 0)) {
			ret = camel_nntp_raw_command_auth(store, &local_error, line, "group %s", full_name);
			if (ret == 211) {
				g_free(store->current_folder);
				store->current_folder = g_strdup (full_name);
				if (camel_nntp_folder_selected(folder, *line, &local_error) < 0) {
					ret = -1;
					goto error;
				}
			} else {
				goto error;
			}
		}

		/* dummy fmt, we just wanted to select the folder */
		if (fmt == NULL)
			return 0;

		va_start(ap, fmt);
		ret = camel_nntp_raw_commandv(store, &local_error, line, fmt, ap);
		va_end(ap);
	error:
		switch (ret) {
		case NNTP_AUTH_REQUIRED:
			if (camel_nntp_try_authenticate(store, error) != NNTP_AUTH_ACCEPTED)
				return -1;
			retry--;
			ret = -1;
			continue;
		case 411:	/* no such group */
			g_set_error (
				error, CAMEL_FOLDER_ERROR,
				CAMEL_FOLDER_ERROR_INVALID,
				_("No such folder: %s"), *line);
			return -1;
		case 400:	/* service discontinued */
		case 401:	/* wrong client state - this should quit but this is what the old code did */
		case 503:	/* information not available - this should quit but this is what the old code did (?) */
			camel_service_disconnect (CAMEL_SERVICE (store), FALSE, NULL);
			ret = -1;
			continue;
		case -1:	/* i/o error */
			camel_service_disconnect (CAMEL_SERVICE (store), FALSE, NULL);
			if (g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_CANCELLED) || retry >= 3) {
				g_propagate_error (error, local_error);
				return -1;
			}
			g_clear_error (&local_error);
			break;
		}
	} while (ret == -1 && retry < 3);

	return ret;
}
