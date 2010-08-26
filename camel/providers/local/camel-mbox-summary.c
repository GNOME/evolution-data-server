/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8; fill-column: 160 -*-
 *
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
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <glib/gi18n-lib.h>
#include <glib/gstdio.h>

#include "camel-mbox-summary.h"
#include "camel-local-private.h"

#define io(x)
#define d(x) /*(printf("%s(%d): ", __FILE__, __LINE__),(x))*/

#define CAMEL_MBOX_SUMMARY_VERSION (1)

#define EXTRACT_DIGIT(val) part++; val=strtoul (part, &part, 10);
#define EXTRACT_FIRST_DIGIT(val) val=strtoul (part, &part, 10);

static CamelFIRecord * summary_header_to_db (CamelFolderSummary *, GError **error);
static gint summary_header_from_db (CamelFolderSummary *, CamelFIRecord *);
static CamelMessageInfo * message_info_from_db(CamelFolderSummary *s, CamelMIRecord *record);
static CamelMIRecord * message_info_to_db(CamelFolderSummary *s, CamelMessageInfo *info);

static gint summary_header_load (CamelFolderSummary *, FILE *);
static gint summary_header_save (CamelFolderSummary *, FILE *);

static CamelMessageInfo * message_info_new_from_header(CamelFolderSummary *, struct _camel_header_raw *);
static CamelMessageInfo * message_info_new_from_parser(CamelFolderSummary *, CamelMimeParser *);
static CamelMessageInfo * message_info_migrate (CamelFolderSummary *, FILE *);
/*static void		  message_info_free (CamelFolderSummary *, CamelMessageInfo *);*/

static gchar *mbox_summary_encode_x_evolution (CamelLocalSummary *cls, const CamelLocalMessageInfo *mi);

static gint mbox_summary_check(CamelLocalSummary *cls, CamelFolderChangeInfo *changeinfo, GError **error);
static gint mbox_summary_sync(CamelLocalSummary *cls, gboolean expunge, CamelFolderChangeInfo *changeinfo, GError **error);
#ifdef STATUS_PINE
static CamelMessageInfo *mbox_summary_add(CamelLocalSummary *cls, CamelMimeMessage *msg, const CamelMessageInfo *info, CamelFolderChangeInfo *ci, GError **error);
#endif

static gint mbox_summary_sync_quick(CamelMboxSummary *cls, gboolean expunge, CamelFolderChangeInfo *changeinfo, GError **error);
static gint mbox_summary_sync_full(CamelMboxSummary *cls, gboolean expunge, CamelFolderChangeInfo *changeinfo, GError **error);

#ifdef STATUS_PINE
/* Which status flags are stored in each separate header */
#define STATUS_XSTATUS (CAMEL_MESSAGE_FLAGGED|CAMEL_MESSAGE_ANSWERED|CAMEL_MESSAGE_DELETED)
#define STATUS_STATUS (CAMEL_MESSAGE_SEEN)

static void encode_status(guint32 flags, gchar status[8]);
static guint32 decode_status(const gchar *status);
#endif

G_DEFINE_TYPE (CamelMboxSummary, camel_mbox_summary, CAMEL_TYPE_LOCAL_SUMMARY)

static gboolean
mbox_info_set_user_flag(CamelMessageInfo *mi, const gchar *name, gboolean value)
{
	gint res;

	res = CAMEL_FOLDER_SUMMARY_CLASS (camel_mbox_summary_parent_class)->info_set_user_flag(mi, name, value);
	if (res)
		((CamelLocalMessageInfo *)mi)->info.flags |= CAMEL_MESSAGE_FOLDER_FLAGGED;

	return res;
}

static gboolean
mbox_info_set_user_tag(CamelMessageInfo *mi, const gchar *name, const gchar *value)
{
	gint res;

	res = CAMEL_FOLDER_SUMMARY_CLASS (camel_mbox_summary_parent_class)->info_set_user_tag(mi, name, value);
	if (res)
		((CamelLocalMessageInfo *)mi)->info.flags |= CAMEL_MESSAGE_FOLDER_FLAGGED;

	return res;
}

#ifdef STATUS_PINE
static gboolean
mbox_info_set_flags(CamelMessageInfo *mi, guint32 flags, guint32 set)
{
	/* Basically, if anything could change the Status line, presume it does */
	if (((CamelMboxSummary *)mi->summary)->xstatus
	    && (flags & (CAMEL_MESSAGE_SEEN|CAMEL_MESSAGE_FLAGGED|CAMEL_MESSAGE_ANSWERED|CAMEL_MESSAGE_DELETED))) {
		flags |= CAMEL_MESSAGE_FOLDER_XEVCHANGE|CAMEL_MESSAGE_FOLDER_FLAGGED;
		set |= CAMEL_MESSAGE_FOLDER_XEVCHANGE|CAMEL_MESSAGE_FOLDER_FLAGGED;
	}

	return CAMEL_FOLDER_SUMMARY_CLASS (camel_mbox_summary_parent_class)->info_set_flags(mi, flags, set);
}
#endif

static void
camel_mbox_summary_class_init (CamelMboxSummaryClass *class)
{
	CamelFolderSummaryClass *folder_summary_class;
	CamelLocalSummaryClass *local_summary_class;

	folder_summary_class = CAMEL_FOLDER_SUMMARY_CLASS (class);
	folder_summary_class->message_info_size = sizeof (CamelMboxMessageInfo);
	folder_summary_class->content_info_size = sizeof (CamelMboxMessageContentInfo);
	folder_summary_class->summary_header_load = summary_header_load;
	folder_summary_class->summary_header_save = summary_header_save;
	folder_summary_class->summary_header_from_db = summary_header_from_db;
	folder_summary_class->summary_header_to_db = summary_header_to_db;
	folder_summary_class->message_info_from_db = message_info_from_db;
	folder_summary_class->message_info_to_db = message_info_to_db;
	folder_summary_class->message_info_new_from_header  = message_info_new_from_header;
	folder_summary_class->message_info_new_from_parser = message_info_new_from_parser;
	folder_summary_class->message_info_migrate = message_info_migrate;
	folder_summary_class->info_set_user_flag = mbox_info_set_user_flag;
	folder_summary_class->info_set_user_tag = mbox_info_set_user_tag;
#ifdef STATUS_PINE
	folder_summary_class->info_set_flags = mbox_info_set_flags;
#endif

	local_summary_class = CAMEL_LOCAL_SUMMARY_CLASS (class);
	local_summary_class->encode_x_evolution = mbox_summary_encode_x_evolution;
	local_summary_class->check = mbox_summary_check;
	local_summary_class->sync = mbox_summary_sync;
#ifdef STATUS_PINE
	local_summary_class->add = mbox_summary_add;
#endif

	class->sync_quick = mbox_summary_sync_quick;
	class->sync_full = mbox_summary_sync_full;
}

