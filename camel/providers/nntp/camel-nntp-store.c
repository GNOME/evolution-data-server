/*ed.txtcamel-unused.txt-*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
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

#include <glib/gstdio.h>
#include <glib/gi18n-lib.h>

#include "camel-nntp-folder.h"
#include "camel-nntp-private.h"
#include "camel-nntp-resp-codes.h"
#include "camel-nntp-settings.h"
#include "camel-nntp-store-summary.h"
#include "camel-nntp-store.h"
#include "camel-nntp-summary.h"

#ifdef G_OS_WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

#define w(x)
#define dd(x) (camel_debug("nntp")?(x):0)

#define NNTP_PORT  119
#define NNTPS_PORT 563

#define DUMP_EXTENSIONS

static GInitableIface *parent_initable_interface;

/* Forward Declarations */
static void camel_nntp_store_initable_init (GInitableIface *interface);
static void camel_network_service_init (CamelNetworkServiceInterface *interface);
static void camel_subscribable_init (CamelSubscribableInterface *interface);

G_DEFINE_TYPE_WITH_CODE (
	CamelNNTPStore,
	camel_nntp_store,
	CAMEL_TYPE_DISCO_STORE,
	G_IMPLEMENT_INTERFACE (
		G_TYPE_INITABLE,
		camel_nntp_store_initable_init)
	G_IMPLEMENT_INTERFACE (
		CAMEL_TYPE_NETWORK_SERVICE,
		camel_network_service_init)
	G_IMPLEMENT_INTERFACE (
		CAMEL_TYPE_SUBSCRIBABLE,
		camel_subscribable_init))

static void
nntp_store_dispose (GObject *object)
{
	CamelNNTPStore *nntp_store = CAMEL_NNTP_STORE (object);
	CamelDiscoStore *disco_store = CAMEL_DISCO_STORE (object);

	/* Only run this the first time. */
	if (nntp_store->summary != NULL)
		camel_service_disconnect_sync (
			CAMEL_SERVICE (object), TRUE, NULL, NULL);

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

	xover = nntp_store->xover;
	while (xover) {
		xn = xover->next;
		g_free (xover);
		xover = xn;
	}

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (camel_nntp_store_parent_class)->finalize (object);
}

static gboolean
nntp_can_work_offline (CamelDiscoStore *store)
{
	return TRUE;
}

