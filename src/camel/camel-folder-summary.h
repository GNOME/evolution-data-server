/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
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

#if !defined (__CAMEL_H_INSIDE__) && !defined (CAMEL_COMPILATION)
#error "Only <camel/camel.h> can be included directly."
#endif

#ifndef CAMEL_FOLDER_SUMMARY_H
#define CAMEL_FOLDER_SUMMARY_H

#include <stdio.h>
#include <time.h>

#include <camel/camel-index.h>
#include <camel/camel-memchunk.h>
#include <camel/camel-mime-message.h>
#include <camel/camel-mime-parser.h>

/* Standard GObject macros */
#define CAMEL_TYPE_FOLDER_SUMMARY \
	(camel_folder_summary_get_type ())
#define CAMEL_FOLDER_SUMMARY(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), CAMEL_TYPE_FOLDER_SUMMARY, CamelFolderSummary))
#define CAMEL_FOLDER_SUMMARY_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), CAMEL_TYPE_FOLDER_SUMMARY, CamelFolderSummaryClass))
#define CAMEL_IS_FOLDER_SUMMARY(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), CAMEL_TYPE_FOLDER_SUMMARY))
#define CAMEL_IS_FOLDER_SUMMARY_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), CAMEL_TYPE_FOLDER_SUMMARY))
#define CAMEL_FOLDER_SUMMARY_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), CAMEL_TYPE_FOLDER_SUMMARY, CamelFolderSummaryClass))

G_BEGIN_DECLS

struct _CamelFolder;
struct _CamelStore;

typedef struct _CamelFolderSummary CamelFolderSummary;
typedef struct _CamelFolderSummaryClass CamelFolderSummaryClass;
typedef struct _CamelFolderSummaryPrivate CamelFolderSummaryPrivate;

typedef struct _CamelMessageInfo CamelMessageInfo;
typedef struct _CamelMessageInfoBase CamelMessageInfoBase;

typedef struct _CamelMessageContentInfo CamelMessageContentInfo;

/* A tree of message content info structures
 * describe the content structure of the message (if it has any) */
struct _CamelMessageContentInfo {
	CamelMessageContentInfo *next;

	CamelMessageContentInfo *childs;
	CamelMessageContentInfo *parent;

	CamelContentType *type;
	gchar *id;
	gchar *description;
	gchar *encoding;		/* this should be an enum?? */
	guint32 size;
};

/* system flag bits */
typedef enum _CamelMessageFlags {
	CAMEL_MESSAGE_ANSWERED = 1 << 0,
	CAMEL_MESSAGE_DELETED = 1 << 1,
	CAMEL_MESSAGE_DRAFT = 1 << 2,
	CAMEL_MESSAGE_FLAGGED = 1 << 3,
	CAMEL_MESSAGE_SEEN = 1 << 4,

	/* these aren't really system flag bits, but are convenience flags */
	CAMEL_MESSAGE_ATTACHMENTS = 1 << 5,
	CAMEL_MESSAGE_ANSWERED_ALL = 1 << 6,
	CAMEL_MESSAGE_JUNK = 1 << 7,
	CAMEL_MESSAGE_SECURE = 1 << 8,
	CAMEL_MESSAGE_NOTJUNK = 1 << 9,
	CAMEL_MESSAGE_FORWARDED = 1 << 10,

	/* following flags are for the folder, and are not really permanent flags */
	CAMEL_MESSAGE_FOLDER_FLAGGED = 1 << 16, /* for use by the folder implementation */
	/* flags after 1 << 16 are used by camel providers,
 *         if adding non permanent flags, add them to the end  */

	CAMEL_MESSAGE_JUNK_LEARN = 1 << 30, /* used when setting CAMEL_MESSAGE_JUNK flag
					     * to say that we request junk plugin
					     * to learn that message as junk/non junk */
	CAMEL_MESSAGE_USER = 1 << 31 /* supports user flags */
} CamelMessageFlags;