static void
camel_mbox_summary_init (CamelMboxSummary *mbox_summary)
{
	CamelFolderSummary *folder_summary;

	folder_summary = CAMEL_FOLDER_SUMMARY (mbox_summary);

	/* and a unique file version */
	folder_summary->version += CAMEL_MBOX_SUMMARY_VERSION;
}

/**
 * camel_mbox_summary_new:
 *
 * Create a new CamelMboxSummary object.
 *
 * Returns: A new CamelMboxSummary widget.
 **/
CamelMboxSummary *
camel_mbox_summary_new(struct _CamelFolder *folder, const gchar *filename, const gchar *mbox_name, CamelIndex *index)
{
	CamelMboxSummary *new;

	new = g_object_new (CAMEL_TYPE_MBOX_SUMMARY, NULL);
	((CamelFolderSummary *)new)->folder = folder;
	if (folder) {
		CamelFolderSummary *summary = (CamelFolderSummary *)new;
		CamelStore *parent_store;

		parent_store = camel_folder_get_parent_store (folder);

		/* Set the functions for db sorting */
		camel_db_set_collate (parent_store->cdb_r, "bdata", "mbox_frompos_sort", (CamelDBCollate)camel_local_frompos_sort);
		summary->sort_by = "bdata";
		summary->collate = "mbox_frompos_sort";

	}
	camel_local_summary_construct((CamelLocalSummary *)new, filename, mbox_name, index);
	return new;
}

void camel_mbox_summary_xstatus(CamelMboxSummary *mbs, gint state)
{
	mbs->xstatus = state;
}

static gchar *
mbox_summary_encode_x_evolution (CamelLocalSummary *cls, const CamelLocalMessageInfo *mi)
{
	const gchar *p, *uidstr;
	guint32 uid;

	/* This is busted, it is supposed to encode ALL DATA */
	p = uidstr = camel_message_info_uid(mi);
	while (*p && isdigit(*p))
		p++;

	if (*p == 0 && sscanf(uidstr, "%u", &uid) == 1) {
		return g_strdup_printf("%08x-%04x", uid, mi->info.flags & 0xffff);
	} else {
		return g_strdup_printf("%s-%04x", uidstr, mi->info.flags & 0xffff);
	}
}

static gint
summary_header_from_db (CamelFolderSummary *s, struct _CamelFIRecord *fir)
{
	CamelMboxSummary *mbs = CAMEL_MBOX_SUMMARY(s);
	gchar *part;

	CAMEL_FOLDER_SUMMARY_CLASS (camel_mbox_summary_parent_class)->summary_header_from_db(s, fir);

	part = fir->bdata;
	if (part) {
		EXTRACT_DIGIT(mbs->version)
		EXTRACT_DIGIT(mbs->folder_size)
	}

	return 0;
}

static gint
summary_header_load(CamelFolderSummary *s, FILE *in)
{
	CamelMboxSummary *mbs = CAMEL_MBOX_SUMMARY(s);

	if (CAMEL_FOLDER_SUMMARY_CLASS (camel_mbox_summary_parent_class)->summary_header_load(s, in) == -1)
		return -1;

	/* legacy version */
	if (s->version == 0x120c)
		return camel_file_util_decode_uint32(in, (guint32 *) &mbs->folder_size);

	/* version 1 */
	if (camel_file_util_decode_fixed_int32(in, (gint32 *) &mbs->version) == -1
	    || camel_file_util_decode_gsize(in, &mbs->folder_size) == -1)
		return -1;

	return 0;
}

static CamelFIRecord *
summary_header_to_db (CamelFolderSummary *s, GError **error)
{
	CamelFolderSummaryClass *folder_summary_class;
	CamelMboxSummary *mbs = CAMEL_MBOX_SUMMARY(s);
	struct _CamelFIRecord *fir;
	gchar *tmp;

	/* Chain up to parent's summary_header_to_db() method. */
	folder_summary_class = CAMEL_FOLDER_SUMMARY_CLASS (camel_mbox_summary_parent_class);
	fir = folder_summary_class->summary_header_to_db (s, error);
	if (fir) {
		tmp = fir->bdata;
		fir->bdata = g_strdup_printf ("%s %d %d", tmp ? tmp : "", CAMEL_MBOX_SUMMARY_VERSION, (gint) mbs->folder_size);
		g_free (tmp);
	}

	return fir;
}

static gint
summary_header_save(CamelFolderSummary *s, FILE *out)
{
	CamelMboxSummary *mbs = CAMEL_MBOX_SUMMARY(s);

	if (CAMEL_FOLDER_SUMMARY_CLASS (camel_mbox_summary_parent_class)->summary_header_save(s, out) == -1)
		return -1;

	camel_file_util_encode_fixed_int32(out, CAMEL_MBOX_SUMMARY_VERSION);

	return camel_file_util_encode_gsize(out, mbs->folder_size);
}

