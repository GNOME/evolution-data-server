/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  Authors: Jeffrey Stedfast <fejj@ximian.com>
 *	     Michael Zucchi <notzed@ximian.com>
 *
 *  Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
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
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>

#include <glib.h>
#include <glib/gi18n-lib.h>

#include "camel-db.h"
#include "camel-exception.h"
#include "camel-mime-message.h"
#include "camel-private.h"
#include "camel-store.h"
#include "camel-vee-store.h"
#include "camel-vtrash-folder.h"
#include "camel-string-utils.h"

/* Returns the class for a CamelFolder */
#define CF_CLASS(so) ((CamelFolderClass *)((CamelObject *)(so))->klass)

static struct {
	const gchar *full_name;
	const gchar *name;
	const gchar *expr;
	guint32 bit;
	guint32 flags;
	const gchar *error_copy;
	const gchar *db_col;
} vdata[] = {
	{ CAMEL_VTRASH_NAME, N_("Trash"), "(match-all (system-flag \"Deleted\"))", CAMEL_MESSAGE_DELETED, CAMEL_FOLDER_IS_TRASH,
	  N_("Cannot copy messages to the Trash folder"), "deleted" },
	{ CAMEL_VJUNK_NAME, N_("Junk"), "(match-all (system-flag \"Junk\"))", CAMEL_MESSAGE_JUNK, CAMEL_FOLDER_IS_JUNK,
	  N_("Cannot copy messages to the Junk folder"), "junk" },
};

static CamelVeeFolderClass *camel_vtrash_folder_parent;

static void camel_vtrash_folder_class_init (CamelVTrashFolderClass *klass);

static void
camel_vtrash_folder_init (CamelVTrashFolder *vtrash)
{
	/*CamelFolder *folder = CAMEL_FOLDER (vtrash);*/
}

CamelType
camel_vtrash_folder_get_type (void)
{
	static CamelType type = CAMEL_INVALID_TYPE;

	if (type == CAMEL_INVALID_TYPE) {
		type = camel_type_register (camel_vee_folder_get_type (),
					    "CamelVTrashFolder",
					    sizeof (CamelVTrashFolder),
					    sizeof (CamelVTrashFolderClass),
					    (CamelObjectClassInitFunc) camel_vtrash_folder_class_init,
					    NULL,
					    (CamelObjectInitFunc) camel_vtrash_folder_init,
					    NULL);
	}

	return type;
}

/**
 * camel_vtrash_folder_new:
 * @parent_store: the parent #CamelVeeStore object
 * @type: type of vfolder, #CAMEL_VTRASH_FOLDER_TRASH or
 * #CAMEL_VTRASH_FOLDER_JUNK currently.
 *
 * Create a new CamelVTrashFolder object.
 *
 * Returns: a new #CamelVTrashFolder object
 **/
CamelFolder *
camel_vtrash_folder_new (CamelStore *parent_store, camel_vtrash_folder_t type)
{
	CamelVTrashFolder *vtrash;

	g_assert(type < CAMEL_VTRASH_FOLDER_LAST);

	vtrash = (CamelVTrashFolder *)camel_object_new(camel_vtrash_folder_get_type());
	camel_vee_folder_construct(CAMEL_VEE_FOLDER (vtrash), parent_store, vdata[type].full_name, _(vdata[type].name),
				   CAMEL_STORE_FOLDER_PRIVATE|CAMEL_STORE_FOLDER_CREATE|CAMEL_STORE_VEE_FOLDER_AUTO|CAMEL_STORE_VEE_FOLDER_SPECIAL);

	((CamelFolder *)vtrash)->folder_flags |= vdata[type].flags;
	camel_vee_folder_set_expression((CamelVeeFolder *)vtrash, vdata[type].expr);
	vtrash->bit = vdata[type].bit;
	vtrash->type = type;

	return (CamelFolder *)vtrash;
}

