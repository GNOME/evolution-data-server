/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8; fill-column: 160 -*- */
/*
 *  Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 *  Authors: Michael Zucchi <notzed@ximian.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#include <glib/gi18n-lib.h>

#include "camel-nntp-folder.h"
#include "camel-nntp-store.h"
#include "camel-nntp-stream.h"
#include "camel-nntp-summary.h"

#define w(x)
#define io(x)
#define d(x) /*(printf ("%s (%d): ", __FILE__, __LINE__),(x))*/
#define dd(x) (camel_debug ("nntp")?(x):0)

#define CAMEL_NNTP_SUMMARY_VERSION (1)

#define EXTRACT_FIRST_DIGIT(val) val=strtoul (part, &part, 10);
#define EXTRACT_DIGIT(val) part++; val=strtoul (part, &part, 10);

#define CAMEL_NNTP_SUMMARY_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), CAMEL_TYPE_NNTP_SUMMARY, CamelNNTPSummaryPrivate))

struct _CamelNNTPSummaryPrivate {
	gchar *uid;

	struct _xover_header *xover; /* xoverview format */
	gint xover_setup;
};

#define _PRIVATE(o) (((CamelNNTPSummary *)(o))->priv)

static CamelMessageInfo * message_info_new_from_header (CamelFolderSummary *, struct _camel_header_raw *);
static gint summary_header_load (CamelFolderSummary *, FILE *);
static gint summary_header_save (CamelFolderSummary *, FILE *);
static gint summary_header_from_db (CamelFolderSummary *s, CamelFIRecord *mir);
static CamelFIRecord * summary_header_to_db (CamelFolderSummary *s, GError **error);

G_DEFINE_TYPE (CamelNNTPSummary, camel_nntp_summary, CAMEL_TYPE_FOLDER_SUMMARY)

static void
camel_nntp_summary_class_init (CamelNNTPSummaryClass *class)
{
	CamelFolderSummaryClass *folder_summary_class;

	g_type_class_add_private (class, sizeof (CamelNNTPSummaryPrivate));

	folder_summary_class = CAMEL_FOLDER_SUMMARY_CLASS (class);
	folder_summary_class->message_info_size = sizeof (CamelMessageInfoBase);
	folder_summary_class->content_info_size = sizeof (CamelMessageContentInfo);
	folder_summary_class->message_info_new_from_header = message_info_new_from_header;
	folder_summary_class->summary_header_load = summary_header_load;
	folder_summary_class->summary_header_save = summary_header_save;
	folder_summary_class->summary_header_from_db = summary_header_from_db;
	folder_summary_class->summary_header_to_db = summary_header_to_db;
}

static void
camel_nntp_summary_init (CamelNNTPSummary *nntp_summary)
{
	CamelFolderSummary *summary = CAMEL_FOLDER_SUMMARY (nntp_summary);

	nntp_summary->priv = CAMEL_NNTP_SUMMARY_GET_PRIVATE (nntp_summary);

	/* and a unique file version */
	summary->version += CAMEL_NNTP_SUMMARY_VERSION;
}

CamelNNTPSummary *
camel_nntp_summary_new (CamelFolder *folder, const gchar *path)
{
	CamelNNTPSummary *cns;

	cns = g_object_new (CAMEL_TYPE_NNTP_SUMMARY, NULL);
	((CamelFolderSummary *)cns)->folder = folder;

	camel_folder_summary_set_filename ((CamelFolderSummary *)cns, path);
	camel_folder_summary_set_build_content ((CamelFolderSummary *)cns, FALSE);

	return cns;
}

static CamelMessageInfo *
message_info_new_from_header (CamelFolderSummary *s,
                              struct _camel_header_raw *h)
{
	CamelMessageInfoBase *mi;
	CamelNNTPSummary *cns = (CamelNNTPSummary *)s;

	/* error to call without this setup */
	if (cns->priv->uid == NULL)
		return NULL;

	mi = (CamelMessageInfoBase *)CAMEL_FOLDER_SUMMARY_CLASS (camel_nntp_summary_parent_class)->message_info_new_from_header(s, h);
	if (mi) {
		camel_pstring_free (mi->uid);
		mi->uid = camel_pstring_strdup (cns->priv->uid);
		g_free (cns->priv->uid);
		cns->priv->uid = NULL;
	}

	return (CamelMessageInfo *)mi;
}