static CamelMessageInfo *
message_info_new_from_header(CamelFolderSummary *s, struct _camel_header_raw *h)
{
	CamelMboxMessageInfo *mi;
	CamelMboxSummary *mbs = (CamelMboxSummary *)s;

	mi = (CamelMboxMessageInfo *)CAMEL_FOLDER_SUMMARY_CLASS (camel_mbox_summary_parent_class)->message_info_new_from_header(s, h);
	if (mi) {
		const gchar *xev, *uid;
		CamelMboxMessageInfo *info = NULL;
		gint add = 0;	/* bitmask of things to add, 1 assign uid, 2, just add as new, 4 = recent */
#ifdef STATUS_PINE
		const gchar *status = NULL, *xstatus = NULL;
		guint32 flags = 0;

		if (mbs->xstatus) {
			/* check for existance of status & x-status headers */
			status = camel_header_raw_find(&h, "Status", NULL);
			if (status)
				flags = decode_status(status);
			xstatus = camel_header_raw_find(&h, "X-Status", NULL);
			if (xstatus)
				flags |= decode_status(xstatus);
		}
#endif
		/* if we have an xev header, use it, else assign a new one */
		xev = camel_header_raw_find(&h, "X-Evolution", NULL);
		if (xev != NULL
		    && camel_local_summary_decode_x_evolution((CamelLocalSummary *)s, xev, &mi->info) == 0) {
			uid = camel_message_info_uid(mi);
			d(printf("found valid x-evolution: %s\n", uid));
			/* If one is there, it should be there already */
			info = (CamelMboxMessageInfo *) camel_folder_summary_peek_info (s, uid);
			if (info) {
				if ((info->info.info.flags & CAMEL_MESSAGE_FOLDER_NOTSEEN)) {
					info->info.info.flags &= ~CAMEL_MESSAGE_FOLDER_NOTSEEN;
					camel_message_info_free(mi);
					mi = info;
				} else {
					add = 7;
					d(printf("seen '%s' before, adding anew\n", uid));
					camel_message_info_free(info);
				}
			} else {
				add = 2;
				d(printf("but isn't present in summary\n"));
			}
		} else {
			d(printf("didn't find x-evolution\n"));
			add = 7;
		}

		if (add&1) {
			mi->info.info.flags |= CAMEL_MESSAGE_FOLDER_FLAGGED | CAMEL_MESSAGE_FOLDER_NOXEV;
			camel_pstring_free (mi->info.info.uid);
			mi->info.info.uid = camel_pstring_add(camel_folder_summary_next_uid_string(s), TRUE);
		} else {
			camel_folder_summary_set_uid(s, strtoul(camel_message_info_uid(mi), NULL, 10));
		}
#ifdef STATUS_PINE
		if (mbs->xstatus && add&2) {
			/* use the status as the flags when we read it the first time */
			if (status)
				mi->info.info.flags = (mi->info.info.flags & ~(STATUS_STATUS)) | (flags & STATUS_STATUS);
			if (xstatus)
				mi->info.info.flags = (mi->info.info.flags & ~(STATUS_XSTATUS)) | (flags & STATUS_XSTATUS);
		}
#endif
		if (mbs->changes) {
			if (add&2)
				camel_folder_change_info_add_uid(mbs->changes, camel_message_info_uid(mi));
			if ((add&4) && status == NULL)
				camel_folder_change_info_recent_uid(mbs->changes, camel_message_info_uid(mi));
		}

		mi->frompos = -1;
	}

	return (CamelMessageInfo *)mi;
}

static CamelMessageInfo *
message_info_new_from_parser(CamelFolderSummary *s, CamelMimeParser *mp)
{
	CamelMessageInfo *mi;

	mi = CAMEL_FOLDER_SUMMARY_CLASS (camel_mbox_summary_parent_class)->message_info_new_from_parser(s, mp);
	if (mi) {
		CamelMboxMessageInfo *mbi = (CamelMboxMessageInfo *)mi;

		mbi->frompos = camel_mime_parser_tell_start_from(mp);
	}

	return mi;
}

static CamelMessageInfo *
message_info_from_db(CamelFolderSummary *s, struct _CamelMIRecord *mir)
{
	CamelMessageInfo *mi;
	gchar *part;

	mi = CAMEL_FOLDER_SUMMARY_CLASS (camel_mbox_summary_parent_class)->message_info_from_db(s, mir);

	if (mi) {
		CamelMboxMessageInfo *mbi = (CamelMboxMessageInfo *)mi;
		part = mir->bdata;
		if (part) {
			EXTRACT_FIRST_DIGIT (mbi->frompos)
		}
	}

	return mi;
}

static CamelMessageInfo *
message_info_migrate (CamelFolderSummary *s, FILE *in)
{
	CamelMessageInfo *mi;

	io(printf("loading mbox message info\n"));

	mi = CAMEL_FOLDER_SUMMARY_CLASS (camel_mbox_summary_parent_class)->message_info_migrate (s, in);
	if (mi) {
		off_t ot = -1;
		CamelMboxMessageInfo *mbi = (CamelMboxMessageInfo *)mi;

		if (camel_file_util_decode_off_t(in, &ot) == -1)
			goto error;

		mbi->frompos = (goffset) ot;
	}

	return mi;
error:
	camel_message_info_free(mi);
	return NULL;
}

static struct _CamelMIRecord *
message_info_to_db(CamelFolderSummary *s, CamelMessageInfo *info)
{
	CamelMboxMessageInfo *mbi = (CamelMboxMessageInfo *)info;
	struct _CamelMIRecord *mir;

	mir = CAMEL_FOLDER_SUMMARY_CLASS (camel_mbox_summary_parent_class)->message_info_to_db(s, info);
	mir->bdata = g_strdup_printf("%" G_GOFFSET_FORMAT, mbi->frompos);

	return mir;
}