/* Changes to system flags will NOT trigger a folder changed event */
#define CAMEL_MESSAGE_SYSTEM_MASK (0xffff << 16)

typedef struct _CamelFlag {
	struct _CamelFlag *next;
	gchar name[1];		/* name allocated as part of the structure */
} CamelFlag;

typedef struct _CamelTag {
	struct _CamelTag *next;
	gchar *value;
	gchar name[1];		/* name allocated as part of the structure */
} CamelTag;

/* a summary messageid is a 64 bit identifier (partial md5 hash) */
typedef struct _CamelSummaryMessageID {
	union {
		guint64 id;
		guchar hash[8];
		struct {
			guint32 hi;
			guint32 lo;
		} part;
	} id;
} CamelSummaryMessageID;

/* summary references is a fixed size array of references */
typedef struct _CamelSummaryReferences {
	gint size;
	CamelSummaryMessageID references[1];
} CamelSummaryReferences;

/* accessor id's */
enum {
	CAMEL_MESSAGE_INFO_SUBJECT,
	CAMEL_MESSAGE_INFO_FROM,
	CAMEL_MESSAGE_INFO_TO,
	CAMEL_MESSAGE_INFO_CC,
	CAMEL_MESSAGE_INFO_MLIST,

	CAMEL_MESSAGE_INFO_FLAGS,
	CAMEL_MESSAGE_INFO_SIZE,

	CAMEL_MESSAGE_INFO_DATE_SENT,
	CAMEL_MESSAGE_INFO_DATE_RECEIVED,

	CAMEL_MESSAGE_INFO_MESSAGE_ID,
	CAMEL_MESSAGE_INFO_REFERENCES,
	CAMEL_MESSAGE_INFO_USER_FLAGS,
	CAMEL_MESSAGE_INFO_USER_TAGS,

	CAMEL_MESSAGE_INFO_HEADERS,
	CAMEL_MESSAGE_INFO_PREVIEW,
	CAMEL_MESSAGE_INFO_CONTENT,
	CAMEL_MESSAGE_INFO_LAST
};

/* information about a given message, use accessors */
struct _CamelMessageInfo {
	CamelFolderSummary *summary;

	volatile gint refcount;
	const gchar *uid;
	/*FIXME: Make it work with the CAMEL_MESSADE_DB_DIRTY flag instead of another 4 bytes*/
	guint dirty : 1;
};

/* For classes wishing to do the provided i/o, or for anonymous users,
 * they must subclass or use this messageinfo structure */
/* Otherwise they can do their own thing entirely */
struct _CamelMessageInfoBase {
	CamelFolderSummary *summary;

	volatile gint refcount;
	const gchar *uid;
	/*FIXME: Make it work with the CAMEL_MESSADE_DB_DIRTY flag instead of another 4 bytes*/
	guint dirty : 1;

	const gchar *subject;
	const gchar *from;
	const gchar *to;
	const gchar *cc;
	const gchar *mlist;

	CamelMessageFlags flags;
	guint32 size;

	time_t date_sent;
	time_t date_received;

	CamelSummaryMessageID message_id;
	CamelSummaryReferences *references;/* from parent to root */

	struct _CamelFlag *user_flags;
	struct _CamelTag *user_tags;

	/* tree of content description - NULL if it is not available */
	CamelMessageContentInfo *content;
	struct _camel_header_param *headers;
	gchar *preview;
	gchar *bodystructure;
};

/**
 * CamelFolderSummaryFlags:
 * @CAMEL_FOLDER_SUMMARY_DIRTY:
 *    There are changes in summary, which should be saved.
 * @CAMEL_FOLDER_SUMMARY_IN_MEMORY_ONLY:
 *    Summary with this flag doesn't use DB for storing its content,
 *    it is always created on the fly.
 **/
