/*
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
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
 * Authors: Michael Zucchi <notzed@ximian.com>
 */

#include "evolution-data-server-config.h"

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#include <glib/gi18n-lib.h>

#include "camel-nntp-folder.h"
#include "camel-nntp-settings.h"
#include "camel-nntp-store.h"
#include "camel-nntp-stream.h"
#include "camel-nntp-summary.h"

#define w(x)
#define io(x)
#define d(x) /*(printf ("%s (%d): ", __FILE__, __LINE__),(x))*/
#define dd(x) (camel_debug ("nntp")?(x):0)

#define CAMEL_NNTP_SUMMARY_VERSION (1)

struct _CamelNNTPSummaryPrivate {
	gchar *uid;
	guint last_full_resync;
	guint last_limit_latest;

	struct _xover_header *xover; /* xoverview format */
	gint xover_setup;
};

#define NNTP_OVER_CHUNK_SIZE    5000
#define NNTP_OVER_CHUNK_MIN     1
#define NNTP_OVER_MAX_RETRIES   3
#define NNTP_IDLE_TIMEOUT_SECS  30
#define NNTP_SAVE_INTERVAL_USEC (15 * G_USEC_PER_SEC)

static CamelMessageInfo * message_info_new_from_headers (CamelFolderSummary *, const CamelNameValueArray *);
static gboolean summary_header_load (CamelFolderSummary *s, CamelStoreDBFolderRecord *record);
static gboolean summary_header_save (CamelFolderSummary *s, CamelStoreDBFolderRecord *record, GError **error);

G_DEFINE_TYPE_WITH_PRIVATE (CamelNNTPSummary, camel_nntp_summary, CAMEL_TYPE_FOLDER_SUMMARY)

static void
camel_nntp_summary_class_init (CamelNNTPSummaryClass *class)
{
	CamelFolderSummaryClass *folder_summary_class;

	folder_summary_class = CAMEL_FOLDER_SUMMARY_CLASS (class);
	folder_summary_class->message_info_new_from_headers = message_info_new_from_headers;
	folder_summary_class->summary_header_load = summary_header_load;
	folder_summary_class->summary_header_save = summary_header_save;
}

static void
camel_nntp_summary_init (CamelNNTPSummary *nntp_summary)
{
	CamelFolderSummary *summary = CAMEL_FOLDER_SUMMARY (nntp_summary);

	nntp_summary->priv = camel_nntp_summary_get_instance_private (nntp_summary);

	/* and a unique file version */
	camel_folder_summary_set_version (summary, camel_folder_summary_get_version (summary) + CAMEL_NNTP_SUMMARY_VERSION);
}

CamelNNTPSummary *
camel_nntp_summary_new (CamelFolder *folder)
{
	CamelNNTPSummary *cns;

	cns = g_object_new (CAMEL_TYPE_NNTP_SUMMARY, "folder", folder, NULL);

	return cns;
}

static CamelMessageInfo *
message_info_new_from_headers (CamelFolderSummary *summary,
			       const CamelNameValueArray *headers)
{
	CamelMessageInfo *mi;
	CamelNNTPSummary *cns = (CamelNNTPSummary *) summary;

	/* error to call without this setup */
	if (cns->priv->uid == NULL)
		return NULL;

	mi = CAMEL_FOLDER_SUMMARY_CLASS (camel_nntp_summary_parent_class)->message_info_new_from_headers (summary, headers);
	if (mi) {
		camel_message_info_set_uid (mi, cns->priv->uid);
		g_free (cns->priv->uid);
		cns->priv->uid = NULL;
	}

	return mi;
}

static gboolean
summary_header_load (CamelFolderSummary *s,
		     CamelStoreDBFolderRecord *record)
{
	CamelNNTPSummary *cns = CAMEL_NNTP_SUMMARY (s);
	gchar *part;

	if (!CAMEL_FOLDER_SUMMARY_CLASS (camel_nntp_summary_parent_class)->summary_header_load (s, record))
		return FALSE;

	part = record->bdata;

	cns->version = camel_util_bdata_get_number (&part, 0);
	cns->high = camel_util_bdata_get_number (&part, 0);
	cns->low = camel_util_bdata_get_number (&part, 0);
	cns->priv->last_full_resync = camel_util_bdata_get_number (&part, 0);
	cns->priv->last_limit_latest = camel_util_bdata_get_number (&part, 0);

	return TRUE;
}