/* like summary_rebuild, but also do changeinfo stuff (if supplied) */
static gint
summary_update(CamelLocalSummary *cls, goffset offset, CamelFolderChangeInfo *changeinfo, GError **error)
{
	gint i, count;
	CamelFolderSummary *s = (CamelFolderSummary *)cls;
	CamelMboxSummary *mbs = (CamelMboxSummary *)cls;
	CamelMimeParser *mp;
	CamelMboxMessageInfo *mi;
	CamelStore *parent_store;
	const gchar *full_name;
	gint fd;
	gint ok = 0;
	struct stat st;
	goffset size = 0;
	GSList *del = NULL;

	d(printf("Calling summary update, from pos %d\n", (gint)offset));

	cls->index_force = FALSE;

	camel_operation_start(NULL, _("Storing folder"));

	fd = g_open(cls->folder_path, O_LARGEFILE | O_RDONLY | O_BINARY, 0);
	if (fd == -1) {
		d(printf("%s failed to open: %s\n", cls->folder_path, g_strerror (errno)));
		g_set_error (
			error, G_IO_ERROR,
			g_io_error_from_errno (errno),
			_("Could not open folder: %s: %s"),
			cls->folder_path, g_strerror (errno));
		camel_operation_end(NULL);
		return -1;
	}

	if (fstat(fd, &st) == 0)
		size = st.st_size;

	mp = camel_mime_parser_new();
	camel_mime_parser_init_with_fd(mp, fd);
	camel_mime_parser_scan_from(mp, TRUE);
	camel_mime_parser_seek(mp, offset, SEEK_SET);

	if (offset > 0) {
		if (camel_mime_parser_step(mp, NULL, NULL) == CAMEL_MIME_PARSER_STATE_FROM
		    && camel_mime_parser_tell_start_from(mp) == offset) {
			camel_mime_parser_unstep(mp);
		} else {
			g_warning("The next message didn't start where I expected, building summary from start");
			camel_mime_parser_drop_step(mp);
			offset = 0;
			camel_mime_parser_seek(mp, offset, SEEK_SET);
		}
	}

	/* we mark messages as to whether we've seen them or not.
	   If we're not starting from the start, we must be starting
	   from the old end, so everything must be treated as new */
	camel_folder_summary_prepare_fetch_all (s, NULL);
	count = camel_folder_summary_count(s);
	for (i=0;i<count;i++) {
		mi = (CamelMboxMessageInfo *)camel_folder_summary_index(s, i);
		if (offset == 0)
			mi->info.info.flags |= CAMEL_MESSAGE_FOLDER_NOTSEEN;
		else
			mi->info.info.flags &= ~CAMEL_MESSAGE_FOLDER_NOTSEEN;
		camel_message_info_free(mi);
	}
	mbs->changes = changeinfo;

	while (camel_mime_parser_step(mp, NULL, NULL) == CAMEL_MIME_PARSER_STATE_FROM) {
		CamelMessageInfo *info;
		goffset pc = camel_mime_parser_tell_start_from (mp) + 1;

		camel_operation_progress (NULL, (gint) (((gfloat) pc / size) * 100));

		info = camel_folder_summary_add_from_parser(s, mp);
		if (info == NULL) {
			gchar *pos_str;

			/* XXX Gettext does not understand G_GINT64_FORMAT
			 *     when used directly in a translatable string,
			 *     so we have to pre-format the position value
			 *     for use in the error message. */
			pos_str = g_strdup_printf (
				"%" G_GINT64_FORMAT, (gint64)
				camel_mime_parser_tell (mp));
			g_set_error (
				error, CAMEL_ERROR, CAMEL_ERROR_GENERIC,
				_("Fatal mail parser error near position %s "
				  "in folder %s"), pos_str, cls->folder_path);
			g_free (pos_str);
			ok = -1;
			break;
		}

		g_assert(camel_mime_parser_step(mp, NULL, NULL) == CAMEL_MIME_PARSER_STATE_FROM_END);
	}

	g_object_unref (CAMEL_OBJECT (mp));

	count = camel_folder_summary_count(s);
	for (i=0;i<count;i++) {
		mi = (CamelMboxMessageInfo *)camel_folder_summary_index(s, i);
		/* must've dissapeared from the file? */
		if (!mi || mi->info.info.flags & CAMEL_MESSAGE_FOLDER_NOTSEEN) {
			gchar *uid;

			if (mi)
				uid = g_strdup (camel_message_info_uid (mi));
			else
				uid = camel_folder_summary_uid_from_index (s, i);

			if (!uid) {
				g_debug ("%s: didn't get uid at %d of %d (%d)", G_STRFUNC, i, count, camel_folder_summary_count (s));
				continue;
			}

			d(printf("uid '%s' vanished, removing", uid));
			if (changeinfo)
				camel_folder_change_info_remove_uid (changeinfo, uid);
			del = g_slist_prepend (del, (gpointer) camel_pstring_strdup (uid));
			camel_folder_summary_remove_index_fast (s, i);
			count--;
			i--;
			g_free (uid);
		}
		if (mi)
			camel_message_info_free (mi);
	}

	/* Delete all in one transaction */
	full_name = camel_folder_get_full_name (s->folder);
	parent_store = camel_folder_get_parent_store (s->folder);
	camel_db_delete_uids (parent_store->cdb_w, full_name, del, NULL);
	g_slist_foreach (del, (GFunc) camel_pstring_free, NULL);
	g_slist_free (del);

	mbs->changes = NULL;

	/* update the file size/mtime in the summary */
	if (ok != -1) {
		if (g_stat(cls->folder_path, &st) == 0) {
			camel_folder_summary_touch(s);
			mbs->folder_size = st.st_size;
			s->time = st.st_mtime;
		}
	}

	camel_operation_end(NULL);

	return ok;
}

static gint
mbox_summary_check(CamelLocalSummary *cls, CamelFolderChangeInfo *changes, GError **error)
{
	CamelMboxSummary *mbs = (CamelMboxSummary *)cls;
	CamelFolderSummary *s = (CamelFolderSummary *)cls;
	struct stat st;
	gint ret = 0;
	gint i, count;

	d(printf("Checking summary\n"));

	/* check if the summary is up-to-date */
	if (g_stat(cls->folder_path, &st) == -1) {
		camel_folder_summary_clear(s);
		g_set_error (
			error, G_IO_ERROR,
			g_io_error_from_errno (errno),
			_("Cannot check folder: %s: %s"),
			cls->folder_path, g_strerror (errno));
		return -1;
	}

	if (cls->check_force)
		mbs->folder_size = 0;
	cls->check_force = 0;

	if (st.st_size == 0) {
		/* empty?  No need to scan at all */
		d(printf("Empty mbox, clearing summary\n"));
		camel_folder_summary_prepare_fetch_all (s, NULL);
		count= camel_folder_summary_count(s);
		for (i=0;i<count;i++) {
			CamelMessageInfo *info = camel_folder_summary_index(s, i);

			if (info) {
				camel_folder_change_info_remove_uid(changes, camel_message_info_uid(info));
				camel_message_info_free(info);
			}
		}
		camel_folder_summary_clear(s);
		ret = 0;
	} else {
		/* is the summary uptodate? */
		if (st.st_size != mbs->folder_size || st.st_mtime != s->time) {
			if (mbs->folder_size < st.st_size) {
				/* this will automatically rescan from 0 if there is a problem */
				d(printf("folder grew, attempting to rebuild from %d\n", mbs->folder_size));
				ret = summary_update(cls, mbs->folder_size, changes, error);
			} else {
				d(printf("folder shrank!  rebuilding from start\n"));
				 ret = summary_update(cls, 0, changes, error);
			}
		} else {
			d(printf("Folder unchanged, do nothing\n"));
		}
	}

	/* FIXME: move upstream? */

	if (ret != -1) {
		if (mbs->folder_size != st.st_size || s->time != st.st_mtime) {
			mbs->folder_size = st.st_size;
			s->time = st.st_mtime;
			camel_folder_summary_touch(s);
		}
	}

	return ret;
}