typedef enum {
	CAMEL_FOLDER_SUMMARY_DIRTY = 1 << 0,
	CAMEL_FOLDER_SUMMARY_IN_MEMORY_ONLY = 1 << 1
} CamelFolderSummaryFlags;

struct _CamelFolderSummary {
	GObject parent;
	CamelFolderSummaryPrivate *priv;

	/* header info */
	guint32 version;	/* version of file loaded/loading */
	time_t time;		/* timestamp for this summary (for implementors to use) */
	CamelFolderSummaryFlags flags;

	const gchar *collate;
	const gchar *sort_by;

	/* Future ABI expansion */
	gpointer later[4];
};

struct _CamelMIRecord;
struct _CamelFIRecord;

struct _CamelFolderSummaryClass {
	GObjectClass parent_class;

	/* sizes of memory objects */
	gsize message_info_size;
	gsize content_info_size;

	/* Load/Save folder summary from DB*/
	gboolean	(*summary_header_from_db)
					(CamelFolderSummary *summary,
					 struct _CamelFIRecord *fir);
	struct _CamelFIRecord *
			(*summary_header_to_db)
					(CamelFolderSummary *summary,
					 GError **error);
	CamelMessageInfo *
			(*message_info_from_db)
					(CamelFolderSummary *summary,
					 struct _CamelMIRecord *mir);
	struct _CamelMIRecord *
			(*message_info_to_db)
					(CamelFolderSummary *summary,
					 CamelMessageInfo *info);
	CamelMessageContentInfo *
			(*content_info_from_db)
					(CamelFolderSummary *summary,
					 struct _CamelMIRecord *mir);
	gboolean	(*content_info_to_db)
					(CamelFolderSummary *summary,
					 CamelMessageContentInfo *info,
					 struct _CamelMIRecord *mir);

	/* create/save/load an individual message info */
	CamelMessageInfo *
			(*message_info_new_from_header)
					(CamelFolderSummary *summary,
					 struct _camel_header_raw *header);
	CamelMessageInfo *
			(*message_info_new_from_parser)
					(CamelFolderSummary *summary,
					 CamelMimeParser *parser);
	CamelMessageInfo *
			(*message_info_new_from_message)
					(CamelFolderSummary *summary,
					 CamelMimeMessage *message,
					 const gchar *bodystructure);
	void		(*message_info_free)
					(CamelFolderSummary *summary,
					 CamelMessageInfo *ci);
	CamelMessageInfo *
			(*message_info_clone)
					(CamelFolderSummary *summary,
					 const CamelMessageInfo *info);

	/* save/load individual content info's */
	CamelMessageContentInfo *
			(*content_info_new_from_header)
					(CamelFolderSummary *summary,
					 struct _camel_header_raw *header);
	CamelMessageContentInfo *
			(*content_info_new_from_parser)
					(CamelFolderSummary *summary,
					 CamelMimeParser *parser);
	CamelMessageContentInfo *
			(*content_info_new_from_message)
					(CamelFolderSummary *summary,
					 CamelMimePart *mime_part);
	void		(*content_info_free)
					(CamelFolderSummary *summary,
					 CamelMessageContentInfo *ci);
	CamelMessageInfo *
			(*message_info_from_uid)
					(CamelFolderSummary *summary,
					 const gchar *uid);

	/* get the next uid */
	gchar *		(*next_uid_string)
					(CamelFolderSummary *summary);

	/* virtual accessors on messageinfo's */
	gconstpointer	(*info_ptr)	(const CamelMessageInfo *info,
					 gint id);
	guint32		(*info_uint32)	(const CamelMessageInfo *info,
					 gint id);
	time_t		(*info_time)	(const CamelMessageInfo *info,
					 gint id);

	gboolean	(*info_user_flag)
					(const CamelMessageInfo *info,
					 const gchar *id);
	const gchar *	(*info_user_tag)
					(const CamelMessageInfo *info,
					 const gchar *id);