static gint
check_capabilities (CamelNNTPStore *store,
             GCancellable *cancellable,
             GError **error)
{
	gint ret;
	gchar *line;
	guint len;

	store->capabilities = 0;

	ret = camel_nntp_raw_command_auth (store, cancellable, error, &line, "CAPABILITIES");
	if (ret != 101)
		return -1;

	while ((ret = camel_nntp_stream_line (store->stream, (guchar **) &line, &len, cancellable, error)) > 0) {
		while (len > 0 && g_ascii_isspace (*line)) {
			line++;
			len--;
		}

		if (len == 4 && g_ascii_strncasecmp (line, "OVER", len) == 0)
			store->capabilities |= NNTP_CAPABILITY_OVER;

		if (len == 1 && g_ascii_strncasecmp (line, ".", len) == 0) {
			ret = 0;
			break;
		}
	}

	return ret;
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
xover_setup (CamelNNTPStore *store,
             GCancellable *cancellable,
             GError **error)
{
	gint ret, i;
	gchar *line;
	guint len;
	guchar c, *p;
	struct _xover_header *xover, *last;

	/* manual override */
	if (store->xover || getenv ("CAMEL_NNTP_DISABLE_XOVER") != NULL)
		return 0;

	ret = camel_nntp_raw_command_auth (store, cancellable, error, &line, "list overview.fmt");
	if (ret == -1) {
		return -1;
	} else if (ret != 215)
		/* unsupported command?  ignore */
		return 0;

	last = (struct _xover_header *) &store->xover;

	/* supported command */
	while ((ret = camel_nntp_stream_line (store->stream, (guchar **) &line, &len, cancellable, error)) > 0) {
		p = (guchar *) line;
		xover = g_malloc0 (sizeof (*xover));
		last->next = xover;
		last = xover;
		while ((c = *p++)) {
			if (c == ':') {
				p[-1] = 0;
				for (i = 0; i < G_N_ELEMENTS (headers); i++) {
					if (strcmp (line, headers[i].name) == 0) {
						xover->name = headers[i].name;
						if (strncmp ((gchar *) p, "full", 4) == 0)
							xover->skip = strlen (xover->name) + 1;
						else
							xover->skip = 0;
						xover->type = headers[i].type;
						break;
					}
				}
				break;
			} else {
				p[-1] = camel_tolower (c);
			}
		}
	}

	return ret;
}

static gboolean
connect_to_server (CamelService *service,
                   GCancellable *cancellable,
                   GError **error)
{
	CamelNNTPStore *store = (CamelNNTPStore *) service;
	CamelDiscoStore *disco_store = (CamelDiscoStore *) service;
	CamelNetworkSettings *network_settings;
	CamelSettings *settings;
	CamelSession *session;
	CamelStream *tcp_stream;
	const gchar *user_cache_dir;
	gboolean retval = FALSE;
	guchar *buf;
	guint len;
	gchar *host;
	gchar *path;
	gchar *user;

	session = camel_service_get_session (service);
	user_cache_dir = camel_service_get_user_cache_dir (service);

	settings = camel_service_ref_settings (service);

	network_settings = CAMEL_NETWORK_SETTINGS (settings);
	host = camel_network_settings_dup_host (network_settings);
	user = camel_network_settings_dup_user (network_settings);

	g_object_unref (settings);

	tcp_stream = camel_network_service_connect_sync (
		CAMEL_NETWORK_SERVICE (service), cancellable, error);

	if (tcp_stream == NULL)
		goto fail;

	store->stream = (CamelNNTPStream *) camel_nntp_stream_new (tcp_stream);
	g_object_unref (tcp_stream);

	/* Read the greeting, if any. */
	if (camel_nntp_stream_line (store->stream, &buf, &len, cancellable, error) == -1) {
		g_prefix_error (
			error, _("Could not read greeting from %s: "), host);

		g_object_unref (store->stream);
		store->stream = NULL;

		goto fail;
	}

	len = strtoul ((gchar *) buf, (gchar **) &buf, 10);
	if (len != 200 && len != 201) {
		g_set_error (
			error, CAMEL_ERROR, CAMEL_ERROR_GENERIC,
			_("NNTP server %s returned error code %d: %s"),
			host, len, buf);

		g_object_unref (store->stream);
		store->stream = NULL;

		goto fail;
	}

	/* if we have username, try it here */
	if (user != NULL && *user != '\0') {

		/* XXX No SASL support. */
		if (!camel_session_authenticate_sync (
			session, service, NULL, cancellable, error))
			goto fail;
	}

	/* set 'reader' mode & ignore return code, also ping the server, inn goes offline very quickly otherwise */
	if (camel_nntp_raw_command_auth (store, cancellable, error, (gchar **) &buf, "mode reader") == -1
	    || camel_nntp_raw_command_auth (store, cancellable, error, (gchar **) &buf, "date") == -1)
		goto fail;

	if (xover_setup (store, cancellable, error) == -1)
		goto fail;

	if (!disco_store->diary) {
		path = g_build_filename (user_cache_dir, ".ev-journal", NULL);
		disco_store->diary = camel_disco_diary_new (disco_store, path, error);
		g_free (path);
	}

	retval = TRUE;

	g_free (store->current_folder);
	store->current_folder = NULL;

fail:
	g_free (host);
	g_free (user);

	return retval;
}

static gboolean
nntp_connect_online (CamelService *service,
                     GCancellable *cancellable,
                     GError **error)
{
	CamelNNTPStore *store = CAMEL_NNTP_STORE (service);
	GError *local_error = NULL;

	if (!connect_to_server (service, cancellable, error))
		return FALSE;

	if (check_capabilities (store, cancellable, &local_error) != -1)
		return TRUE;

	if (local_error)
		g_error_free (local_error);

	store->capabilities = 0;

	/* disconnect and reconnect without capability check */

	if (store->stream)
		g_object_unref (store->stream);
	store->stream = NULL;
	g_free (store->current_folder);
	store->current_folder = NULL;

	return connect_to_server (service, cancellable, error);
}

static gboolean
nntp_connect_offline (CamelService *service,
                      GCancellable *cancellable,
                      GError **error)
{
	CamelNNTPStore *nntp_store = CAMEL_NNTP_STORE (service);
	CamelDiscoStore *disco_store = (CamelDiscoStore *) nntp_store;
	const gchar *user_cache_dir;
	gchar *path;

	user_cache_dir = camel_service_get_user_cache_dir (service);

	/* setup store-wide cache */
	if (nntp_store->cache == NULL) {
		nntp_store->cache = camel_data_cache_new (user_cache_dir, error);
		if (nntp_store->cache == NULL)
			return FALSE;

		/* Default cache expiry - 2 weeks old, or not visited in 5 days */
		camel_data_cache_set_expire_age (nntp_store->cache, 60 * 60 * 24 * 14);
		camel_data_cache_set_expire_access (nntp_store->cache, 60 * 60 * 24 * 5);
	}

	if (disco_store->diary)
		return TRUE;

	path = g_build_filename (user_cache_dir, ".ev-journal", NULL);
	disco_store->diary = camel_disco_diary_new (disco_store, path, error);
	g_free (path);

	if (!disco_store->diary)
		return FALSE;

	return TRUE;
}

static gboolean
nntp_disconnect_online (CamelService *service,
                        gboolean clean,
                        GCancellable *cancellable,
                        GError **error)
{
	CamelNNTPStore *store = CAMEL_NNTP_STORE (service);
	gchar *line;

	if (clean)
		camel_nntp_raw_command (store, cancellable, NULL, &line, "quit");

	g_object_unref (store->stream);
	store->stream = NULL;
	g_free (store->current_folder);
	store->current_folder = NULL;

	return TRUE;
}

static gboolean
nntp_disconnect_offline (CamelService *service,
                         gboolean clean,
                         GCancellable *cancellable,
                         GError **error)
{
	CamelDiscoStore *disco = CAMEL_DISCO_STORE (service);

	if (disco->diary) {
		g_object_unref (disco->diary);
		disco->diary = NULL;
	}

	return TRUE;
}

static gchar *
nntp_store_get_name (CamelService *service,
                     gboolean brief)
{
	CamelNetworkSettings *network_settings;
	CamelSettings *settings;
	gchar *host;
	gchar *name;

	settings = camel_service_ref_settings (service);

	network_settings = CAMEL_NETWORK_SETTINGS (settings);
	host = camel_network_settings_dup_host (network_settings);

	g_object_unref (settings);

	if (brief)
		name = g_strdup_printf ("%s", host);
	else
		name = g_strdup_printf (_("USENET News via %s"), host);

	g_free (host);

	return name;
}

extern CamelServiceAuthType camel_nntp_password_authtype;

static CamelAuthenticationResult
nntp_store_authenticate_sync (CamelService *service,
                              const gchar *mechanism,
                              GCancellable *cancellable,
                              GError **error)
{
	CamelNetworkSettings *network_settings;
	CamelSettings *settings;
	CamelNNTPStore *store;
	CamelAuthenticationResult result;
	const gchar *password;
	gchar *line = NULL;
	gchar *user;
	gint status;

	store = CAMEL_NNTP_STORE (service);

	password = camel_service_get_password (service);

	settings = camel_service_ref_settings (service);

	network_settings = CAMEL_NETWORK_SETTINGS (settings);
	user = camel_network_settings_dup_user (network_settings);

	g_object_unref (settings);

	if (user == NULL) {
		g_set_error_literal (
			error, CAMEL_SERVICE_ERROR,
			CAMEL_SERVICE_ERROR_CANT_AUTHENTICATE,
			_("Cannot authenticate without a username"));
		result = CAMEL_AUTHENTICATION_ERROR;
		goto exit;
	}

	if (password == NULL) {
		g_set_error_literal (
			error, CAMEL_SERVICE_ERROR,
			CAMEL_SERVICE_ERROR_CANT_AUTHENTICATE,
			_("Authentication password not available"));
		result = CAMEL_AUTHENTICATION_ERROR;
		goto exit;
	}

	/* XXX Currently only authinfo user/pass is supported. */
	status = camel_nntp_raw_command (
		store, cancellable, error, &line,
		"authinfo user %s", user);
	if (status == NNTP_AUTH_CONTINUE)
		status = camel_nntp_raw_command (
			store, cancellable, error, &line,
			"authinfo pass %s", password);

	switch (status) {
		case NNTP_AUTH_ACCEPTED:
			result = CAMEL_AUTHENTICATION_ACCEPTED;
			break;

		case NNTP_AUTH_REJECTED:
			result = CAMEL_AUTHENTICATION_REJECTED;
			break;

		default:
			result = CAMEL_AUTHENTICATION_ERROR;
			break;
	}

exit:
	g_free (user);

	return result;
}

static GList *
nntp_store_query_auth_types_sync (CamelService *service,
                                  GCancellable *cancellable,
                                  GError **error)
{
	return g_list_append (NULL, &camel_nntp_password_authtype);
}

static CamelFolder *
nntp_get_folder (CamelStore *store,
                 const gchar *folder_name,
                 guint32 flags,
                 GCancellable *cancellable,
                 GError **error)
{
	return camel_nntp_folder_new (
		store, folder_name, cancellable, error);
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
nntp_folder_info_from_store_info (CamelNNTPStore *store,
                                  gboolean short_notation,
                                  CamelStoreInfo *si)
{
	CamelFolderInfo *fi;

	fi = camel_folder_info_new ();
	fi->full_name = g_strdup (si->path);

	if (short_notation)
		fi->display_name = nntp_newsgroup_name_short (si->path);
	else
		fi->display_name = g_strdup (si->path);

	fi->unread = si->unread;
	fi->total = si->total;
	fi->flags = si->flags;

	return fi;
}

static CamelFolderInfo *
nntp_folder_info_from_name (CamelNNTPStore *store,
                            gboolean short_notation,
                            const gchar *name)
{
	CamelFolderInfo *fi;

	fi = camel_folder_info_new ();
	fi->full_name = g_strdup (name);

	if (short_notation)
		fi->display_name = nntp_newsgroup_name_short (name);
	else
		fi->display_name = g_strdup (name);

	fi->unread = -1;

	return fi;
}

/* handle list/newgroups response */
static CamelNNTPStoreInfo *
nntp_store_info_update (CamelNNTPStore *store,
                        gchar *line)
{
	CamelStoreSummary *summ = (CamelStoreSummary *) store->summary;
	CamelNNTPStoreInfo *si, *fsi;
	gchar *relpath, *tmp;
	guint32 last = 0, first = 0, new = 0;

	tmp = strchr (line, ' ');
	if (tmp)
		*tmp++ = 0;

	fsi = si = (CamelNNTPStoreInfo *) camel_store_summary_path ((CamelStoreSummary *) store->summary, line);
	if (si == NULL) {
		si = (CamelNNTPStoreInfo *) camel_store_summary_info_new (summ);

		relpath = g_alloca (strlen (line) + 2);
		sprintf (relpath, "/%s", line);

		si->info.path = g_strdup (line);
		si->full_name = g_strdup (line); /* why do we keep this? */
		camel_store_summary_add ((CamelStoreSummary *) store->summary, &si->info);
	} else {
		first = si->first;
		last = si->last;
	}

	if (tmp && *tmp >= '0' && *tmp <= '9') {
		last = strtoul (tmp, &tmp, 10);
		if (*tmp == ' ' && tmp[1] >= '0' && tmp[1] <= '9') {
			first = strtoul (tmp + 1, &tmp, 10);
			if (*tmp == ' ' && tmp[1] != 'y')
				si->info.flags |= CAMEL_STORE_INFO_FOLDER_READONLY;
		}
	}

	dd (printf ("store info update '%s' first '%u' last '%u'\n", line, first, last));

	if (si->last) {
		if (last > si->last)
			new = last - si->last;
	} else {
		if (last > first)
			new = last - first;
	}

	si->info.total = last > first ? last - first : 0;
	si->info.unread += new;	/* this is a _guess_ */
	si->last = last;
	si->first = first;

	if (fsi)
		camel_store_summary_info_free ((CamelStoreSummary *) store->summary, &fsi->info);
	else			/* TODO see if we really did touch it */
		camel_store_summary_touch ((CamelStoreSummary *) store->summary);

	return si;
}

static CamelFolderInfo *
nntp_store_get_subscribed_folder_info (CamelNNTPStore *store,
                                       const gchar *top,
                                       guint flags,
                                       GCancellable *cancellable,
                                       GError **error)
{
	CamelService *service;
	CamelSettings *settings;
	CamelStoreInfo *si;
	CamelFolderInfo *first = NULL, *last = NULL, *fi = NULL;
	gboolean short_folder_names;
	gint i;

	/* since we do not do a tree, any request that is not for root is sure to give no results */
	if (top != NULL && top[0] != 0)
		return NULL;

	service = CAMEL_SERVICE (store);

	settings = camel_service_ref_settings (service);

	short_folder_names = camel_nntp_settings_get_short_folder_names (
		CAMEL_NNTP_SETTINGS (settings));

	g_object_unref (settings);

	for (i = 0; i < camel_store_summary_count ((CamelStoreSummary *) store->summary); i++) {
		si = camel_store_summary_index ((CamelStoreSummary *) store->summary, i);
		if (si == NULL)
			continue;

		if (si->flags & CAMEL_STORE_INFO_FOLDER_SUBSCRIBED) {
			/* slow mode?  open and update the folder, always! this will implictly update
			 * our storeinfo too; in a very round-about way */
			if ((flags & CAMEL_STORE_FOLDER_INFO_FAST) == 0) {
				CamelNNTPFolder *folder;
				gchar *line;

				folder = (CamelNNTPFolder *)
					camel_store_get_folder_sync (
					(CamelStore *) store, si->path,
					0, cancellable, NULL);
				if (folder) {
					CamelFolderChangeInfo *changes = NULL;

					if (camel_nntp_command (store, cancellable, NULL, folder, &line, NULL) != -1) {
						if (camel_folder_change_info_changed (folder->changes)) {
							changes = folder->changes;
							folder->changes = camel_folder_change_info_new ();
						}
					}
					if (changes) {
						camel_folder_changed (CAMEL_FOLDER (folder), changes);
						camel_folder_change_info_free (changes);
					}
					g_object_unref (folder);
				}
			}
			fi = nntp_folder_info_from_store_info (store, short_folder_names, si);
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
tree_insert (CamelFolderInfo *root,
             CamelFolderInfo *last,
             CamelFolderInfo *fi)
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
nntp_push_to_hierarchy (CamelNNTPStore *store,
                        CamelFolderInfo *root,
                        CamelFolderInfo *pfi,
                        GHashTable *known)
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
			fi = camel_folder_info_new ();
			fi->full_name = g_strdup (pfi->full_name);
			fi->display_name = g_strdup (name);

			fi->unread = 0;
			fi->total = 0;
			fi->flags =
				CAMEL_FOLDER_NOSELECT |
				CAMEL_FOLDER_CHILDREN;

			g_hash_table_insert (known, fi->full_name, fi);
			root = tree_insert (root, last, fi);
			last = fi;
		} else {
			last = kfi;
		}

		*dot = '.';
		name = dot + 1;
	}

	g_free (pfi->display_name);
	pfi->display_name = g_strdup (name);

	return tree_insert (root, last, pfi);
}

/*
 * get folder info, using the information in our StoreSummary
 */
static CamelFolderInfo *
nntp_store_get_cached_folder_info (CamelNNTPStore *store,
                                   const gchar *orig_top,
                                   guint flags,
                                   GError **error)
{
	CamelService *service;
	CamelSettings *settings;
	gint i;
	gint subscribed_or_flag = (flags & CAMEL_STORE_FOLDER_INFO_SUBSCRIBED) ? 0 : 1,
	    root_or_flag = (orig_top == NULL || orig_top[0] == '\0') ? 1 : 0,
	    recursive_flag = flags & CAMEL_STORE_FOLDER_INFO_RECURSIVE,
	    is_folder_list = flags & CAMEL_STORE_FOLDER_INFO_SUBSCRIPTION_LIST;
	CamelStoreInfo *si;
	CamelFolderInfo *first = NULL, *last = NULL, *fi = NULL;
	GHashTable *known; /* folder name to folder info */
	gboolean folder_hierarchy_relative;
	gchar *tmpname;
	gchar *top = g_strconcat (orig_top ? orig_top:"", ".", NULL);
	gint toplen = strlen (top);

	service = CAMEL_SERVICE (store);

	settings = camel_service_ref_settings (service);

	folder_hierarchy_relative =
		camel_nntp_settings_get_folder_hierarchy_relative (
		CAMEL_NNTP_SETTINGS (settings));

	g_object_unref (settings);

	known = g_hash_table_new (g_str_hash, g_str_equal);

	for (i = 0; (si = camel_store_summary_index ((CamelStoreSummary *) store->summary, i)); i++) {
		if ((subscribed_or_flag || (si->flags & CAMEL_STORE_INFO_FOLDER_SUBSCRIBED))
		    && (root_or_flag || strncmp (si->path, top, toplen) == 0)) {
			if (recursive_flag || is_folder_list || strchr (si->path + toplen, '.') == NULL) {
				/* add the item */
				fi = nntp_folder_info_from_store_info (store, FALSE, si);
				if (!fi)
					continue;
				if (folder_hierarchy_relative) {
					g_free (fi->display_name);
					fi->display_name = g_strdup (si->path + ((toplen == 1) ? 0 : toplen));
				}
			} else {
				/* apparently, this is an indirect subitem. if it's not a subitem of
				 * the item we added last, we need to add a portion of this item to
				 * the list as a placeholder */
				if (!last ||
				    strncmp (si->path, last->full_name, strlen (last->full_name)) != 0 ||
				    si->path[strlen (last->full_name)] != '.') {
					gchar *dot;
					tmpname = g_strdup (si->path);
					dot = strchr (tmpname + toplen, '.');
					if (dot)
						*dot = '\0';
					fi = nntp_folder_info_from_name (store, FALSE, tmpname);
					if (!fi)
						continue;

					fi->flags |= CAMEL_FOLDER_NOSELECT;
					if (folder_hierarchy_relative) {
						g_free (fi->display_name);
						fi->display_name = g_strdup (tmpname + ((toplen == 1) ? 0 : toplen));
					}
					g_free (tmpname);
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
				first = nntp_push_to_hierarchy (store, first, fi, known);
			} else {
				if (last)
					last->next = fi;
				else
					first = fi;
				last = fi;
			}
		} else if (subscribed_or_flag && first) {
			/* we have already added subitems, but this item is no longer a subitem */
			camel_store_summary_info_free ((CamelStoreSummary *) store->summary, si);
			break;
		}
		camel_store_summary_info_free ((CamelStoreSummary *) store->summary, si);
	}

	g_hash_table_destroy (known);
	g_free (top);
	return first;
}

static void
store_info_remove (gpointer key,
                   gpointer value,
                   gpointer data)
{
	CamelStoreSummary *summary = data;
	CamelStoreInfo *si = value;

	camel_store_summary_remove (summary, si);
}

static gint
store_info_sort (gconstpointer a,
                 gconstpointer b)
{
	return strcmp ((*(CamelNNTPStoreInfo **) a)->full_name, (*(CamelNNTPStoreInfo **) b)->full_name);
}

/* retrieves the date from the NNTP server */
static gboolean
nntp_get_date (CamelNNTPStore *nntp_store,
               GCancellable *cancellable,
               GError **error)
{
	guchar *line;
	gint ret = camel_nntp_command (nntp_store, cancellable, error, NULL, (gchar **) &line, "date");
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

static CamelFolderInfo *
nntp_store_get_folder_info_all (CamelNNTPStore *nntp_store,
                                const gchar *top,
                                CamelStoreGetFolderInfoFlags flags,
                                gboolean online,
                                GCancellable *cancellable,
                                GError **error)
{
	CamelNNTPStoreSummary *summary = nntp_store->summary;
	CamelNNTPStoreInfo *si;
	guint len;
	guchar *line;
	gint ret = -1;
	CamelFolderInfo *fi = NULL;

	if (top == NULL)
		top = "";

	if (online && (top == NULL || top[0] == 0)) {
		/* we may need to update */
		if (summary->last_newslist[0] != 0) {
			gchar date[14];
			memcpy (date, summary->last_newslist + 2, 6); /* YYMMDDD */
			date[6] = ' ';
			memcpy (date + 7, summary->last_newslist + 8, 6); /* HHMMSS */
			date[13] = '\0';

			/* Some servers don't support date (!), so fallback if they dont */
			if (!nntp_get_date (nntp_store, cancellable, NULL))
				goto do_complete_list_nodate;

			ret = camel_nntp_command (nntp_store, cancellable, error, NULL, (gchar **) &line, "newgroups %s", date);
			if (ret == -1)
				goto error;
			else if (ret != 231) {
				/* newgroups not supported :S so reload the complete list */
				summary->last_newslist[0] = 0;
				goto do_complete_list;
			}

			while ((ret = camel_nntp_stream_line (nntp_store->stream, &line, &len, cancellable, error)) > 0)
				nntp_store_info_update (nntp_store, (gchar *) line);
		} else {
			GHashTable *all;
			gint i;

		do_complete_list:
			/* seems we do need a complete list */
			/* at first, we do a DATE to find out the last load occasion */
			nntp_get_date (nntp_store, cancellable, NULL);
		do_complete_list_nodate:
			ret = camel_nntp_command (nntp_store, cancellable, error, NULL, (gchar **) &line, "list");
			if (ret == -1)
				goto error;
			else if (ret != 215) {
				g_set_error (
					error, CAMEL_SERVICE_ERROR,
					CAMEL_SERVICE_ERROR_INVALID,
					_("Error retrieving newsgroups:\n\n%s"), line);
				goto error;
			}

			all = g_hash_table_new (g_str_hash, g_str_equal);
			for (i = 0; (si = (CamelNNTPStoreInfo *) camel_store_summary_index ((CamelStoreSummary *) nntp_store->summary, i)); i++)
				g_hash_table_insert (all, si->info.path, si);

			while ((ret = camel_nntp_stream_line (nntp_store->stream, &line, &len, cancellable, error)) > 0) {
				si = nntp_store_info_update (nntp_store, (gchar *) line);
				g_hash_table_remove (all, si->info.path);
			}

			g_hash_table_foreach (all, store_info_remove, nntp_store->summary);
			g_hash_table_destroy (all);
		}

		/* sort the list */
		g_ptr_array_sort (CAMEL_STORE_SUMMARY (nntp_store->summary)->folders, store_info_sort);
		if (ret < 0)
			goto error;

		camel_store_summary_save ((CamelStoreSummary *) nntp_store->summary);
	}

	fi = nntp_store_get_cached_folder_info (nntp_store, top, flags, error);
error:

	return fi;
}

static CamelFolderInfo *
nntp_get_folder_info (CamelStore *store,
                      const gchar *top,
                      CamelStoreGetFolderInfoFlags flags,
                      gboolean online,
                      GCancellable *cancellable,
                      GError **error)
{
	CamelNNTPStore *nntp_store = CAMEL_NNTP_STORE (store);
	CamelFolderInfo *first = NULL;

	dd (printf (
		"g_f_i: fast %d subscr %d recursive %d online %d top \"%s\"\n",
		flags & CAMEL_STORE_FOLDER_INFO_FAST,
		flags & CAMEL_STORE_FOLDER_INFO_SUBSCRIBED,
		flags & CAMEL_STORE_FOLDER_INFO_RECURSIVE,
		online,
		top ? top:""));

	if (flags & CAMEL_STORE_FOLDER_INFO_SUBSCRIBED)
		first = nntp_store_get_subscribed_folder_info (
			nntp_store, top, flags, cancellable, error);
	else
		first = nntp_store_get_folder_info_all (
			nntp_store, top, flags, online, cancellable, error);

	return first;
}

static CamelFolderInfo *
nntp_get_folder_info_online (CamelStore *store,
                             const gchar *top,
                             CamelStoreGetFolderInfoFlags flags,
                             GCancellable *cancellable,
                             GError **error)
{
	return nntp_get_folder_info (
		store, top, flags, TRUE, cancellable, error);
}

static CamelFolderInfo *
nntp_get_folder_info_offline (CamelStore *store,
                              const gchar *top,
                              CamelStoreGetFolderInfoFlags flags,
                              GCancellable *cancellable,
                              GError **error)
{
	return nntp_get_folder_info (
		store, top, flags, FALSE, cancellable, error);
}

/* stubs for various folder operations we're not implementing */

static CamelFolderInfo *
nntp_store_create_folder_sync (CamelStore *store,
                               const gchar *parent_name,
                               const gchar *folder_name,
                               GCancellable *cancellable,
                               GError **error)
{
	g_set_error (
		error, CAMEL_FOLDER_ERROR,
		CAMEL_FOLDER_ERROR_INVALID,
		_("You cannot create a folder in a News store: "
		"subscribe instead."));

	return NULL;
}

static gboolean
nntp_store_rename_folder_sync (CamelStore *store,
                               const gchar *old_name,
                               const gchar *new_name_in,
                               GCancellable *cancellable,
                               GError **error)
{
	g_set_error (
		error, CAMEL_FOLDER_ERROR,
		CAMEL_FOLDER_ERROR_INVALID,
		_("You cannot rename a folder in a News store."));

	return FALSE;
}

static gboolean
nntp_store_delete_folder_sync (CamelStore *store,
                               const gchar *folder_name,
                               GCancellable *cancellable,
                               GError **error)
{
	CamelSubscribable *subscribable;
	CamelSubscribableInterface *interface;

	subscribable = CAMEL_SUBSCRIBABLE (store);
	interface = CAMEL_SUBSCRIBABLE_GET_INTERFACE (subscribable);

	interface->unsubscribe_folder_sync (
		subscribable, folder_name, cancellable, NULL);

	g_set_error (
		error, CAMEL_FOLDER_ERROR,
		CAMEL_FOLDER_ERROR_INVALID,
		_("You cannot remove a folder in a News store: "
		"unsubscribe instead."));

	return FALSE;
}

static gboolean
nntp_can_refresh_folder (CamelStore *store,
                         CamelFolderInfo *info,
                         GError **error)
{
	/* any nntp folder can be refreshed */
	return TRUE;
}

/* nntp stores part of its data in user_data_dir and part in user_cache_dir,
 * thus check whether to migrate based on folders.db file */
static void
nntp_migrate_to_user_cache_dir (CamelService *service)
{
	const gchar *user_data_dir, *user_cache_dir;
	gchar *udd_folders_db, *ucd_folders_db;

	g_return_if_fail (service != NULL);
	g_return_if_fail (CAMEL_IS_SERVICE (service));

	user_data_dir = camel_service_get_user_data_dir (service);
	user_cache_dir = camel_service_get_user_cache_dir (service);

	g_return_if_fail (user_data_dir != NULL);
	g_return_if_fail (user_cache_dir != NULL);

	udd_folders_db = g_build_filename (user_data_dir, "folders.db", NULL);
	ucd_folders_db = g_build_filename (user_cache_dir, "folders.db", NULL);

	/* migrate only if the source directory exists and the destination doesn't */
	if (g_file_test (udd_folders_db, G_FILE_TEST_EXISTS) &&
	    !g_file_test (ucd_folders_db, G_FILE_TEST_EXISTS)) {
		gchar *parent_dir;

		parent_dir = g_path_get_dirname (user_cache_dir);
		g_mkdir_with_parents (parent_dir, S_IRWXU);
		g_free (parent_dir);

		if (g_rename (user_data_dir, user_cache_dir) == -1) {
			g_debug ("%s: Failed to migrate '%s' to '%s': %s", G_STRFUNC, user_data_dir, user_cache_dir, g_strerror (errno));
		} else if (g_mkdir_with_parents (user_data_dir, S_IRWXU) != -1) {
			gchar *udd_ev_store_summary, *ucd_ev_store_summary;

			udd_ev_store_summary = g_build_filename (user_data_dir, ".ev-store-summary", NULL);
			ucd_ev_store_summary = g_build_filename (user_cache_dir, ".ev-store-summary", NULL);

			/* return back the .ev-store-summary file, it's saved in user_data_dir */
			if (g_rename (ucd_ev_store_summary, udd_ev_store_summary) == -1)
				g_debug ("%s: Failed to return back '%s' to '%s': %s", G_STRFUNC, ucd_ev_store_summary, udd_ev_store_summary, g_strerror (errno));
		}
	}

	g_free (udd_folders_db);
	g_free (ucd_folders_db);
}

static gboolean
nntp_store_initable_init (GInitable *initable,
                          GCancellable *cancellable,
                          GError **error)
{
	CamelNNTPStore *nntp_store;
	CamelStore *store;
	CamelService *service;
	const gchar *user_data_dir;
	const gchar *user_cache_dir;
	gchar *tmp;

	nntp_store = CAMEL_NNTP_STORE (initable);
	store = CAMEL_STORE (initable);
	service = CAMEL_SERVICE (initable);

	store->flags |= CAMEL_STORE_USE_CACHE_DIR;
	nntp_migrate_to_user_cache_dir (service);

	/* Chain up to parent interface's init() method. */
	if (!parent_initable_interface->init (initable, cancellable, error))
		return FALSE;

	service = CAMEL_SERVICE (initable);
	user_data_dir = camel_service_get_user_data_dir (service);
	user_cache_dir = camel_service_get_user_cache_dir (service);

	if (g_mkdir_with_parents (user_data_dir, S_IRWXU) == -1) {
		g_set_error_literal (
			error, G_FILE_ERROR,
			g_file_error_from_errno (errno),
			g_strerror (errno));
		return FALSE;
	}

	tmp = g_build_filename (user_data_dir, ".ev-store-summary", NULL);
	nntp_store->summary = camel_nntp_store_summary_new ();
	camel_store_summary_set_filename ((CamelStoreSummary *) nntp_store->summary, tmp);
	g_free (tmp);

	camel_store_summary_load ((CamelStoreSummary *) nntp_store->summary);

	/* setup store-wide cache */
	nntp_store->cache = camel_data_cache_new (user_cache_dir, error);
	if (nntp_store->cache == NULL)
		return FALSE;

	/* Default cache expiry - 2 weeks old, or not visited in 5 days */
	camel_data_cache_set_expire_age (nntp_store->cache, 60 * 60 * 24 * 14);
	camel_data_cache_set_expire_access (nntp_store->cache, 60 * 60 * 24 * 5);

	return TRUE;
}

static const gchar *
nntp_store_get_service_name (CamelNetworkService *service,
                             CamelNetworkSecurityMethod method)
{
	const gchar *service_name;

	switch (method) {
		case CAMEL_NETWORK_SECURITY_METHOD_SSL_ON_ALTERNATE_PORT:
			service_name = "nntps";
			break;

		default:
			service_name = "nntp";
			break;
	}

	return service_name;
}

static guint16
nntp_store_get_default_port (CamelNetworkService *service,
                             CamelNetworkSecurityMethod method)
{
	guint16 default_port;

	switch (method) {
		case CAMEL_NETWORK_SECURITY_METHOD_SSL_ON_ALTERNATE_PORT:
			default_port = NNTPS_PORT;
			break;

		default:
			default_port = NNTP_PORT;
			break;
	}

	return default_port;
}

static gboolean
nntp_store_folder_is_subscribed (CamelSubscribable *subscribable,
                                 const gchar *folder_name)
{
	CamelNNTPStore *nntp_store = CAMEL_NNTP_STORE (subscribable);
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
nntp_store_subscribe_folder_sync (CamelSubscribable *subscribable,
                                  const gchar *folder_name,
                                  GCancellable *cancellable,
                                  GError **error)
{
	CamelNNTPStore *nntp_store = CAMEL_NNTP_STORE (subscribable);
	CamelService *service;
	CamelSettings *settings;
	CamelStoreInfo *si;
	CamelFolderInfo *fi;
	gboolean short_folder_names;
	gboolean success = TRUE;

	service = CAMEL_SERVICE (subscribable);

	settings = camel_service_ref_settings (service);

	short_folder_names = camel_nntp_settings_get_short_folder_names (
		CAMEL_NNTP_SETTINGS (settings));

	g_object_unref (settings);

	si = camel_store_summary_path (CAMEL_STORE_SUMMARY (nntp_store->summary), folder_name);
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
			fi = nntp_folder_info_from_store_info (nntp_store, short_folder_names, si);
			fi->flags |= CAMEL_FOLDER_NOINFERIORS | CAMEL_FOLDER_NOCHILDREN;
			camel_store_summary_touch ((CamelStoreSummary *) nntp_store->summary);
			camel_store_summary_save ((CamelStoreSummary *) nntp_store->summary);
			camel_subscribable_folder_subscribed (subscribable, fi);
			camel_folder_info_free (fi);
		}
	}

	return success;
}

static gboolean
nntp_store_unsubscribe_folder_sync (CamelSubscribable *subscribable,
                                    const gchar *folder_name,
                                    GCancellable *cancellable,
                                    GError **error)
{
	CamelNNTPStore *nntp_store = CAMEL_NNTP_STORE (subscribable);
	CamelService *service;
	CamelSettings *settings;
	CamelFolderInfo *fi;
	CamelStoreInfo *fitem;
	gboolean short_folder_names;
	gboolean success = TRUE;

	service = CAMEL_SERVICE (subscribable);

	settings = camel_service_ref_settings (service);

	short_folder_names = camel_nntp_settings_get_short_folder_names (
		CAMEL_NNTP_SETTINGS (settings));

	g_object_unref (settings);

	fitem = camel_store_summary_path (CAMEL_STORE_SUMMARY (nntp_store->summary), folder_name);

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
			fi = nntp_folder_info_from_store_info (nntp_store, short_folder_names, fitem);
			camel_store_summary_touch ((CamelStoreSummary *) nntp_store->summary);
			camel_store_summary_save ((CamelStoreSummary *) nntp_store->summary);
			camel_subscribable_folder_unsubscribed (subscribable, fi);
			camel_folder_info_free (fi);
		}
	}

	return success;
}

static void
camel_nntp_store_class_init (CamelNNTPStoreClass *class)
{
	GObjectClass *object_class;
	CamelServiceClass *service_class;
	CamelStoreClass *store_class;
	CamelDiscoStoreClass *disco_store_class;

	object_class = G_OBJECT_CLASS (class);
	object_class->dispose = nntp_store_dispose;
	object_class->finalize = nntp_store_finalize;

	service_class = CAMEL_SERVICE_CLASS (class);
	service_class->settings_type = CAMEL_TYPE_NNTP_SETTINGS;
	service_class->get_name = nntp_store_get_name;
	service_class->authenticate_sync = nntp_store_authenticate_sync;
	service_class->query_auth_types_sync = nntp_store_query_auth_types_sync;

	store_class = CAMEL_STORE_CLASS (class);
	store_class->can_refresh_folder = nntp_can_refresh_folder;
	store_class->free_folder_info = camel_store_free_folder_info_full;
	store_class->create_folder_sync = nntp_store_create_folder_sync;
	store_class->delete_folder_sync = nntp_store_delete_folder_sync;
	store_class->rename_folder_sync = nntp_store_rename_folder_sync;

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
camel_nntp_store_initable_init (GInitableIface *interface)
{
	parent_initable_interface = g_type_interface_peek_parent (interface);

	interface->init = nntp_store_initable_init;
}

static void
camel_network_service_init (CamelNetworkServiceInterface *interface)
{
	interface->get_service_name = nntp_store_get_service_name;
	interface->get_default_port = nntp_store_get_default_port;
}

static void
camel_subscribable_init (CamelSubscribableInterface *interface)
{
	interface->folder_is_subscribed = nntp_store_folder_is_subscribed;
	interface->subscribe_folder_sync = nntp_store_subscribe_folder_sync;
	interface->unsubscribe_folder_sync = nntp_store_unsubscribe_folder_sync;
}

static void
camel_nntp_store_init (CamelNNTPStore *nntp_store)
{
	nntp_store->mem = (CamelStreamMem *) camel_stream_mem_new ();

	/* Clear the default flags.  We don't want a virtual Junk or Trash
	 * folder and the user can't create/delete/rename newsgroup folders. */
	CAMEL_STORE (nntp_store)->flags = 0;
}

/* Enter owning lock */
gint
camel_nntp_raw_commandv (CamelNNTPStore *store,
                         GCancellable *cancellable,
                         GError **error,
                         gchar **line,
                         const gchar *fmt,
                         va_list ap)
{
	CamelStream *stream;
	GByteArray *byte_array;
	const guchar *p, *ps;
	guchar c;
	gchar *s;
	gint d;
	guint u, u2;

	g_assert (store->stream->mode != CAMEL_NNTP_STREAM_DATA);

	camel_nntp_stream_set_mode (store->stream, CAMEL_NNTP_STREAM_LINE);

	p = (const guchar *) fmt;
	ps = (const guchar *) p;

	stream = CAMEL_STREAM (store->mem);

	while ((c = *p++)) {
		gchar *strval;

		switch (c) {
		case '%':
			c = *p++;
			camel_stream_write (
				stream, (const gchar *) ps,
				p - ps - (c == '%' ? 1 : 2),
				NULL, NULL);
			ps = p;
			switch (c) {
			case 's':
				s = va_arg (ap, gchar *);
				strval = g_strdup (s);
				break;
			case 'd':
				d = va_arg (ap, gint);
				strval = g_strdup_printf ("%d", d);
				break;
			case 'u':
				u = va_arg (ap, guint);
				strval = g_strdup_printf ("%u", u);
				break;
			case 'm':
				s = va_arg (ap, gchar *);
				strval = g_strdup_printf ("<%s>", s);
				break;
			case 'r':
				u = va_arg (ap, guint);
				u2 = va_arg (ap, guint);
				if (u == u2)
					strval = g_strdup_printf ("%u", u);
				else
					strval = g_strdup_printf ("%u-%u", u, u2);
				break;
			default:
				g_warning ("Passing unknown format to nntp_command: %c\n", c);
				g_assert (0);
			}

			camel_stream_write_string (stream, strval, NULL, NULL);

			g_free (strval);
			strval = NULL;
		}
	}

	camel_stream_write (stream, (const gchar *) ps, p - ps - 1, NULL, NULL);
	camel_stream_write (stream, "\r\n", 2, NULL, NULL);

	byte_array = camel_stream_mem_get_byte_array (store->mem);

	if (camel_stream_write (
		(CamelStream *) store->stream,
		(const gchar *) byte_array->data,
		byte_array->len, cancellable, error) == -1)
		goto ioerror;

	/* FIXME: hack */
	g_seekable_seek (G_SEEKABLE (stream), 0, G_SEEK_SET, NULL, NULL);
	g_byte_array_set_size (byte_array, 0);

	if (camel_nntp_stream_line (store->stream, (guchar **) line, &u, cancellable, error) == -1)
		goto ioerror;

	u = strtoul (*line, NULL, 10);

	/* Handle all switching to data mode here, to make callers job easier */
	if (u == 215 || (u >= 220 && u <=224) || (u >= 230 && u <= 231))
		camel_nntp_stream_set_mode (store->stream, CAMEL_NNTP_STREAM_DATA);

	return u;

ioerror:
	g_prefix_error (error, _("NNTP Command failed: "));
	return -1;
}

gint
camel_nntp_raw_command (CamelNNTPStore *store,
                        GCancellable *cancellable,
                        GError **error,
                        gchar **line,
                        const gchar *fmt,
                        ...)
{
	gint ret;
	va_list ap;

	va_start (ap, fmt);
	ret = camel_nntp_raw_commandv (store, cancellable, error, line, fmt, ap);
	va_end (ap);

	return ret;
}

/* use this where you also need auth to be handled, i.e. most cases where you'd try raw command */
gint
camel_nntp_raw_command_auth (CamelNNTPStore *store,
                             GCancellable *cancellable,
                             GError **error,
                             gchar **line,
                             const gchar *fmt,
                             ...)
{
	CamelService *service;
	CamelSession *session;
	gint ret, retry, go;
	va_list ap;

	service = CAMEL_SERVICE (store);
	session = camel_service_get_session (service);

	retry = 0;

	do {
		go = FALSE;
		retry++;

		va_start (ap, fmt);
		ret = camel_nntp_raw_commandv (store, cancellable, error, line, fmt, ap);
		va_end (ap);

		if (ret == NNTP_AUTH_REQUIRED) {
			if (!camel_session_authenticate_sync (
				session, service, NULL, cancellable, error))
				return -1;
			go = TRUE;
		}
	} while (retry < 3 && go);

	return ret;
}

gint
camel_nntp_command (CamelNNTPStore *store,
                    GCancellable *cancellable,
                    GError **error,
                    CamelNNTPFolder *folder,
                    gchar **line,
                    const gchar *fmt,
                    ...)
{
	CamelService *service;
	CamelSession *session;
	const gchar *full_name = NULL;
	const guchar *p;
	va_list ap;
	gint ret, retry;
	guint u;
	GError *local_error = NULL;

	service = CAMEL_SERVICE (store);
	session = camel_service_get_session (service);

	if (((CamelDiscoStore *) store)->status == CAMEL_DISCO_STORE_OFFLINE) {
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
		    && !camel_service_connect_sync (service, cancellable, error))
			return -1;

		/* Check for unprocessed data, !*/
		if (store->stream && store->stream->mode == CAMEL_NNTP_STREAM_DATA) {
			g_warning ("Unprocessed data left in stream, flushing");
			while (camel_nntp_stream_getd (store->stream, (guchar **) &p, &u, cancellable, error) > 0)
				;
		}
		camel_nntp_stream_set_mode (store->stream, CAMEL_NNTP_STREAM_LINE);

		if (folder != NULL
		    && (store->current_folder == NULL || strcmp (store->current_folder, full_name) != 0)) {
			ret = camel_nntp_raw_command_auth (store, cancellable, &local_error, line, "group %s", full_name);
			if (ret == 211) {
				g_free (store->current_folder);
				store->current_folder = g_strdup (full_name);
				if (camel_nntp_folder_selected (folder, *line, NULL, &local_error) < 0) {
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

		va_start (ap, fmt);
		ret = camel_nntp_raw_commandv (store, cancellable, &local_error, line, fmt, ap);
		va_end (ap);
	error:
		switch (ret) {
		case NNTP_AUTH_REQUIRED:
			if (!camel_session_authenticate_sync (
				session, service, NULL, cancellable, error))
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
			camel_service_disconnect_sync (
				service, FALSE, cancellable, NULL);
			ret = -1;
			continue;
		case -1:	/* i/o error */
			camel_service_disconnect_sync (
				service, FALSE, cancellable, NULL);
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