/* perform a full sync */
static gint
mbox_summary_sync_full(CamelMboxSummary *mbs, gboolean expunge, CamelFolderChangeInfo *changeinfo, GError **error)
{
	CamelLocalSummary *cls = (CamelLocalSummary *)mbs;
	gint fd = -1, fdout = -1;
	gchar *tmpname = NULL;
	guint32 flags = (expunge?1:0);

	d(printf("performing full summary/sync\n"));

	camel_operation_start(NULL, _("Storing folder"));

	fd = g_open(cls->folder_path, O_LARGEFILE | O_RDONLY | O_BINARY, 0);
	if (fd == -1) {
		g_set_error (
			error, G_IO_ERROR,
			g_io_error_from_errno (errno),
			_("Could not open file: %s: %s"),
			cls->folder_path, g_strerror (errno));
		camel_operation_end(NULL);
		return -1;
	}

	tmpname = g_alloca (strlen (cls->folder_path) + 5);
	sprintf (tmpname, "%s.tmp", cls->folder_path);
	d(printf("Writing temporary file to %s\n", tmpname));
	fdout = g_open(tmpname, O_LARGEFILE|O_WRONLY|O_CREAT|O_TRUNC|O_BINARY, 0600);
	if (fdout == -1) {
		g_set_error (
			error, G_IO_ERROR,
			g_io_error_from_errno (errno),
			_("Cannot open temporary mailbox: %s"),
			g_strerror (errno));
		goto error;
	}

	if (camel_mbox_summary_sync_mbox((CamelMboxSummary *)cls, flags, changeinfo, fd, fdout, error) == -1)
		goto error;

	d(printf("Closing folders\n"));

	if (close(fd) == -1) {
		g_warning("Cannot close source folder: %s", g_strerror (errno));
		g_set_error (
			error, G_IO_ERROR,
			g_io_error_from_errno (errno),
			_("Could not close source folder %s: %s"),
			cls->folder_path, g_strerror (errno));
		fd = -1;
		goto error;
	}

	if (close(fdout) == -1) {
		g_warning("Cannot close temporary folder: %s", g_strerror (errno));
		g_set_error (
			error, G_IO_ERROR,
			g_io_error_from_errno (errno),
			_("Could not close temporary folder: %s"),
			g_strerror (errno));
		fdout = -1;
		goto error;
	}

	/* this should probably either use unlink/link/unlink, or recopy over
	   the original mailbox, for various locking reasons/etc */
#ifdef G_OS_WIN32
	if (g_file_test(cls->folder_path,G_FILE_TEST_IS_REGULAR) && g_remove(cls->folder_path) == -1)
		g_warning ("Cannot remove %s: %s", cls->folder_path, g_strerror (errno));
#endif
	if (g_rename(tmpname, cls->folder_path) == -1) {
		g_warning("Cannot rename folder: %s", g_strerror (errno));
		g_set_error (
			error, G_IO_ERROR,
			g_io_error_from_errno (errno),
			_("Could not rename folder: %s"),
			g_strerror (errno));
		goto error;
	}
	tmpname = NULL;

	camel_operation_end(NULL);

	return 0;
 error:
	if (fd != -1)
		close(fd);

	if (fdout != -1)
		close(fdout);

	if (tmpname)
		g_unlink(tmpname);

	camel_operation_end(NULL);

	return -1;
}

static gint
cms_sort_frompos (gpointer a, gpointer b, gpointer data)
{
	CamelFolderSummary *summary = (CamelFolderSummary *)data;
	CamelMboxMessageInfo *info1, *info2;
	gint ret = 0;

	/* Things are in memory already. Sorting speeds up syncing, if things are sorted by from pos. */
	info1 = (CamelMboxMessageInfo *)camel_folder_summary_uid (summary, *(gchar **)a);
	info2 = (CamelMboxMessageInfo *)camel_folder_summary_uid (summary, *(gchar **)b);

	if (info1->frompos > info2->frompos)
		ret = 1;
	else if  (info1->frompos < info2->frompos)
		ret = -1;
	else
		ret = 0;
	camel_message_info_free (info1);
	camel_message_info_free (info2);

	return ret;

}