/* This entire code will be useless, since we sync the counts always. */
static gint
vtrash_getv(CamelObject *object, CamelException *ex, CamelArgGetV *args)
{
	CamelFolder *folder = (CamelFolder *)object;
	gint i;
	guint32 tag;
	gint unread = -1, deleted = 0, junked = 0, visible = 0, count = -1, junked_not_deleted = -1;

	for (i=0;i<args->argc;i++) {
		CamelArgGet *arg = &args->argv[i];

		tag = arg->tag;

		/* NB: this is a copy of camel-folder.c with the unread count logic altered.
		   makes sure its still atomically calculated */
		switch (tag & CAMEL_ARG_TAG) {
		case CAMEL_FOLDER_ARG_UNREAD:
		case CAMEL_FOLDER_ARG_DELETED:
		case CAMEL_FOLDER_ARG_JUNKED:
		case CAMEL_FOLDER_ARG_JUNKED_NOT_DELETED:
		case CAMEL_FOLDER_ARG_VISIBLE:

			/* This is so we can get the values atomically, and also so we can calculate them only once */
			if (unread == -1) {
				gint j;
				CamelMessageInfoBase *info;
				CamelVeeMessageInfo *vinfo;

				unread = deleted = visible = junked = junked_not_deleted = 0;
				count = camel_folder_summary_count(folder->summary);
				for (j=0; j<count; j++) {
					if ((info = (CamelMessageInfoBase *) camel_folder_summary_index(folder->summary, j))) {
						guint32 flags;

						vinfo = (CamelVeeMessageInfo *) info;
						flags = vinfo->old_flags;/* ? vinfo->old_flags : camel_message_info_flags(info); */

						if ((flags & (CAMEL_MESSAGE_SEEN)) == 0)
							unread++;
						if (flags & CAMEL_MESSAGE_DELETED)
							deleted++;
						if (flags & CAMEL_MESSAGE_JUNK) {
							junked++;
								if (!(flags & CAMEL_MESSAGE_DELETED))
									junked_not_deleted++;
						}
						if ((flags & (CAMEL_MESSAGE_DELETED|CAMEL_MESSAGE_JUNK)) == 0)
							visible++;
						camel_message_info_free(info);
					}
				}
			}

			switch (tag & CAMEL_ARG_TAG) {
			case CAMEL_FOLDER_ARG_UNREAD:
				count = unread == -1 ? 0 : unread;
				break;
			case CAMEL_FOLDER_ARG_DELETED:
				count = deleted == -1 ? 0 : deleted;
				break;
			case CAMEL_FOLDER_ARG_JUNKED:
				count = junked == -1 ? 0 : junked;
				break;
			case CAMEL_FOLDER_ARG_JUNKED_NOT_DELETED:
				count = junked_not_deleted == -1 ? 0 : junked_not_deleted;
				break;
			case CAMEL_FOLDER_ARG_VISIBLE:
				count = visible == -1 ? 0 : visible;
				break;
			}
			folder->summary->unread_count = unread == -1 ? 0 : unread;
			folder->summary->deleted_count = deleted == -1 ? 0 : deleted;
			junked = folder->summary->junk_count = junked == -1 ? 0 : junked;
			folder->summary->junk_not_deleted_count = junked_not_deleted == -1 ? 0 : junked_not_deleted;
			folder->summary->visible_count = visible == -1 ? 0 : visible;
			*arg->ca_int = count;
			break;
		default:
			continue;
		}

		arg->tag = (tag & CAMEL_ARG_TYPE) | CAMEL_ARG_IGNORE;
	}

	return ((CamelObjectClass *)camel_vtrash_folder_parent)->getv(object, ex, args);
}

static void
vtrash_append_message (CamelFolder *folder, CamelMimeMessage *message,
		       const CamelMessageInfo *info, gchar **appended_uid,
		       CamelException *ex)
{
	camel_exception_setv(ex, CAMEL_EXCEPTION_SYSTEM, "%s",
			     _(vdata[((CamelVTrashFolder *)folder)->type].error_copy));
}

