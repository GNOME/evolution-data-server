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
	const char *full_name;
	const char *name;
	const char *expr;
	guint32 bit;
	guint32 flags;
	const char *error_copy;
	const char *db_col;
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
static int
vtrash_getv(CamelObject *object, CamelException *ex, CamelArgGetV *args)
{
	CamelFolder *folder = (CamelFolder *)object;
	int i;
	guint32 tag;
	int unread = -1, deleted = 0, junked = 0, visible = 0, count = -1;

	for (i=0;i<args->argc;i++) {
		CamelArgGet *arg = &args->argv[i];

		tag = arg->tag;

		/* NB: this is a copy of camel-folder.c with the unread count logic altered.
		   makes sure its still atomically calculated */
		switch (tag & CAMEL_ARG_TAG) {
		case CAMEL_FOLDER_ARG_UNREAD:
		case CAMEL_FOLDER_ARG_DELETED:
		case CAMEL_FOLDER_ARG_JUNKED:
		case CAMEL_FOLDER_ARG_VISIBLE:
			/* This is so we can get the values atomically, and also so we can calculate them only once */
			if (unread == -1) {
				int j;
				CamelMessageInfo *info;

				unread = 0;
				count = camel_folder_summary_count(folder->summary);
				for (j=0; j<count; j++) {
					if ((info = camel_folder_summary_index(folder->summary, j))) {
						guint32 flags = camel_message_info_flags(info);

						if ((flags & (CAMEL_MESSAGE_SEEN)) == 0)
							unread++;
						if (flags & CAMEL_MESSAGE_DELETED)
							deleted++;
						if (flags & CAMEL_MESSAGE_JUNK)
							junked++;
						if ((flags & (CAMEL_MESSAGE_DELETED|CAMEL_MESSAGE_JUNK)) == 0)
							visible++;
						camel_message_info_free(info);
					}
				}
			}

			switch (tag & CAMEL_ARG_TAG) {
			case CAMEL_FOLDER_ARG_UNREAD:
				count = unread;
				break;
			case CAMEL_FOLDER_ARG_DELETED:
				count = deleted;
				break;
			case CAMEL_FOLDER_ARG_JUNKED:
				count = junked;
				break;
			case CAMEL_FOLDER_ARG_VISIBLE:
				count = visible;
				break;
			}

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
		       const CamelMessageInfo *info, char **appended_uid,
		       CamelException *ex)
{
	camel_exception_setv(ex, CAMEL_EXCEPTION_SYSTEM, 
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
	int i;

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
	int i;
	GHashTable *batch = NULL;
	const char *tuid;
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
			camel_exception_setv(ex, CAMEL_EXCEPTION_SYSTEM, 
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
			g_warning ("Cannot find uid %s in source folder during transfer", (char *) uids->pdata[i]);
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

#warning rewrite the same way as camel-vee-summary.c
static GPtrArray *
vtrash_search_by_expression(CamelFolder *folder, const char *expression, CamelException *ex)
{
	GList *node;
	GPtrArray *matches, *result = g_ptr_array_new(), *uids = g_ptr_array_new();
	struct _CamelVeeFolderPrivate *p = ((CamelVeeFolder *)folder)->priv;
	GPtrArray *infos = camel_folder_get_summary(folder);

	/* we optimise the search by only searching for messages which we have anyway */
	CAMEL_VEE_FOLDER_LOCK(folder, subfolder_lock);
	node = p->folders;
	while (node) {
		CamelFolder *f = node->data;
		int i;
		char hash[8];

		camel_vee_folder_hash_folder(f, hash);

		for (i=0;i<infos->len;i++) {
			CamelVeeMessageInfo  *vmi = (CamelVeeMessageInfo *) camel_folder_summary_uid (folder->summary, infos->pdata[i]);
			if (!vmi)
				continue;
			//if (camel_message_info_flags(mi) & ((CamelVTrashFolder *)folder)->bit)
			if (vmi->summary == f->summary) /* Belongs to this folder */
				g_ptr_array_add(uids, (void *)camel_pstring_strdup(infos->pdata[i]+8));
			camel_message_info_free (vmi);
		}

                #warning search in the DB of the folder, for the expression, with the vtrash bit (junk/trash)
		if (uids->len > 0
		    && (matches = camel_folder_search_by_uids(f, expression, uids, NULL))) {
			for (i = 0; i < matches->len; i++) {
				char *uid = matches->pdata[i], *vuid;

				vuid = g_malloc(strlen(uid)+9);
				memcpy(vuid, hash, 8);
				strcpy(vuid+8, uid);
				g_ptr_array_add(result, (gpointer) camel_pstring_strdup(vuid));
				g_free (vuid);
			}
			camel_folder_search_free(f, matches);
		}
		g_ptr_array_set_size(uids, 0);

		node = g_list_next(node);
	}
	camel_folder_free_summary (folder, infos);
	CAMEL_VEE_FOLDER_UNLOCK(folder, subfolder_lock);

	g_ptr_array_foreach (uids, (GFunc) camel_pstring_free, NULL);
	g_ptr_array_free(uids, TRUE);

	return result;
}

static GPtrArray *
vtrash_search_by_uids(CamelFolder *folder, const char *expression, GPtrArray *uids, CamelException *ex)
{
	GList *node;
	GPtrArray *matches, *result = g_ptr_array_new(), *folder_uids = g_ptr_array_new();
	struct _CamelVeeFolderPrivate *p = ((CamelVeeFolder *)folder)->priv;
	
	CAMEL_VEE_FOLDER_LOCK(folder, subfolder_lock);

	node = p->folders;
	while (node) {
		CamelFolder *f = node->data;
		int i;
		char hash[8];
		
		camel_vee_folder_hash_folder(f, hash);

		/* map the vfolder uid's to the source folder uid's first */
		#warning "check this. is it uids od folder_uids"
		//g_ptr_array_set_size(uids, 0);
		g_ptr_array_set_size (folder_uids, 0);
		for (i=0;i<uids->len;i++) {
			char *uid = uids->pdata[i];
			
			//if (strlen(uid) >= 8 && strncmp(uid, hash, 8) == 0) {
			if (strncmp(uid, hash, 8) == 0) {				
				//CamelMessageInfo *mi;
				#warning "is it really reqd, if so uncomment it"
				//mi = camel_folder_get_message_info(f, uid+8);
				//if (mi) {
				//	if(camel_message_info_flags(mi) & ((CamelVTrashFolder *)folder)->bit)
						g_ptr_array_add(folder_uids, uid+8);
				//	camel_folder_free_message_info(f, mi);
				//}
			}
		}

		if (folder_uids->len > 0
		    && (matches = camel_folder_search_by_uids(f, expression, folder_uids, ex))) {
			for (i = 0; i < matches->len; i++) {
				char *uid = matches->pdata[i], *vuid;
				
				vuid = g_malloc(strlen(uid)+9);
				memcpy(vuid, hash, 8);
				strcpy(vuid+8, uid);
				g_ptr_array_add(result, (gpointer) camel_pstring_strdup(vuid));
				g_free (vuid);
			}
			camel_folder_search_free(f, matches);
		}
		node = g_list_next(node);
	}

	CAMEL_VEE_FOLDER_UNLOCK(folder, subfolder_lock);

	g_ptr_array_free(folder_uids, TRUE);

	return result;
}

static void
vtrash_uid_removed(CamelVTrashFolder *vf, const char *uid, char hash[8])
{
	char *vuid;

	vuid = g_alloca(strlen(uid)+9);
	memcpy(vuid, hash, 8);
	strcpy(vuid+8, uid);
	camel_folder_change_info_remove_uid(((CamelVeeFolder *)vf)->changes, vuid);
	camel_folder_summary_remove_uid(((CamelFolder *)vf)->summary, vuid);
}

static void
vtrash_uid_added(CamelVTrashFolder *vf, const char *uid, CamelFolderSummary *ssummary, char hash[8])
{
	char *vuid;
	CamelVeeMessageInfo *vinfo;

	vuid = g_alloca(strlen(uid)+9);
	memcpy(vuid, hash, 8);
	strcpy(vuid+8, uid);
	vinfo = (CamelVeeMessageInfo *)camel_folder_summary_uid(((CamelFolder *)vf)->summary, vuid);
	if (vinfo == NULL) {
		CamelMessageInfo *tinfo;
		tinfo = (CamelMessageInfo *) camel_vee_summary_add((CamelVeeSummary *)((CamelFolder *)vf)->summary, ssummary, uid, hash);
		if (tinfo) {
			camel_folder_change_info_add_uid(((CamelVeeFolder *)vf)->changes, vuid);
			camel_message_info_free (tinfo);
		}
	}
}

static void
vtrash_folder_changed(CamelVeeFolder *vf, CamelFolder *sub, CamelFolderChangeInfo *changes)
{
	CamelMessageInfo *info;
	char hash[8];
	CamelFolderChangeInfo *vf_changes = NULL;
	int i;

	camel_vee_folder_hash_folder(sub, hash);

	CAMEL_VEE_FOLDER_LOCK(vf, summary_lock);

	/* remove any removed that we also have */
	for (i=0;i<changes->uid_removed->len;i++)
		vtrash_uid_removed((CamelVTrashFolder *)vf, (const char *)changes->uid_removed->pdata[i], hash);

	/* check any changed still deleted/junked */
	for (i=0;i<changes->uid_changed->len;i++) {
		const char *uid = changes->uid_changed->pdata[i];

		info = camel_folder_get_message_info(sub, uid);
		if (info == NULL)
			continue;

		if ((camel_message_info_flags(info) & ((CamelVTrashFolder *)vf)->bit) == 0)
			vtrash_uid_removed((CamelVTrashFolder *)vf, uid, hash);
		else
			vtrash_uid_added((CamelVTrashFolder *)vf, uid, sub->summary, hash);

		camel_message_info_free(info);
	}

	/* add any new ones which are already matching */
	for (i=0;i<changes->uid_added->len;i++) {
		const char *uid = changes->uid_added->pdata[i];

		info = camel_folder_get_message_info(sub, uid);
		if (info == NULL)
			continue;

		if ((camel_message_info_flags(info) & ((CamelVTrashFolder *)vf)->bit) != 0)
			vtrash_uid_added((CamelVTrashFolder *)vf, uid, sub->summary, hash);

		camel_message_info_free(info);
	}

	if (camel_folder_change_info_changed(((CamelVeeFolder *)vf)->changes)) {
		vf_changes = ((CamelVeeFolder *)vf)->changes;
		((CamelVeeFolder *)vf)->changes = camel_folder_change_info_new();
	}

	CAMEL_VEE_FOLDER_UNLOCK(vf, summary_lock);
	
	if (vf_changes) {
		camel_object_trigger_event(vf, "folder_changed", vf_changes);
		camel_folder_change_info_free(vf_changes);
	}
}

static void
vtrash_add_folder(CamelVeeFolder *vf, CamelFolder *sub)
{
	GPtrArray *infos=NULL;
	int i;
	char hash[8], *shash;
	CamelFolderChangeInfo *vf_changes = NULL;

	camel_vee_folder_hash_folder(sub, hash);
	shash = g_strdup_printf("%c%c%c%c%c%c%c%c", hash[0], hash[1], hash[2], hash[3], hash[4], hash[5], hash[6], hash[7]);
	if (!g_hash_table_lookup (vf->hashes, shash))
		g_hash_table_insert (vf->hashes, g_strdup(shash), sub->summary);
	
	CAMEL_VEE_FOLDER_LOCK(vf, summary_lock);

	if (((CamelVTrashFolder *)vf)->bit == CAMEL_MESSAGE_DELETED) {
		infos = camel_db_get_folder_deleted_uids (sub->cdb, sub->full_name, NULL);
		if (infos) {
			((CamelFolder *)vf)->summary->saved_count += infos->len;
			((CamelFolder *)vf)->summary->deleted_count += infos->len;
		}
	}
	else if (((CamelVTrashFolder *)vf)->bit == CAMEL_MESSAGE_JUNK)
		infos = camel_db_get_folder_junk_uids (sub->cdb, sub->full_name, NULL);

	if (!infos) {
		CAMEL_VEE_FOLDER_UNLOCK(vf, summary_lock);
		g_free (shash);
		return;
	}
	
	for (i=0;i<infos->len;i++) {
		char *uid = infos->pdata[i];
		vtrash_uid_added((CamelVTrashFolder *)vf, uid, sub->summary, hash);
	}
	
	g_ptr_array_foreach (infos, (GFunc) camel_pstring_free, NULL);
	g_ptr_array_free (infos, TRUE);

	if (camel_folder_change_info_changed(vf->changes)) {
		vf_changes = vf->changes;
		vf->changes = camel_folder_change_info_new();
	}

	CAMEL_VEE_FOLDER_UNLOCK(vf, summary_lock);

	if (vf_changes) {
		camel_object_trigger_event(vf, "folder_changed", vf_changes);
		camel_folder_change_info_free(vf_changes);
	}

	g_free(shash);
}

static void
vtrash_remove_folder(CamelVeeFolder *vf, CamelFolder *sub)
{
	GPtrArray *infos;
	int i;
	char hash[8], *shash;
	CamelFolderChangeInfo *vf_changes = NULL;
	CamelFolderSummary *ssummary = sub->summary;
	int start, last;



	CAMEL_VEE_FOLDER_LOCK(vf, summary_lock);

	start = -1;
	last = -1;
	infos = camel_folder_get_summary ((CamelFolder *) vf);
	for (i=0;i<infos->len;i++) {

		CamelVeeMessageInfo *mi = (CamelVeeMessageInfo *) camel_folder_summary_uid (((CamelFolder *)vf)->summary, infos->pdata[i]);
		if (mi == NULL)
			continue;
		if (mi->summary == NULL) {
			camel_message_info_free (mi);
			continue;
		}

		if (mi->summary == ssummary) {
			const char *uid = camel_message_info_uid(mi);

			camel_folder_change_info_remove_uid(vf->changes, uid);

			if (last == -1) {
				last = start = i;
			} else if (last+1 == i) {
				last = i;
			} else {
				camel_folder_summary_remove_range(((CamelFolder *)vf)->summary, start, last);
				i -= (last-start)+1;
				start = last = i;
			}
		}
		camel_message_info_free (mi);
	}
	camel_folder_free_summary(sub, infos);

	if (last != -1)
		camel_folder_summary_remove_range(((CamelFolder *)vf)->summary, start, last);

	if (camel_folder_change_info_changed(vf->changes)) {
		vf_changes = vf->changes;
		vf->changes = camel_folder_change_info_new();
	}

	CAMEL_VEE_FOLDER_UNLOCK(vf, summary_lock);

	camel_vee_folder_hash_folder(sub, hash);
	shash = g_strdup_printf("%c%c%c%c%c%c%c%c", hash[0], hash[1], hash[2], hash[3], hash[4], hash[5], hash[6], hash[7]);
	if (g_hash_table_lookup (vf->hashes, shash))
		g_hash_table_remove (vf->hashes, shash);
	g_free(shash);	
	if (vf_changes) {
		camel_object_trigger_event(vf, "folder_changed", vf_changes);
		camel_folder_change_info_free(vf_changes);
	}
}

static int
vtrash_rebuild_folder(CamelVeeFolder *vf, CamelFolder *source, CamelException *ex)
{
	/* we should always be in sync */
	return 0;
}

static void
camel_vtrash_folder_class_init (CamelVTrashFolderClass *klass)
{
	CamelFolderClass *folder_class = (CamelFolderClass *) klass;
	
	camel_vtrash_folder_parent = CAMEL_VEE_FOLDER_CLASS(camel_vee_folder_get_type());

	/* Not required from here on. We don't count */
	/* ((CamelObjectClass *)klass)->getv = vtrash_getv; */ 
	
	folder_class->append_message = vtrash_append_message;
	folder_class->transfer_messages_to = vtrash_transfer_messages_to;
	/* Not required, lets use the base class search function */
	/* folder_class->search_by_expression = vtrash_search_by_expression; */
	/* folder_class->search_by_uids = vtrash_search_by_uids; */

	//((CamelVeeFolderClass *)klass)->add_folder = vtrash_add_folder;
	//((CamelVeeFolderClass *)klass)->remove_folder = vtrash_remove_folder;
	//((CamelVeeFolderClass *)klass)->rebuild_folder = vtrash_rebuild_folder;

	//((CamelVeeFolderClass *)klass)->folder_changed = vtrash_folder_changed;
}