	/* set accessors for the modifyable bits */
	gboolean	(*info_set_user_flag)
					(CamelMessageInfo *info,
					 const gchar *id,
					 gboolean state);
	gboolean	(*info_set_user_tag)
					(CamelMessageInfo *info,
					 const gchar *id,
					 const gchar *val);
	gboolean	(*info_set_flags)
					(CamelMessageInfo *info,
					 guint32 mask,
					 guint32 set);
};

GType		camel_folder_summary_get_type	(void);
CamelFolderSummary *
		camel_folder_summary_new	(struct _CamelFolder *folder);

struct _CamelFolder *
		camel_folder_summary_get_folder	(CamelFolderSummary *summary);

guint32		camel_folder_summary_get_saved_count
						(CamelFolderSummary *summary);
guint32		camel_folder_summary_get_unread_count
						(CamelFolderSummary *summary);
guint32		camel_folder_summary_get_deleted_count
						(CamelFolderSummary *summary);
guint32		camel_folder_summary_get_junk_count
						(CamelFolderSummary *summary);
guint32		camel_folder_summary_get_junk_not_deleted_count
						(CamelFolderSummary *summary);
guint32		camel_folder_summary_get_visible_count
						(CamelFolderSummary *summary);

void		camel_folder_summary_set_index	(CamelFolderSummary *summary,
						 CamelIndex *index);
CamelIndex *	camel_folder_summary_get_index	(CamelFolderSummary *summary);
void		camel_folder_summary_set_build_content
						(CamelFolderSummary *summary,
						 gboolean state);
gboolean	camel_folder_summary_get_build_content
						(CamelFolderSummary *summary);
void		camel_folder_summary_set_need_preview
						(CamelFolderSummary *summary,
						 gboolean preview);
gboolean	camel_folder_summary_get_need_preview
						(CamelFolderSummary *summary);
guint32		camel_folder_summary_next_uid	(CamelFolderSummary *summary);
void		camel_folder_summary_set_next_uid
						(CamelFolderSummary *summary,
						 guint32 uid);
guint32		camel_folder_summary_get_next_uid
						(CamelFolderSummary *summary);
gchar *		camel_folder_summary_next_uid_string
						(CamelFolderSummary *summary);

/* load/save the full summary from/to the db */
gboolean	camel_folder_summary_save_to_db	(CamelFolderSummary *summary,
						 GError **error);
gboolean	camel_folder_summary_load_from_db
						(CamelFolderSummary *summary,
						 GError **error);

/* only load the header */
gboolean	camel_folder_summary_header_load_from_db
						(CamelFolderSummary *summary,
						 struct _CamelStore *store,
						 const gchar *folder_name,
						 GError **error);
gboolean	camel_folder_summary_header_save_to_db
						(CamelFolderSummary *summary,
						 GError **error);

/* set the dirty bit on the summary */
void		camel_folder_summary_touch	(CamelFolderSummary *summary);

/* Just build raw summary items */
CamelMessageInfo *
		camel_folder_summary_info_new_from_header
						(CamelFolderSummary *summary,
						 struct _camel_header_raw *headers);
CamelMessageInfo *
		camel_folder_summary_info_new_from_parser
						(CamelFolderSummary *summary,
						 CamelMimeParser *parser);
CamelMessageInfo *
		camel_folder_summary_info_new_from_message
						(CamelFolderSummary *summary,
						 CamelMimeMessage *message,
						 const gchar *bodystructure);

CamelMessageContentInfo *
		camel_folder_summary_content_info_new
						(CamelFolderSummary *summary);
void		camel_folder_summary_content_info_free
						(CamelFolderSummary *summary,
						 CamelMessageContentInfo *ci);

void		camel_folder_summary_add_preview
						(CamelFolderSummary *summary,
						 CamelMessageInfo *info);

/* add a new raw summary item */
void		camel_folder_summary_add	(CamelFolderSummary *summary,
						 CamelMessageInfo *info);