static gint
summary_header_from_db (CamelFolderSummary *s, CamelFIRecord *mir)
{
	CamelNNTPSummary *cns = CAMEL_NNTP_SUMMARY (s);
	gchar *part;

	if (CAMEL_FOLDER_SUMMARY_CLASS (camel_nntp_summary_parent_class)->summary_header_from_db (s, mir) == -1)
		return -1;

	part = mir->bdata;

	if (part) {
		EXTRACT_FIRST_DIGIT (cns->version)
	}

	if (part) {
		EXTRACT_DIGIT (cns->high)
	}

	if (part) {
		EXTRACT_DIGIT (cns->low)
	}

	return 0;
}

static gint
summary_header_load (CamelFolderSummary *s, FILE *in)
{
	CamelNNTPSummary *cns = CAMEL_NNTP_SUMMARY (s);

	if (CAMEL_FOLDER_SUMMARY_CLASS (camel_nntp_summary_parent_class)->summary_header_load (s, in) == -1)
		return -1;

	/* Legacy version */
	if (s->version == 0x20c) {
		camel_file_util_decode_fixed_int32 (in, (gint32 *) &cns->high);
		return camel_file_util_decode_fixed_int32 (in, (gint32 *) &cns->low);
	}

	if (camel_file_util_decode_fixed_int32 (in, (gint32 *) &cns->version) == -1)
		return -1;

	if (cns->version > CAMEL_NNTP_SUMMARY_VERSION) {
		g_warning ("Unknown NNTP summary version");
		errno = EINVAL;
		return -1;
	}

	if (camel_file_util_decode_fixed_int32 (in, (gint32 *) &cns->high) == -1
	    || camel_file_util_decode_fixed_int32 (in, (gint32 *) &cns->low) == -1)
		return -1;

	return 0;
}

static CamelFIRecord *
summary_header_to_db (CamelFolderSummary *s, GError **error)
{
	CamelNNTPSummary *cns = CAMEL_NNTP_SUMMARY (s);
	struct _CamelFIRecord *fir;

	fir = CAMEL_FOLDER_SUMMARY_CLASS (camel_nntp_summary_parent_class)->summary_header_to_db (s, error);
	if (!fir)
		return NULL;
	fir->bdata = g_strdup_printf ("%d %d %d", CAMEL_NNTP_SUMMARY_VERSION, cns->high, cns->low);

	return fir;
}

static gint
summary_header_save (CamelFolderSummary *s, FILE *out)
{
	CamelNNTPSummary *cns = CAMEL_NNTP_SUMMARY (s);

	if (CAMEL_FOLDER_SUMMARY_CLASS (camel_nntp_summary_parent_class)->summary_header_save (s, out) == -1
	    || camel_file_util_encode_fixed_int32 (out, CAMEL_NNTP_SUMMARY_VERSION) == -1
	    || camel_file_util_encode_fixed_int32 (out, cns->high) == -1
	    || camel_file_util_encode_fixed_int32 (out, cns->low) == -1)
		return -1;

	return 0;
}

/* ********************************************************************** */