static gboolean
summary_header_save (CamelFolderSummary *s,
		     CamelStoreDBFolderRecord *record,
		     GError **error)
{
	CamelNNTPSummary *cns = CAMEL_NNTP_SUMMARY (s);

	if (!CAMEL_FOLDER_SUMMARY_CLASS (camel_nntp_summary_parent_class)->summary_header_save (s, record, error))
		return FALSE;
	record->bdata = g_strdup_printf ("%d %u %u %u %u", CAMEL_NNTP_SUMMARY_VERSION, cns->high, cns->low, cns->priv->last_full_resync, cns->priv->last_limit_latest);

	return TRUE;
}

/* ********************************************************************** */

/* Note: This will be called from camel_nntp_command, so only use camel_nntp_raw_command */
static gint
add_range_xover (CamelNNTPSummary *cns,
                 CamelNNTPStore *nntp_store,
                 guint high,
                 guint low,
                 CamelFolderChangeInfo *changes,
                 guint *inout_progress_count,
                 guint progress_total,
                 GCancellable *cancellable,
                 GError **error)
{
	CamelNNTPCapabilities capability = CAMEL_NNTP_CAPABILITY_OVER;
	CamelNNTPStream *nntp_stream;
	CamelNetworkSettings *network_settings;
	CamelSettings *settings;
	CamelService *service;
	CamelFolderSummary *s;
	CamelNameValueArray *headers = NULL;
	gchar *line, *tab;
	gchar *host;
	guint len;
	gint ret;
	guint n, size;
	guint old_timeout;
	gboolean folder_filter_recent;
	struct _xover_header *xover;

	s = (CamelFolderSummary *) cns;
	folder_filter_recent = camel_folder_summary_get_folder (s) &&
		(camel_folder_get_flags (camel_folder_summary_get_folder (s)) & CAMEL_FOLDER_FILTER_RECENT) != 0;

	service = CAMEL_SERVICE (nntp_store);

	settings = camel_service_ref_settings (service);

	network_settings = CAMEL_NETWORK_SETTINGS (settings);
	host = camel_network_settings_dup_host (network_settings);

	g_object_unref (settings);

	camel_operation_push_message (
		cancellable, _("%s: Scanning new messages"), host);

	g_free (host);

	if (camel_nntp_store_has_capabilities (nntp_store, capability))
		ret = camel_nntp_raw_command_auth (
			nntp_store, cancellable, error,
			&line, "over %r", low, high);
	else
		ret = -1;
	if (ret != 224) {
		camel_nntp_store_remove_capabilities (nntp_store, capability);
		ret = camel_nntp_raw_command_auth (
			nntp_store, cancellable, error,
			&line, "xover %r", low, high);
	}

	if (ret != 224) {
		camel_operation_pop_message (cancellable);
		if (ret != -1)
			g_set_error (
				error, CAMEL_ERROR, CAMEL_ERROR_GENERIC,
				_("Unexpected server response from xover: %s"), line);
		return -1;
	}

	nntp_stream = camel_nntp_store_ref_stream (nntp_store);

	old_timeout = camel_nntp_stream_get_timeout (nntp_stream);

	/* The server already responded 224, so it is alive.  Use a
	 * reduced idle timeout to detect mid-transfer connection
	 * kills by DPI middleboxes faster than the default 180s,
	 * but not so aggressively that normal network latency
	 * causes false timeouts. */
	camel_nntp_stream_set_timeout (nntp_stream, NNTP_IDLE_TIMEOUT_SECS);

	headers = camel_name_value_array_new ();
	while ((ret = camel_nntp_stream_line (nntp_stream, (guchar **) &line, &len, cancellable, error)) > 0) {
		if (progress_total > 0)
			camel_operation_progress (cancellable, ((*inout_progress_count) * 100) / progress_total);
		(*inout_progress_count)++;
		n = strtoul (line, &tab, 10);
		if (*tab != '\t')
			continue;
		tab++;
		xover = nntp_store->xover;
		size = 0;
		for (; tab[0] && xover; xover = xover->next) {
			line = tab;
			tab = strchr (line, '\t');
			if (tab)
				*tab++ = 0;
			else
				tab = line + strlen (line);

			/* do we care about this column? */
			if (xover->name) {
				line += xover->skip;
				if (line < tab) {
					camel_name_value_array_append (headers, xover->name, line);
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
			if (!camel_folder_summary_check_uid (s, cns->priv->uid)) {
				CamelMessageInfo *mi;

				mi = camel_folder_summary_info_new_from_headers (s, headers);
				camel_message_info_set_size (mi, size);
				camel_folder_summary_add (s, mi, FALSE);

				cns->high = n;
				camel_folder_change_info_add_uid (changes, camel_message_info_get_uid (mi));
				if (folder_filter_recent)
					camel_folder_change_info_recent_uid (changes, camel_message_info_get_uid (mi));
				g_clear_object (&mi);
			} else if (cns->high < n) {
				cns->high = n;
			}
		}

		g_clear_pointer (&cns->priv->uid, g_free);

		camel_name_value_array_clear (headers);
	}

	camel_name_value_array_free (headers);
	camel_nntp_stream_set_timeout (nntp_stream, old_timeout);
	g_clear_object (&nntp_stream);

	camel_operation_pop_message (cancellable);

	return ret;
}

/* Note: This will be called from camel_nntp_command, so only use camel_nntp_raw_command */
static gint
add_range_head (CamelNNTPSummary *cns,
                CamelNNTPStore *nntp_store,
                guint high,
                guint low,
                CamelFolderChangeInfo *changes,
                guint *inout_progress_count,
                guint progress_total,
                GCancellable *cancellable,
                GError **error)
{
	CamelNNTPStream *nntp_stream;
	CamelNetworkSettings *network_settings;
	CamelSettings *settings;
	CamelService *service;
	CamelFolderSummary *s;
	gint ret = -1;
	gchar *line, *msgid;
	guint i, n;
	CamelMessageInfo *mi;
	CamelMimeParser *mp;
	gchar *host;
	gboolean folder_filter_recent;
	guint old_timeout;

	s = (CamelFolderSummary *) cns;
	folder_filter_recent = camel_folder_summary_get_folder (s) &&
		(camel_folder_get_flags (camel_folder_summary_get_folder (s)) & CAMEL_FOLDER_FILTER_RECENT) != 0;

	mp = camel_mime_parser_new ();

	service = CAMEL_SERVICE (nntp_store);

	settings = camel_service_ref_settings (service);

	network_settings = CAMEL_NETWORK_SETTINGS (settings);
	host = camel_network_settings_dup_host (network_settings);

	g_object_unref (settings);

	camel_operation_push_message (
		cancellable, _("%s: Scanning new messages"), host);

	g_free (host);

	nntp_stream = camel_nntp_store_ref_stream (nntp_store);

	old_timeout = camel_nntp_stream_get_timeout (nntp_stream);

	/* Use reduced idle timeout — see comment in add_range_xover. */
	camel_nntp_stream_set_timeout (nntp_stream, NNTP_IDLE_TIMEOUT_SECS);

	for (i = low; i < high + 1; i++) {
		if (progress_total > 0)
			camel_operation_progress (cancellable, ((*inout_progress_count) * 100) / progress_total);
		(*inout_progress_count)++;
		ret = camel_nntp_raw_command_auth (
			nntp_store, cancellable, error, &line, "head %u", i);
		/* unknown article, ignore */
		if (ret == 423)
			continue;
		else if (ret == -1)
			goto error;
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
		if ((msgid = strchr (line, '<')) && (line = strchr (msgid + 1, '>'))) {
			line[1] = 0;
			cns->priv->uid = g_strdup_printf ("%u,%s\n", n, msgid);
			if (!camel_folder_summary_check_uid (s, cns->priv->uid)) {
				if (camel_mime_parser_init_with_stream (mp, CAMEL_STREAM (nntp_stream), error) == -1)
					goto error;
				mi = camel_folder_summary_info_new_from_parser (s, mp);
				camel_folder_summary_add (s, mi, FALSE);
				while (camel_mime_parser_step (mp, NULL, NULL) != CAMEL_MIME_PARSER_STATE_EOF)
					;
				if (mi == NULL) {
					goto error;
				}
				cns->high = i;
				camel_folder_change_info_add_uid (changes, camel_message_info_get_uid (mi));
				if (folder_filter_recent)
					camel_folder_change_info_recent_uid (changes, camel_message_info_get_uid (mi));
				g_clear_object (&mi);
			}
			g_clear_pointer (&cns->priv->uid, g_free);
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
				g_strerror (errno));
	}

ioerror:
	g_clear_pointer (&cns->priv->uid, g_free);
	g_object_unref (mp);

	camel_nntp_stream_set_timeout (nntp_stream, old_timeout);
	g_clear_object (&nntp_stream);

	camel_operation_pop_message (cancellable);

	return ret;
}

static gint
fetch_articles_chunked (CamelNNTPSummary *cns,
                        CamelNNTPStore *store,
                        const gchar *full_name,
                        guint target_high,
                        CamelFolderChangeInfo *changes,
                        CamelFolderSummary *s,
                        GCancellable *cancellable,
                        GError **error)
{
	GError *local_error = NULL;
	gchar *grp_line = NULL;
	guint scan_from;
	guint chunk_size = NNTP_OVER_CHUNK_SIZE;
	guint retries = 0;
	guint progress_count = 0;
	guint total_to_fetch;
	gint64 last_save_time = 0;
	gint ret = 0;

	scan_from = cns->high + 1;
	total_to_fetch = target_high - cns->high;

	while (scan_from <= target_high) {
		guint chunk_low, chunk_high, old_high;

		chunk_low = scan_from;
		chunk_high = chunk_low + chunk_size - 1;
		if (chunk_high > target_high)
			chunk_high = target_high;
		old_high = cns->high;

		if (store->xover)
			ret = add_range_xover (cns, store, chunk_high, chunk_low,
				changes, &progress_count, total_to_fetch,
				cancellable, &local_error);
		else
			ret = add_range_head (cns, store, chunk_high, chunk_low,
				changes, &progress_count, total_to_fetch,
				cancellable, &local_error);

		if (ret == -1 &&
		    g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_TIMED_OUT)) {
			g_clear_error (&local_error);

			if (cns->high > old_high) {
				/* Partial timeout: got some articles
				 * then connection was killed (DPI).
				 * Advance past received data.  Keep
				 * chunk_size large -- the per-connection
				 * byte budget resets on reconnect. */
				scan_from = cns->high + 1;
				retries = 0;
			} else {
				/* Zero-progress timeout: server did
				 * not send anything.  Try smaller
				 * chunk, and after too many failures
				 * skip forward to the next range. */
				retries++;

				if (retries > NNTP_OVER_MAX_RETRIES) {
					/* Stuck at this range.  Skip
					 * forward — this range is
					 * unreachable (DPI or dead spot).
					 * Jump by one chunk_size and
					 * reset for a fresh attempt. */
					scan_from = chunk_high + 1;
					if (cns->high < chunk_high)
						cns->high = chunk_high;
					chunk_size = NNTP_OVER_CHUNK_SIZE;
					retries = 0;
				} else if (chunk_size > NNTP_OVER_CHUNK_MIN) {
					chunk_size /= 2;
					if (chunk_size < NNTP_OVER_CHUNK_MIN)
						chunk_size = NNTP_OVER_CHUNK_MIN;
				}
			}

			/* Save progress and reconnect.  A new TCP
			 * connection gets a fresh byte budget from
			 * middleboxes.  Always save here (not throttled)
			 * because a reconnect or crash may follow. */
			camel_folder_summary_touch (s);
			camel_folder_summary_save (s, NULL);
			last_save_time = g_get_monotonic_time ();

			if (g_cancellable_is_cancelled (cancellable)) {
				ret = -1;
				break;
			}

			camel_service_disconnect_sync (CAMEL_SERVICE (store), FALSE, cancellable, NULL);
			if (!camel_service_connect_sync (CAMEL_SERVICE (store), cancellable, error)) {
				ret = -1;
				break;
			}
			if (camel_nntp_raw_command_auth (store, cancellable, error, &grp_line, "group %s", full_name) == 211) {
				camel_nntp_store_set_current_group (store, full_name);
			} else {
				camel_nntp_store_set_current_group (store, NULL);
				ret = -1;
				break;
			}
			ret = 0;
			continue;

		} else if (ret == -1) {
			g_propagate_error (error, local_error);
			break;
		}

		/* Success: full chunk completed. */
		retries = 0;
		if (chunk_size < NNTP_OVER_CHUNK_SIZE) {
			chunk_size *= 2;
			if (chunk_size > NNTP_OVER_CHUNK_SIZE)
				chunk_size = NNTP_OVER_CHUNK_SIZE;
		}

		/* OVER chunk_low–chunk_high has been fully processed;
		 * all articles in this range are fetched.  Advance
		 * past the entire scanned range, not just the last
		 * article number found (the range may be sparse). */
		if (cns->high < chunk_high)
			cns->high = chunk_high;
		scan_from = chunk_high + 1;

		camel_folder_summary_touch (s);

		/* Avoid hammering the disk on fast connections;
		 * save at most once per NNTP_SAVE_INTERVAL_USEC.
		 * The timeout path above always saves immediately
		 * because a reconnect (or crash) may follow. */
		if (g_get_monotonic_time () - last_save_time >= NNTP_SAVE_INTERVAL_USEC) {
			camel_folder_summary_save (s, NULL);
			last_save_time = g_get_monotonic_time ();
		}

		if (g_cancellable_is_cancelled (cancellable))
			break;
	}

	/* Final save to flush any unsaved progress. */
	camel_folder_summary_save (s, NULL);

	return ret;
}

/* Assumes we have the stream */
/* Note: This will be called from camel_nntp_command, so only use camel_nntp_raw_command */
gint
camel_nntp_summary_check (CamelNNTPSummary *cns,
                          CamelNNTPStore *store,
                          gchar *line,
                          CamelFolderChangeInfo *changes,
                          GCancellable *cancellable,
                          GError **error)
{
	CamelNNTPStoreSummary *nntp_store_summary;
	CamelStoreSummary *store_summary;
	CamelFolderSummary *s;
	gint ret = 0, i;
	guint n, f, l, limit_latest = 0;
	gint count;
	gchar *folder = NULL;
	CamelNNTPStoreInfo *si = NULL;
	CamelStore *parent_store;
	CamelSettings *settings;
	GPtrArray *known_uids;
	const gchar *full_name;

	s = (CamelFolderSummary *) cns;

	full_name = camel_folder_get_full_name (camel_folder_summary_get_folder (s));
	parent_store = camel_folder_get_parent_store (camel_folder_summary_get_folder (s));

	settings = camel_service_ref_settings (CAMEL_SERVICE (store));
	if (settings) {
		if (camel_nntp_settings_get_use_limit_latest (CAMEL_NNTP_SETTINGS (settings)))
			limit_latest = camel_nntp_settings_get_limit_latest (CAMEL_NNTP_SETTINGS (settings));

		g_object_unref (settings);
	}

	line +=3;
	(void)strtoul (line, &line, 10);
	f = strtoul (line, &line, 10);
	l = strtoul (line, &line, 10);
	if (line[0] == ' ') {
		gchar *tmp;
		gsize tmp_len;

		folder = line + 1;
		tmp = strchr (folder, ' ');
		if (tmp)
			*tmp = 0;

		tmp_len = strlen (folder) + 1;
		tmp = g_alloca (tmp_len);
		g_strlcpy (tmp, folder, tmp_len);
		folder = tmp;
	}

	if (cns->low == f && cns->high == l && cns->priv->last_limit_latest >= limit_latest) {
		if (cns->priv->last_limit_latest != limit_latest) {
			cns->priv->last_limit_latest = limit_latest;

			camel_folder_summary_touch (s);
			camel_folder_summary_save (s, NULL);
		}

		dd (printf ("nntp_summary: no work to do!\n"));
		goto update;
	}

	/* Need to work out what to do with our messages */

	/* Check for messages no longer on the server */
	known_uids = camel_folder_summary_dup_uids (s);
	if (known_uids) {
		GPtrArray *removed = NULL;

		/* Only remove articles whose numbers fall outside the
		 * server's current [f, l] range (from the GROUP response).
		 * This is safe because GROUP returns a single line that
		 * cannot be truncated by middleboxes.
		 *
		 * The original code also did a daily LISTGROUP-based
		 * full resync, but that streams hundreds of thousands of
		 * article numbers and is vulnerable to truncation by DPI
		 * middleboxes.  A truncated response causes Evolution to
		 * incorrectly delete valid locally-cached articles.
		 * Since NNTP articles are only expired by server retention
		 * policy (not by user action), stale local entries are
		 * harmless — opening one just shows "article not found". */
		if (cns->low != f) {
			for (i = 0; i < known_uids->len; i++) {
				const gchar *uid;

				uid = g_ptr_array_index (known_uids, i);
				n = strtoul (uid, NULL, 10);

				if (n < f || n > l) {
					if (!removed)
						removed = g_ptr_array_new_with_free_func ((GDestroyNotify) camel_pstring_free);
					g_ptr_array_add (removed, (gpointer) camel_pstring_strdup (uid));
				}
			}
		}

		if (removed) {
			camel_folder_summary_remove_uids (s, removed);
			g_ptr_array_unref (removed);
		}

		g_clear_pointer (&known_uids, g_ptr_array_unref);
	}

	cns->low = f;

	if (cns->high < l || limit_latest != cns->priv->last_limit_latest) {
		if (limit_latest > 0 && l - f > limit_latest)
			f = l - limit_latest + 1;

		if (cns->high < f || limit_latest != cns->priv->last_limit_latest)
			cns->high = f - 1;

		ret = fetch_articles_chunked (
			cns, store, full_name, l,
			changes, s, cancellable, error);
	}

	cns->priv->last_limit_latest = limit_latest;

	/* TODO: not from here */
	camel_folder_summary_touch (s);
	camel_folder_summary_save (s, NULL);

update:
	/* update store summary if we have it */

	nntp_store_summary = camel_nntp_store_ref_summary (store);

	store_summary = CAMEL_STORE_SUMMARY (nntp_store_summary);

	if (folder != NULL)
		si = (CamelNNTPStoreInfo *)
			camel_store_summary_path (store_summary, folder);

	if (si != NULL) {
		guint32 unread = 0;

		count = camel_folder_summary_count (s);
		camel_store_db_count_messages (camel_store_get_db (parent_store), full_name, CAMEL_STORE_DB_COUNT_KIND_UNREAD, &unread, NULL);

		if (si->info.unread != unread
		    || si->info.total != count
		    || si->first != f
		    || si->last != l) {
			si->info.unread = unread;
			si->info.total = count;
			si->first = f;
			si->last = l;
			camel_store_summary_touch (store_summary);
			camel_store_summary_save (store_summary);
		}
		camel_store_info_unref ((CamelStoreInfo *) si);

	} else if (folder != NULL) {
		g_warning ("Group '%s' not present in summary", folder);

	} else {
		g_warning ("Missing group from group response");
	}

	g_clear_object (&nntp_store_summary);

	return ret;
}
