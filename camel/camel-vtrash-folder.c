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

#include <glib/gi18n-lib.h>

#include "camel-db.h"
#include "camel-mime-message.h"
#include "camel-store.h"
#include "camel-vee-store.h"
#include "camel-vtrash-folder.h"
#include "camel-string-utils.h"

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

struct _transfer_data {
	CamelFolder *folder;
	CamelFolder *dest;
	GPtrArray *uids;
	gboolean delete;
};

G_DEFINE_TYPE (CamelVTrashFolder, camel_vtrash_folder, CAMEL_TYPE_VEE_FOLDER)

static void
transfer_messages (CamelFolder *folder,
                   struct _transfer_data *md,
                   GError **error)
{
	gint i;

	camel_folder_transfer_messages_to (
		md->folder, md->uids, md->dest, NULL, md->delete, error);

	for (i=0;i<md->uids->len;i++)
		g_free(md->uids->pdata[i]);
	g_ptr_array_free(md->uids, TRUE);
	g_object_unref (md->folder);
	g_free(md);
}

static gboolean
vtrash_folder_append_message (CamelFolder *folder,
                              CamelMimeMessage *message,
                              const CamelMessageInfo *info,
                              gchar **appended_uid,
                              GError **error)
{
	g_set_error (
		error, CAMEL_ERROR, CAMEL_ERROR_GENERIC, "%s",
		_(vdata[((CamelVTrashFolder *)folder)->type].error_copy));

	return FALSE;
}

static gboolean
vtrash_folder_transfer_messages_to (CamelFolder *source,
                                    GPtrArray *uids,
                                    CamelFolder *dest,
                                    GPtrArray **transferred_uids,
                                    gboolean delete_originals,
                                    GError **error)
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
			g_set_error (
				error, CAMEL_ERROR, CAMEL_ERROR_GENERIC, "%s",
				_(vdata[((CamelVTrashFolder *)dest)->type].error_copy));
			return FALSE;
		}

		/* Move to trash is the same as setting the message flag */
		for (i = 0; i < uids->len; i++)
			camel_folder_set_message_flags(source, uids->pdata[i], ((CamelVTrashFolder *)dest)->bit, ~0);
		return TRUE;
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
				md->folder = g_object_ref (mi->summary->folder);
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
		g_hash_table_foreach(batch, (GHFunc)transfer_messages, error);
		g_hash_table_destroy(batch);
	}

	return TRUE;
}

static void
camel_vtrash_folder_class_init (CamelVTrashFolderClass *class)
{
	CamelFolderClass *folder_class;

	folder_class = CAMEL_FOLDER_CLASS (class);
	folder_class->append_message = vtrash_folder_append_message;
	folder_class->transfer_messages_to = vtrash_folder_transfer_messages_to;
}

static void
camel_vtrash_folder_init (CamelVTrashFolder *vtrash_folder)
{
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

	vtrash = g_object_new (
		CAMEL_TYPE_VTRASH_FOLDER,
		"full-name", vdata[type].full_name,
		"name", gettext (vdata[type].name),
		"parent-store", parent_store, NULL);

	camel_vee_folder_construct (
		CAMEL_VEE_FOLDER (vtrash),
		CAMEL_STORE_FOLDER_PRIVATE |
		CAMEL_STORE_FOLDER_CREATE |
		CAMEL_STORE_VEE_FOLDER_AUTO |
		CAMEL_STORE_VEE_FOLDER_SPECIAL);

	((CamelFolder *)vtrash)->folder_flags |= vdata[type].flags;
	camel_vee_folder_set_expression((CamelVeeFolder *)vtrash, vdata[type].expr);
	vtrash->bit = vdata[type].bit;
	vtrash->type = type;

	return (CamelFolder *)vtrash;
}