/* Note: This will be called from camel_nntp_command, so only use camel_nntp_raw_command */
static gint
add_range_xover (CamelNNTPSummary *cns, CamelNNTPStore *store, guint high, guint low, CamelFolderChangeInfo *changes, GError **error)
{
	CamelFolderSummary *s;
	CamelMessageInfoBase *mi;
	struct _camel_header_raw *headers = NULL;
	gchar *line, *tab;
	guint len;
	gint ret;
	guint n, count, total, size;
	struct _xover_header *xover;
	GHashTable *summary_table;

	s = (CamelFolderSummary *)cns;
	summary_table = camel_folder_summary_get_hashtable (s);

	camel_operation_start (NULL, _("%s: Scanning new messages"), ((CamelService *)store)->url->host);

	ret = camel_nntp_raw_command_auth (store, error, &line, "over %r", low, high);
	if (ret != 224)
		ret = camel_nntp_raw_command_auth (store, error, &line, "xover %r", low, high);

	if (ret != 224) {
		camel_operation_end (NULL);
		if (ret != -1)
			g_set_error (
				error, CAMEL_ERROR, CAMEL_ERROR_GENERIC,
				_("Unexpected server response from xover: %s"), line);
		return -1;
	}

	count = 0;
	total = high-low+1;
	while ((ret = camel_nntp_stream_line (store->stream, (guchar **)&line, &len, error)) > 0) {
		camel_operation_progress (NULL, (count * 100) / total);
		count++;
		n = strtoul (line, &tab, 10);
		if (*tab != '\t')
			continue;
		tab++;
		xover = store->xover;
		size = 0;
		for (;tab[0] && xover;xover = xover->next) {
			line = tab;
			tab = strchr (line, '\t');
			if (tab)
				*tab++ = 0;
			else
				tab = line+strlen (line);

			/* do we care about this column? */
			if (xover->name) {
				line += xover->skip;
				if (line < tab) {
					camel_header_raw_append (&headers, xover->name, line, -1);
					switch (xover->type) {
					case XOVER_STRING:
						break;
					case XOVER_MSGID:
						cns->priv->uid = g_strdup_printf ("%u,%s", n, line);
						break;
					case XOVER_SIZE:
						size = strtoul (line, NULL, 10);
						break;
					}
				}
			}
		}

		/* skip headers we don't care about, incase the server doesn't actually send some it said it would. */
		while (xover && xover->name == NULL)
			xover = xover->next;

		/* truncated line? ignore? */
		if (xover == NULL) {
			if (!GPOINTER_TO_INT (g_hash_table_lookup (summary_table, cns->priv->uid))) {
				mi = (CamelMessageInfoBase *)camel_folder_summary_add_from_header (s, headers);
				if (mi) {
					mi->size = size;
					cns->high = n;
					camel_folder_change_info_add_uid (changes, camel_message_info_uid (mi));
				}
			}
		}

		if (cns->priv->uid) {
			g_free (cns->priv->uid);
			cns->priv->uid = NULL;
		}

		camel_header_raw_clear (&headers);
	}

	camel_operation_end (NULL);

	camel_folder_summary_free_hashtable (summary_table);

	return ret;
}

/* Note: This will be called from camel_nntp_command, so only use camel_nntp_raw_command */
static gint
add_range_head (CamelNNTPSummary *cns, CamelNNTPStore *store, guint high, guint low, CamelFolderChangeInfo *changes, GError **error)
{
	CamelFolderSummary *s;
	gint ret = -1;
	gchar *line, *msgid;
	guint i, n, count, total;
	CamelMessageInfo *mi;
	CamelMimeParser *mp;
	GHashTable *summary_table;

	s = (CamelFolderSummary *)cns;

	summary_table = camel_folder_summary_get_hashtable (s);

	mp = camel_mime_parser_new ();

	camel_operation_start (NULL, _("%s: Scanning new messages"), ((CamelService *)store)->url->host);

	count = 0;
	total = high-low+1;
	for (i=low;i<high+1;i++) {
		camel_operation_progress (NULL, (count * 100) / total);
		count++;
		ret = camel_nntp_raw_command_auth (store, error, &line, "head %u", i);
		/* unknown article, ignore */
		if (ret == 423)
			continue;
		else if (ret == -1)
			goto ioerror;
		else if (ret != 221) {
			g_set_error (
				error, CAMEL_ERROR, CAMEL_ERROR_GENERIC,
				_("Unexpected server response from head: %s"),
				line);
			goto ioerror;
		}
		line += 3;
		n = strtoul (line, &line, 10);
		if (n != i)
			g_warning ("retrieved message '%u' when i expected '%u'?\n", n, i);

		/* FIXME: use camel-mime-utils.c function for parsing msgid? */
		if ((msgid = strchr (line, '<')) && (line = strchr (msgid+1, '>'))) {
			line[1] = 0;
			cns->priv->uid = g_strdup_printf ("%u,%s\n", n, msgid);
			if (!GPOINTER_TO_INT (g_hash_table_lookup (summary_table, cns->priv->uid))) {
				if (camel_mime_parser_init_with_stream (mp, (CamelStream *)store->stream, error) == -1)
					goto error;
				mi = camel_folder_summary_add_from_parser (s, mp);
				while (camel_mime_parser_step (mp, NULL, NULL) != CAMEL_MIME_PARSER_STATE_EOF)
					;
				if (mi == NULL) {
					goto error;
				}
				cns->high = i;
				camel_folder_change_info_add_uid (changes, camel_message_info_uid (mi));
			}
			if (cns->priv->uid) {
				g_free (cns->priv->uid);
				cns->priv->uid = NULL;
			}
		}
	}

	ret = 0;

error:
	if (ret == -1) {
		if (errno == EINTR)
			g_set_error (
				error, G_IO_ERROR,
				G_IO_ERROR_CANCELLED,
				_("Cancelled"));
		else
			g_set_error (
				error, G_IO_ERROR,
				g_io_error_from_errno (errno),
				_("Operation failed: %s"),
				g_strerror(errno));
	}

ioerror:
	if (cns->priv->uid) {
		g_free (cns->priv->uid);
		cns->priv->uid = NULL;
	}
	g_object_unref (mp);

	camel_operation_end (NULL);

	camel_folder_summary_free_hashtable (summary_table);

	return ret;
}