/* perform a quick sync - only system flags have changed */
static gint
mbox_summary_sync_quick(CamelMboxSummary *mbs, gboolean expunge, CamelFolderChangeInfo *changeinfo, GError **error)
{
	CamelLocalSummary *cls = (CamelLocalSummary *)mbs;
	CamelFolderSummary *s = (CamelFolderSummary *)mbs;
	CamelMimeParser *mp = NULL;
	gint i;
	CamelMboxMessageInfo *info = NULL;
	gint fd = -1, pfd;
	gchar *xevnew, *xevtmp;
	const gchar *xev;
	gint len;
	goffset lastpos;
	GPtrArray *summary = NULL;

	d(printf("Performing quick summary sync\n"));

	camel_operation_start(NULL, _("Storing folder"));

	fd = g_open(cls->folder_path, O_LARGEFILE|O_RDWR|O_BINARY, 0);
	if (fd == -1) {
		g_set_error (
			error, G_IO_ERROR,
			g_io_error_from_errno (errno),
			_("Could not open file: %s: %s"),
			cls->folder_path, g_strerror (errno));

		camel_operation_end(NULL);
		return -1;
	}

	/* need to dup since mime parser closes its fd once it is finalized */
	pfd = dup(fd);
	if (pfd == -1) {
		g_set_error (
			error, G_IO_ERROR,
			g_io_error_from_errno (errno),
			_("Could not store folder: %s"),
			g_strerror(errno));
		close(fd);
		return -1;
	}

	mp = camel_mime_parser_new();
	camel_mime_parser_scan_from(mp, TRUE);
	camel_mime_parser_scan_pre_from(mp, TRUE);
	camel_mime_parser_init_with_fd(mp, pfd);

	camel_folder_summary_lock (s, CAMEL_FOLDER_SUMMARY_SUMMARY_LOCK);
	/* Sync only the changes */
	summary = camel_folder_summary_get_changed ((CamelFolderSummary *)mbs);
	if (summary->len)
		g_ptr_array_sort_with_data (summary, (GCompareDataFunc)cms_sort_frompos, (gpointer) mbs);

	for (i = 0; i < summary->len; i++) {
		gint xevoffset;
		gint pc = (i+1)*100/summary->len;

		camel_operation_progress(NULL, pc);

		info = (CamelMboxMessageInfo *)camel_folder_summary_uid(s, summary->pdata[i]);

		d(printf("Checking message %s %08x\n", camel_message_info_uid(info), ((CamelMessageInfoBase *)info)->flags));

		if ((info->info.info.flags & CAMEL_MESSAGE_FOLDER_FLAGGED) == 0) {
			camel_message_info_free((CamelMessageInfo *)info);
			info = NULL;
			continue;
		}

		d(printf("Updating message %s: %d\n", camel_message_info_uid(info), (gint)info->frompos));

		camel_mime_parser_seek(mp, info->frompos, SEEK_SET);

		if (camel_mime_parser_step(mp, NULL, NULL) != CAMEL_MIME_PARSER_STATE_FROM) {
			g_set_error (
				error, CAMEL_ERROR, CAMEL_ERROR_GENERIC,
				_("MBOX file is corrupted, please fix it. (Expected a From line, but didn't get it.)"));
			goto error;
		}

		if (camel_mime_parser_tell_start_from(mp) != info->frompos) {
			g_warning("Didn't get the next message where I expected (%d) got %d instead",
				  (gint)info->frompos, (gint)camel_mime_parser_tell_start_from(mp));
			g_set_error (
				error, CAMEL_ERROR, CAMEL_ERROR_GENERIC,
				_("Summary and folder mismatch, even after a sync"));
			goto error;
		}

		if (camel_mime_parser_step(mp, NULL, NULL) == CAMEL_MIME_PARSER_STATE_FROM_END) {
			g_warning("camel_mime_parser_step failed (2)");
			goto error;
		}

		xev = camel_mime_parser_header(mp, "X-Evolution", &xevoffset);
		if (xev == NULL || camel_local_summary_decode_x_evolution(cls, xev, NULL) == -1) {
			g_warning("We're supposed to have a valid x-ev header, but we dont");
			goto error;
		}
		xevnew = camel_local_summary_encode_x_evolution(cls, &info->info);
		/* SIGH: encode_param_list is about the only function which folds headers by itself.
		   This should be fixed somehow differently (either parser doesn't fold headers,
		   or param_list doesn't, or something */
		xevtmp = camel_header_unfold(xevnew);
		/* the raw header contains a leading ' ', so (dis)count that too */
		if (strlen(xev)-1 != strlen(xevtmp)) {
			g_free(xevnew);
			g_free(xevtmp);
			g_warning("Hmm, the xev headers shouldn't have changed size, but they did");
			goto error;
		}
		g_free(xevtmp);

		/* we write out the xevnew string, assuming its been folded identically to the original too! */

		lastpos = lseek(fd, 0, SEEK_CUR);
		lseek(fd, xevoffset+strlen("X-Evolution: "), SEEK_SET);
		do {
			len = write(fd, xevnew, strlen(xevnew));
		} while (len == -1 && errno == EINTR);
		lseek(fd, lastpos, SEEK_SET);
		g_free(xevnew);

		camel_mime_parser_drop_step(mp);
		camel_mime_parser_drop_step(mp);

		info->info.info.flags &= 0xffff;
		info->info.info.dirty = TRUE;
		camel_message_info_free((CamelMessageInfo *)info);
		info = NULL;
	}

	d(printf("Closing folders\n"));

	if (close(fd) == -1) {
		g_warning ("Cannot close source folder: %s", g_strerror (errno));
		g_set_error (
			error, G_IO_ERROR,
			g_io_error_from_errno (errno),
			_("Could not close source folder %s: %s"),
			cls->folder_path, g_strerror (errno));
		fd = -1;
		goto error;
	}

	g_ptr_array_foreach (summary, (GFunc) camel_pstring_free, NULL);
	g_ptr_array_free (summary, TRUE);
	g_object_unref (mp);

	camel_operation_end(NULL);
	camel_folder_summary_unlock (s, CAMEL_FOLDER_SUMMARY_SUMMARY_LOCK);

	return 0;
 error:
	g_ptr_array_foreach (summary, (GFunc) camel_pstring_free, NULL);
	g_ptr_array_free (summary, TRUE);
	if (fd != -1)
		close(fd);
	if (mp)
		g_object_unref (mp);
	if (info)
		camel_message_info_free((CamelMessageInfo *)info);

	camel_operation_end(NULL);
	camel_folder_summary_unlock (s, CAMEL_FOLDER_SUMMARY_SUMMARY_LOCK);

	return -1;
}

static gint
mbox_summary_sync(CamelLocalSummary *cls, gboolean expunge, CamelFolderChangeInfo *changeinfo, GError **error)
{
	struct stat st;
	CamelMboxSummary *mbs = (CamelMboxSummary *)cls;
	CamelFolderSummary *s = (CamelFolderSummary *)cls;
	CamelStore *parent_store;
	const gchar *full_name;
	gint i;
	gint quick = TRUE, work=FALSE;
	gint ret;
	GPtrArray *summary = NULL;

	/* first, sync ourselves up, just to make sure */
	if (camel_local_summary_check(cls, changeinfo, error) == -1)
		return -1;

	full_name = camel_folder_get_full_name (s->folder);
	parent_store = camel_folder_get_parent_store (s->folder);

	/* Sync only the changes */

	summary = camel_folder_summary_get_changed ((CamelFolderSummary *)mbs);
	for (i=0; i<summary->len; i++) {
		CamelMboxMessageInfo *info = (CamelMboxMessageInfo *)camel_folder_summary_uid(s, summary->pdata[i]);

		if ((expunge && (info->info.info.flags & CAMEL_MESSAGE_DELETED)) ||
		    (info->info.info.flags & (CAMEL_MESSAGE_FOLDER_NOXEV|CAMEL_MESSAGE_FOLDER_XEVCHANGE)))
			quick = FALSE;
		else
			work |= (info->info.info.flags & CAMEL_MESSAGE_FOLDER_FLAGGED) != 0;
		camel_message_info_free(info);
	}

	g_ptr_array_foreach (summary, (GFunc) camel_pstring_free, NULL);
	g_ptr_array_free (summary, TRUE);

	if (quick && expunge) {
		guint32 dcount =0;

		if (camel_db_count_deleted_message_info (parent_store->cdb_w, full_name, &dcount, error) == -1)
			return -1;
		if (dcount)
			quick = FALSE;
	}

	/* yuck i hate this logic, but its to simplify the 'all ok, update summary' and failover cases */
	ret = -1;
	if (quick) {
		if (work) {
			ret = CAMEL_MBOX_SUMMARY_GET_CLASS (cls)->sync_quick (mbs, expunge, changeinfo, NULL);
			if (ret == -1)
				g_warning("failed a quick-sync, trying a full sync");
		} else {
			ret = 0;
		}
	}

	if (ret == -1)
		ret = CAMEL_MBOX_SUMMARY_GET_CLASS (cls)->sync_full (mbs, expunge, changeinfo, error);
	if (ret == -1)
		return -1;

	if (g_stat(cls->folder_path, &st) == -1) {
		g_set_error (
			error, G_IO_ERROR,
			g_io_error_from_errno (errno),
			_("Unknown error: %s"), g_strerror (errno));
		return -1;
	}

	if (mbs->folder_size != st.st_size || s->time != st.st_mtime) {
		s->time = st.st_mtime;
		mbs->folder_size = st.st_size;
		camel_folder_summary_touch(s);
	}

	return CAMEL_LOCAL_SUMMARY_CLASS (camel_mbox_summary_parent_class)->sync(cls, expunge, changeinfo, error);
}