/* insert mi to summary */
void		camel_folder_summary_insert	(CamelFolderSummary *summary,
						 CamelMessageInfo *info,
						 gboolean load);

gboolean	camel_folder_summary_remove	(CamelFolderSummary *summary,
						 CamelMessageInfo *info);

gboolean	camel_folder_summary_remove_uid	(CamelFolderSummary *summary,
						 const gchar *uid);
gboolean	camel_folder_summary_remove_uids (CamelFolderSummary *summary,
						 GList *uids);

/* remove all items */
gboolean	camel_folder_summary_clear	(CamelFolderSummary *summary,
						 GError **error);

/* lookup functions */
guint		camel_folder_summary_count	(CamelFolderSummary *summary);

gboolean	camel_folder_summary_check_uid	(CamelFolderSummary *summary,
						 const gchar *uid);
CamelMessageInfo *
		camel_folder_summary_get	(CamelFolderSummary *summary,
						 const gchar *uid);
guint32		camel_folder_summary_get_info_flags
						(CamelFolderSummary *summary,
						 const gchar *uid);
GPtrArray *	camel_folder_summary_get_array	(CamelFolderSummary *summary);
void		camel_folder_summary_free_array	(GPtrArray *array);

GHashTable *	camel_folder_summary_get_hash	(CamelFolderSummary *summary);

gboolean	camel_folder_summary_replace_flags
						(CamelFolderSummary *summary,
						 CamelMessageInfo *info);

/* Peek from mem only */
CamelMessageInfo *
		camel_folder_summary_peek_loaded
						(CamelFolderSummary *summary,
						 const gchar *uid);

/* Get only the uids of dirty/changed things to sync to server/db */
GPtrArray *	camel_folder_summary_get_changed
						(CamelFolderSummary *summary);

/* reload the summary at any required point if required */
void		camel_folder_summary_prepare_fetch_all
						(CamelFolderSummary *summary,
						 GError **error);

/* summary locking */
void		camel_folder_summary_lock	(CamelFolderSummary *summary);
void		camel_folder_summary_unlock	(CamelFolderSummary *summary);

/* message flag operations */
gboolean	camel_flag_get			(CamelFlag **list,
						 const gchar *name);
gboolean	camel_flag_set			(CamelFlag **list,
						 const gchar *name,
						 gboolean value);
gboolean	camel_flag_list_copy		(CamelFlag **to,
						 CamelFlag **from);
gint		camel_flag_list_size		(CamelFlag **list);
void		camel_flag_list_free		(CamelFlag **list);

CamelMessageFlags
		camel_system_flag		(const gchar *name);
gboolean	camel_system_flag_get		(CamelMessageFlags flags,
						 const gchar *name);

/* message tag operations */
const gchar *	camel_tag_get			(CamelTag **list,
						 const gchar *name);
gboolean	camel_tag_set			(CamelTag **list,
						 const gchar *name,
						 const gchar *value);
gboolean	camel_tag_list_copy		(CamelTag **to,
						 CamelTag **from);
gint		camel_tag_list_size		(CamelTag **list);
void		camel_tag_list_free		(CamelTag **list);

/* Summary may be null */
/* Use anonymous pointers to avoid tons of cast crap */
GType		camel_message_info_get_type	(void) G_GNUC_CONST;
gpointer	camel_message_info_new		(CamelFolderSummary *summary);
gpointer	camel_message_info_ref		(gpointer info);
CamelMessageInfo *
		camel_message_info_new_from_header
						(CamelFolderSummary *summary,
						 struct _camel_header_raw *header);
void		camel_message_info_unref	(gpointer info);
gpointer	camel_message_info_clone	(gconstpointer info);