/* Assumes we have the stream */
/* Note: This will be called from camel_nntp_command, so only use camel_nntp_raw_command */
gint
camel_nntp_summary_check (CamelNNTPSummary *cns, CamelNNTPStore *store, gchar *line, CamelFolderChangeInfo *changes, GError **error)
{
	CamelFolderSummary *s;
	gint ret = 0, i;
	guint n, f, l;
	gint count;
	gchar *folder = NULL;
	CamelNNTPStoreInfo *si;
	CamelStore *parent_store;
	GSList *del = NULL;
	const gchar *full_name;

	s = (CamelFolderSummary *)cns;

	full_name = camel_folder_get_full_name (s->folder);
	parent_store = camel_folder_get_parent_store (s->folder);

	line +=3;
	n = strtoul(line, &line, 10);
	f = strtoul(line, &line, 10);
	l = strtoul(line, &line, 10);
	if (line[0] == ' ') {
		gchar *tmp;

		folder = line+1;
		tmp = strchr (folder, ' ');
		if (tmp)
			*tmp = 0;
		tmp = g_alloca (strlen (folder)+1);
		strcpy (tmp, folder);
		folder = tmp;
	}

	if (cns->low == f && cns->high == l) {
		dd (printf ("nntp_summary: no work to do!\n"));
		goto update;
	}

	/* Need to work out what to do with our messages */

	/* Check for messages no longer on the server */
	if (cns->low != f) {
		count = camel_folder_summary_count (s);
		for (i = 0; i < count; i++) {
			gchar *uid;
			const gchar *msgid;

			uid  = camel_folder_summary_uid_from_index (s, i);
			n = strtoul (uid, NULL, 10);

			if (n < f || n > l) {
				dd (printf ("nntp_summary: %u is lower/higher than lowest/highest article, removed\n", n));
				/* Since we use a global cache this could prematurely remove
				   a cached message that might be in another folder - not that important as
				   it is a true cache */
				msgid = strchr (uid, ',');
				if (msgid)
					camel_data_cache_remove (store->cache, "cache", msgid+1, NULL);
				camel_folder_change_info_remove_uid (changes, uid);
				del = g_slist_prepend (del, uid);
				camel_folder_summary_remove_uid_fast (s, uid);
				uid = NULL; /*Lets not free it */
				count--;
				i--;
			}
			g_free (uid);
		}
		cns->low = f;
	}

	camel_db_delete_uids (parent_store->cdb_w, full_name, del, NULL);
	g_slist_foreach (del, (GFunc) g_free, NULL);
	g_slist_free (del);

	if (cns->high < l) {
		if (cns->high < f)
			cns->high = f-1;

		if (store->xover) {
			ret = add_range_xover (cns, store, l, cns->high+1, changes, error);
		} else {
			ret = add_range_head (cns, store, l, cns->high+1, changes, error);
		}
	}

	/* TODO: not from here */
	camel_folder_summary_touch (s);
	camel_folder_summary_save_to_db (s, NULL);

update:
	/* update store summary if we have it */
	if (folder
	    && (si = (CamelNNTPStoreInfo *)camel_store_summary_path ((CamelStoreSummary *)store->summary, folder))) {
		guint32 unread = 0;

		count = camel_folder_summary_count (s);
		camel_db_count_unread_message_info (parent_store->cdb_r, full_name, &unread, NULL);

		if (si->info.unread != unread
		    || si->info.total != count
		    || si->first != f
		    || si->last != l) {
			si->info.unread = unread;
			si->info.total = count;
			si->first = f;
			si->last = l;
			camel_store_summary_touch ((CamelStoreSummary *)store->summary);
			camel_store_summary_save ((CamelStoreSummary *)store->summary);
		}
		camel_store_summary_info_free ((CamelStoreSummary *)store->summary, (CamelStoreInfo *)si);
	} else {
		if (folder)
			g_warning ("Group '%s' not present in summary", folder);
		else
			g_warning ("Missing group from group response");
	}

	return ret;
}