gint
camel_mbox_summary_sync_mbox(CamelMboxSummary *cls, guint32 flags, CamelFolderChangeInfo *changeinfo, gint fd, gint fdout, GError **error)
{
	CamelMboxSummary *mbs = (CamelMboxSummary *)cls;
	CamelFolderSummary *s = (CamelFolderSummary *)mbs;
	CamelMimeParser *mp = NULL;
	CamelStore *parent_store;
	const gchar *full_name;
	gint i, count;
	CamelMboxMessageInfo *info = NULL;
	gchar *buffer, *xevnew = NULL;
	gsize len;
	const gchar *fromline;
	gint lastdel = FALSE;
	gboolean touched = FALSE;
	GSList *del=NULL;
#ifdef STATUS_PINE
	gchar statnew[8], xstatnew[8];
#endif

	d(printf("performing full summary/sync\n"));

	/* need to dup this because the mime-parser owns the fd after we give it to it */
	fd = dup(fd);
	if (fd == -1) {
		g_set_error (
			error, G_IO_ERROR,
			g_io_error_from_errno (errno),
			_("Could not store folder: %s"),
			g_strerror (errno));
		return -1;
	}

	mp = camel_mime_parser_new();
	camel_mime_parser_scan_from(mp, TRUE);
	camel_mime_parser_scan_pre_from(mp, TRUE);
	camel_mime_parser_init_with_fd(mp, fd);

	camel_folder_summary_lock (s, CAMEL_FOLDER_SUMMARY_SUMMARY_LOCK);
	camel_folder_summary_prepare_fetch_all (s, NULL);
	count = camel_folder_summary_count(s);
	for (i = 0; i < count; i++) {
		gint pc = (i + 1) * 100 / count;

		camel_operation_progress(NULL, pc);

		info = (CamelMboxMessageInfo *)camel_folder_summary_index(s, i);

		if (!info)
			continue;

		d(printf("Looking at message %s\n", camel_message_info_uid(info)));

		d(printf("seeking (%s) to %d\n", ((CamelMessageInfo *) info)->uid, (gint)info->frompos));
		if (lastdel)
			camel_mime_parser_seek(mp, info->frompos, SEEK_SET);

		if (camel_mime_parser_step(mp, &buffer, &len) != CAMEL_MIME_PARSER_STATE_FROM) {
			g_set_error (
				error, CAMEL_ERROR, CAMEL_ERROR_GENERIC,
				_("MBOX file is corrupted, please fix it. (Expected a From line, but didn't get it.)"));
			goto error;
		}

		if (camel_mime_parser_tell_start_from(mp) != info->frompos) {
			g_warning("Didn't get the next message where I expected (%d) got %d instead",
				  (gint)info->frompos, (gint)camel_mime_parser_tell_start_from(mp));
			g_set_error (
				error, CAMEL_ERROR, CAMEL_ERROR_GENERIC,
				_("Summary and folder mismatch, even after a sync"));
			goto error;
		}

		lastdel = FALSE;
		if ((flags&1) && info->info.info.flags & CAMEL_MESSAGE_DELETED) {
			const gchar *uid = camel_message_info_uid(info);
			guint32 flags = camel_message_info_flags(info);
			gint read, junk;
			d(printf("Deleting %s\n", uid));

			if (((CamelLocalSummary *)cls)->index)
				camel_index_delete_name(((CamelLocalSummary *)cls)->index, uid);

			/* remove it from the change list */
			junk = flags & CAMEL_MESSAGE_JUNK;
			read = flags & CAMEL_MESSAGE_SEEN;
			s->saved_count--;
			if (junk)
				s->junk_count--;
			if (!read)
				s->unread_count--;
			s->deleted_count--;
			camel_folder_change_info_remove_uid(changeinfo, uid);
			camel_folder_summary_remove_index_fast (s, i);
			del = g_slist_prepend (del, (gpointer) camel_pstring_strdup(uid));
			camel_message_info_free((CamelMessageInfo *)info);
			count--;
			i--;
			info = NULL;
			lastdel = TRUE;
			touched = TRUE;
		} else {
			/* otherwise, the message is staying, copy its From_ line across */
#if 0
			if (i>0)
				write(fdout, "\n", 1);
#endif
			info->frompos = lseek(fdout, 0, SEEK_CUR);
			((CamelMessageInfo *)info)->dirty = TRUE;
			fromline = camel_mime_parser_from_line(mp);
			d(printf("Saving %s:%d\n", camel_message_info_uid(info), info->frompos));
			write(fdout, fromline, strlen(fromline));
		}

		if (info && info->info.info.flags & (CAMEL_MESSAGE_FOLDER_NOXEV | CAMEL_MESSAGE_FOLDER_FLAGGED)) {
			d(printf("Updating header for %s flags = %08x\n", camel_message_info_uid(info), info->info.flags));

			if (camel_mime_parser_step(mp, &buffer, &len) == CAMEL_MIME_PARSER_STATE_FROM_END) {
				g_warning("camel_mime_parser_step failed (2)");
				goto error;
			}

			xevnew = camel_local_summary_encode_x_evolution((CamelLocalSummary *)cls, &info->info);
#ifdef STATUS_PINE
			if (mbs->xstatus) {
				encode_status(info->info.info.flags & STATUS_STATUS, statnew);
				encode_status(info->info.info.flags & STATUS_XSTATUS, xstatnew);
				len = camel_local_summary_write_headers(fdout, camel_mime_parser_headers_raw(mp), xevnew, statnew, xstatnew);
			} else {
#endif
				len = camel_local_summary_write_headers(fdout, camel_mime_parser_headers_raw(mp), xevnew, NULL, NULL);
#ifdef STATUS_PINE
			}
#endif
			if (len == -1) {
				d(printf("Error writing to temporary mailbox\n"));
				g_set_error (
					error, G_IO_ERROR,
					g_io_error_from_errno (errno),
					_("Writing to temporary mailbox failed: %s"),
					g_strerror (errno));
				goto error;
			}
			info->info.info.flags &= 0xffff;
			g_free(xevnew);
			xevnew = NULL;
			camel_mime_parser_drop_step(mp);
		}

		camel_mime_parser_drop_step(mp);
		if (info) {
			d(printf("looking for message content to copy across from %d\n", (gint)camel_mime_parser_tell(mp)));
			while (camel_mime_parser_step(mp, &buffer, &len) == CAMEL_MIME_PARSER_STATE_PRE_FROM) {
				/*d(printf("copying mbox contents to temporary: '%.*s'\n", len, buffer));*/
				if (write(fdout, buffer, len) != len) {
					g_set_error (
						error, G_IO_ERROR,
						g_io_error_from_errno (errno),
						_("Writing to temporary mailbox failed: %s: %s"),
						((CamelLocalSummary *)cls)->folder_path,
						g_strerror (errno));
					goto error;
				}
			}

			if (write(fdout, "\n", 1) != 1) {
				g_set_error (
					error, G_IO_ERROR,
					g_io_error_from_errno (errno),
					_("Writing to temporary mailbox failed: %s"),
					g_strerror (errno));
				goto error;
			}

			d(printf("we are now at %d, from = %d\n", (gint)camel_mime_parser_tell(mp),
				 (gint)camel_mime_parser_tell_start_from(mp)));
			camel_mime_parser_unstep(mp);
			camel_message_info_free((CamelMessageInfo *)info);
			info = NULL;
		}
	}

	full_name = camel_folder_get_full_name (s->folder);
	parent_store = camel_folder_get_parent_store (s->folder);
	camel_db_delete_uids (parent_store->cdb_w, full_name, del, NULL);
	g_slist_foreach (del, (GFunc) camel_pstring_free, NULL);
	g_slist_free (del);

#if 0
	/* if last was deleted, append the \n we removed */
	if (lastdel && count > 0)
		write(fdout, "\n", 1);
#endif

	g_object_unref (mp);

	/* clear working flags */
	for (i=0; i<count; i++) {
		info = (CamelMboxMessageInfo *)camel_folder_summary_index(s, i);
		if (info) {
			if (info->info.info.flags & (CAMEL_MESSAGE_FOLDER_NOXEV|CAMEL_MESSAGE_FOLDER_FLAGGED|CAMEL_MESSAGE_FOLDER_XEVCHANGE)) {
				info->info.info.flags &= ~(CAMEL_MESSAGE_FOLDER_NOXEV
							   |CAMEL_MESSAGE_FOLDER_FLAGGED
							   |CAMEL_MESSAGE_FOLDER_XEVCHANGE);
				((CamelMessageInfo *)info)->dirty = TRUE;
				camel_folder_summary_touch(s);
			}
			camel_message_info_free((CamelMessageInfo *)info);
			info = NULL;
		}
	}

	if (touched)
		camel_folder_summary_header_save_to_db (s, NULL);

	camel_folder_summary_unlock (s, CAMEL_FOLDER_SUMMARY_SUMMARY_LOCK);

	return 0;
 error:
	g_free(xevnew);

	if (mp)
		g_object_unref (mp);
	if (info)
		camel_message_info_free((CamelMessageInfo *)info);

	camel_folder_summary_unlock (s, CAMEL_FOLDER_SUMMARY_SUMMARY_LOCK);

	return -1;
}