/* These will be fully removed soon, left only for a backward compatibility */
#define camel_message_info_ptr			camel_message_info_get_ptr
#define camel_message_info_uint32		camel_message_info_get_uint32
#define camel_message_info_time			camel_message_info_get_time
#define camel_message_info_uid			camel_message_info_get_uid
#define camel_message_info_subject		camel_message_info_get_subject
#define camel_message_info_preview		camel_message_info_get_preview
#define camel_message_info_from			camel_message_info_get_from
#define camel_message_info_to			camel_message_info_get_to
#define camel_message_info_cc			camel_message_info_get_cc
#define camel_message_info_mlist		camel_message_info_get_mlist
#define camel_message_info_flags		camel_message_info_get_flags
#define camel_message_info_size			camel_message_info_get_size
#define camel_message_info_date_sent		camel_message_info_get_date_sent
#define camel_message_info_date_received	camel_message_info_get_date_received
#define camel_message_info_message_id		camel_message_info_get_message_id
#define camel_message_info_references		camel_message_info_get_references
#define camel_message_info_user_flags		camel_message_info_get_user_flags
#define camel_message_info_user_tags		camel_message_info_get_user_tags
#define camel_message_info_headers		camel_message_info_get_headers
#define camel_message_info_content		camel_message_info_get_content
#define camel_message_info_user_flag		camel_message_info_get_user_flag
#define camel_message_info_user_tag		camel_message_info_get_user_tag

/* accessors */
gconstpointer	camel_message_info_get_ptr	(gconstpointer info,
						 gint id);
guint32		camel_message_info_get_uint32	(gconstpointer info,
						 gint id);
time_t		camel_message_info_get_time	(gconstpointer info,
						 gint id);

const gchar *	camel_message_info_get_uid	(gconstpointer info);
const gchar *	camel_message_info_get_subject	(gconstpointer info);
const gchar *	camel_message_info_get_preview	(gconstpointer info);
const gchar *	camel_message_info_get_from	(gconstpointer info);
const gchar *	camel_message_info_get_to	(gconstpointer info);

const gchar *	camel_message_info_get_cc	(gconstpointer info);
const gchar *	camel_message_info_get_mlist	(gconstpointer info);
guint32		camel_message_info_get_flags	(gconstpointer info);
guint32		camel_message_info_get_size	(gconstpointer info);

time_t		camel_message_info_get_date_sent
						(gconstpointer info);
time_t		camel_message_info_get_date_received
						(gconstpointer info);

const CamelSummaryMessageID *
		camel_message_info_get_message_id
						(gconstpointer info);
const CamelSummaryReferences *
		camel_message_info_get_references
						(gconstpointer info);
const CamelFlag *
		camel_message_info_get_user_flags
						(gconstpointer info);
const CamelTag *
		camel_message_info_get_user_tags
						(gconstpointer info);
const CamelHeaderParam *
		camel_message_info_get_headers	(gconstpointer info);
const CamelMessageContentInfo *
		camel_message_info_get_content	(gconstpointer info);
gboolean	camel_message_info_get_user_flag(gconstpointer info,
						 const gchar *id);
const gchar *	camel_message_info_get_user_tag	(gconstpointer info,
						 const gchar *id);

gboolean	camel_message_info_set_flags	(CamelMessageInfo *info,
						 CamelMessageFlags flags,
						 guint32 set);
gboolean	camel_message_info_set_user_flag
						(CamelMessageInfo *info,
						 const gchar *id,
						 gboolean state);
gboolean	camel_message_info_set_user_tag	(CamelMessageInfo *info,
						 const gchar *id,
						 const gchar *val);

/* debugging functions */
void		camel_content_info_dump		(CamelMessageContentInfo *ci,
						 gint depth);
void		camel_message_info_dump		(CamelMessageInfo *info);

/* utility functions for bdata string decomposition */
gint		bdata_extract_digit		(gchar **part);
gchar *		bdata_extract_string		(gchar **part);

G_END_DECLS

#endif /* CAMEL_FOLDER_SUMMARY_H */