struct _transfer_data {
	CamelFolder *folder;
	CamelFolder *dest;
	GPtrArray *uids;
	gboolean delete;
};

static void
transfer_messages(CamelFolder *folder, struct _transfer_data *md, CamelException *ex)
{
	gint i;

	if (!camel_exception_is_set (ex))
		camel_folder_transfer_messages_to(md->folder, md->uids, md->dest, NULL, md->delete, ex);

	for (i=0;i<md->uids->len;i++)
		g_free(md->uids->pdata[i]);
	g_ptr_array_free(md->uids, TRUE);
	camel_object_unref((CamelObject *)md->folder);
	g_free(md);
}

static void
vtrash_transfer_messages_to (CamelFolder *source, GPtrArray *uids,
			     CamelFolder *dest, GPtrArray **transferred_uids,
			     gboolean delete_originals, CamelException *ex)
{
	CamelVeeMessageInfo *mi;
	gint i;
	GHashTable *batch = NULL;
	const gchar *tuid;
	struct _transfer_data *md;
	guint32 sbit = ((CamelVTrashFolder *)source)->bit;

	/* This is a special case of transfer_messages_to: Either the
	 * source or the destination is a vtrash folder (but not both
	 * since a store should never have more than one).
	 */

	if (transferred_uids)
		*transferred_uids = NULL;

	if (CAMEL_IS_VTRASH_FOLDER (dest)) {
		/* Copy to trash is meaningless. */
		if (!delete_originals) {
			camel_exception_setv(ex, CAMEL_EXCEPTION_SYSTEM, "%s",
					     _(vdata[((CamelVTrashFolder *)dest)->type].error_copy));
			return;
		}

		/* Move to trash is the same as setting the message flag */
		for (i = 0; i < uids->len; i++)
			camel_folder_set_message_flags(source, uids->pdata[i], ((CamelVTrashFolder *)dest)->bit, ~0);
		return;
	}

	/* Moving/Copying from the trash to the original folder = undelete.
	 * Moving/Copying from the trash to a different folder = move/copy.
	 *
	 * Need to check this uid by uid, but we batch up the copies.
	 */

	for (i = 0; i < uids->len; i++) {
		mi = (CamelVeeMessageInfo *)camel_folder_get_message_info (source, uids->pdata[i]);
		if (mi == NULL) {
			g_warning ("Cannot find uid %s in source folder during transfer", (gchar *) uids->pdata[i]);
			continue;
		}

		if (dest == mi->summary->folder) {
			/* Just unset the flag on the original message */
			camel_folder_set_message_flags (source, uids->pdata[i], sbit, 0);
		} else {
			if (batch == NULL)
				batch = g_hash_table_new(NULL, NULL);
			md = g_hash_table_lookup(batch, mi->summary->folder);
			if (md == NULL) {
				md = g_malloc0(sizeof(*md));
				md->folder = mi->summary->folder;
				camel_object_ref((CamelObject *)md->folder);
				md->uids = g_ptr_array_new();
				md->dest = dest;
				g_hash_table_insert(batch, mi->summary->folder, md);
			}

			tuid = uids->pdata[i];
			if (strlen(tuid)>8)
				tuid += 8;
			g_ptr_array_add(md->uids, g_strdup(tuid));
		}
		camel_folder_free_message_info (source, (CamelMessageInfo *)mi);
	}

	if (batch) {
		g_hash_table_foreach(batch, (GHFunc)transfer_messages, ex);
		g_hash_table_destroy(batch);
	}
}

static void
camel_vtrash_folder_class_init (CamelVTrashFolderClass *klass)
{
	CamelFolderClass *folder_class = (CamelFolderClass *) klass;

	camel_vtrash_folder_parent = CAMEL_VEE_FOLDER_CLASS(camel_vee_folder_get_type());

	/* Not required from here on. We don't count */
	((CamelObjectClass *)klass)->getv = vtrash_getv;

	folder_class->append_message = vtrash_append_message;
	folder_class->transfer_messages_to = vtrash_transfer_messages_to;
}