#ifdef STATUS_PINE
static CamelMessageInfo *
mbox_summary_add(CamelLocalSummary *cls, CamelMimeMessage *msg, const CamelMessageInfo *info, CamelFolderChangeInfo *ci, GError **error)
{
	CamelLocalSummaryClass *local_summary_class;
	CamelMboxMessageInfo *mi;

	/* Chain up to parent's add() method. */
	local_summary_class = CAMEL_LOCAL_SUMMARY_CLASS (camel_mbox_summary_parent_class);
	mi = (CamelMboxMessageInfo *) local_summary_class->add (
		cls, msg, info, ci, error);
	if (mi && ((CamelMboxSummary *)cls)->xstatus) {
		gchar status[8];

		/* we snoop and add status/x-status headers to suit */
		encode_status(mi->info.info.flags & STATUS_STATUS, status);
		camel_medium_set_header((CamelMedium *)msg, "Status", status);
		encode_status(mi->info.info.flags & STATUS_XSTATUS, status);
		camel_medium_set_header((CamelMedium *)msg, "X-Status", status);
	}

	return (CamelMessageInfo *)mi;
}

static struct {
	gchar tag;
	guint32 flag;
} status_flags[] = {
	{ 'F', CAMEL_MESSAGE_FLAGGED },
	{ 'A', CAMEL_MESSAGE_ANSWERED },
	{ 'D', CAMEL_MESSAGE_DELETED },
	{ 'R', CAMEL_MESSAGE_SEEN },
};

static void
encode_status(guint32 flags, gchar status[8])
{
	gsize i;
	gchar *p;

	p = status;
	for (i = 0; i < G_N_ELEMENTS (status_flags); i++)
		if (status_flags[i].flag & flags)
			*p++ = status_flags[i].tag;
	*p++ = 'O';
	*p = '\0';
}

static guint32
decode_status(const gchar *status)
{
	const gchar *p;
	guint32 flags = 0;
	gsize i;
	gchar c;

	p = status;
	while ((c = *p++)) {
		for (i = 0; i < G_N_ELEMENTS (status_flags); i++)
			if (status_flags[i].tag == c)
				flags |= status_flags[i].flag;
	}

	return flags;
}

#endif /* STATUS_PINE */
