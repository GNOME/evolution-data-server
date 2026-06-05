/*
 * SPDX-FileCopyrightText: (C) 2026 Red Hat (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#if !defined (__CAMEL_H_INSIDE__) && !defined (CAMEL_COMPILATION)
#error "Only <camel/camel.h> can be included directly."
#endif

#ifndef CAMEL_FOLDER_VIEW_H
#define CAMEL_FOLDER_VIEW_H

#include <camel/camel-folder.h>
#include <camel/camel-enums.h>


G_BEGIN_DECLS

#define CAMEL_TYPE_FOLDER_VIEW (camel_folder_view_get_type ())
G_DECLARE_FINAL_TYPE (CamelFolderView, camel_folder_view, CAMEL, FOLDER_VIEW, GObject)

typedef struct _CamelFolderViewRow CamelFolderViewRow;
typedef struct _CamelFolderViewGeneration CamelFolderViewGeneration;

void		camel_folder_view_row_lock	(CamelFolderViewRow *row);
void		camel_folder_view_row_unlock	(CamelFolderViewRow *row);

const gchar *	camel_folder_view_row_get_uid	(CamelFolderViewRow *row);
guint32		camel_folder_view_row_get_flags	(CamelFolderViewRow *row);
gint64		camel_folder_view_row_get_date_sent
						(CamelFolderViewRow *row);
gint64		camel_folder_view_row_get_date_received
						(CamelFolderViewRow *row);
guint32		camel_folder_view_row_get_size	(CamelFolderViewRow *row);
const gchar *	camel_folder_view_row_get_subject
						(CamelFolderViewRow *row);
const gchar *	camel_folder_view_row_get_from	(CamelFolderViewRow *row);
const gchar *	camel_folder_view_row_get_to	(CamelFolderViewRow *row);
const gchar *	camel_folder_view_row_get_cc	(CamelFolderViewRow *row);
const gchar *	camel_folder_view_row_get_mlist	(CamelFolderViewRow *row);
const gchar *	camel_folder_view_row_get_preview
						(CamelFolderViewRow *row);
const gchar *	camel_folder_view_row_get_subject_trimmed
						(CamelFolderViewRow *row);
const gchar *	camel_folder_view_row_get_sender
						(CamelFolderViewRow *row);
const gchar *	camel_folder_view_row_get_sender_mail
						(CamelFolderViewRow *row);
const gchar *	camel_folder_view_row_get_recipients
						(CamelFolderViewRow *row);
const gchar *	camel_folder_view_row_get_recipients_mail
						(CamelFolderViewRow *row);
const gchar *	camel_folder_view_row_get_correspondents
						(CamelFolderViewRow *row);
const gchar *	camel_folder_view_row_get_labels
						(CamelFolderViewRow *row);
const gchar *	camel_folder_view_row_get_followup_flag
						(CamelFolderViewRow *row);
gboolean	camel_folder_view_row_get_followup_completed
						(CamelFolderViewRow *row);
gint64		camel_folder_view_row_get_followup_due_by
						(CamelFolderViewRow *row);
gint		camel_folder_view_row_get_score	(CamelFolderViewRow *row);
const gchar *	camel_folder_view_row_get_location
						(CamelFolderViewRow *row);
const gchar *	camel_folder_view_row_get_color	(CamelFolderViewRow *row);
gboolean	camel_folder_view_row_get_ignore_thread
						(CamelFolderViewRow *row);
const gchar *	camel_folder_view_row_get_user_header
						(CamelFolderViewRow *row,
						 guint index);

CamelFolderView *
		camel_folder_view_new		(CamelFolder *folder,
						 gboolean default_expanded);
CamelFolder *	camel_folder_view_get_folder	(CamelFolderView *self);

void		camel_folder_view_freeze	(CamelFolderView *self);
void		camel_folder_view_thaw		(CamelFolderView *self);
gboolean	camel_folder_view_is_frozen	(CamelFolderView *self);

gboolean	camel_folder_view_rebuild_sync	(CamelFolderView *self,
						 GCancellable *cancellable,
						 GError **error);
gboolean	camel_folder_view_sort		(CamelFolderView *self);
gboolean	camel_folder_view_process_pending_changes_sync
						(CamelFolderView *self,
						 CamelFolderViewGeneration *old_generation,
						 GCancellable *cancellable,
						 GError **error);

void		camel_folder_view_clear_own_addresses
						(CamelFolderView *self);
void		camel_folder_view_add_own_address
						(CamelFolderView *self,
						 const gchar *email);
void		camel_folder_view_remove_own_address
						(CamelFolderView *self,
						 const gchar *email);
void		camel_folder_view_set_user_headers
						(CamelFolderView *self,
						 const gchar * const *headers);

void		camel_folder_view_set_filter	(CamelFolderView *self,
						 const gchar *sexp);
const gchar *	camel_folder_view_get_filter	(CamelFolderView *self);

void		camel_folder_view_set_ensure_uid
						(CamelFolderView *self,
						 const gchar *uid);
const gchar *	camel_folder_view_get_ensure_uid
						(CamelFolderView *self);

void		camel_folder_view_set_show_deleted
						(CamelFolderView *self,
						 gboolean show_deleted);
gboolean	camel_folder_view_get_show_deleted
						(CamelFolderView *self);
void		camel_folder_view_set_show_junk
						(CamelFolderView *self,
						 gboolean show_junk);
gboolean	camel_folder_view_get_show_junk
						(CamelFolderView *self);

void		camel_folder_view_set_threading	(CamelFolderView *self,
						 CamelFolderViewThreading mode);
CamelFolderViewThreading
		camel_folder_view_get_threading	(CamelFolderView *self);

void		camel_folder_view_set_thread_subject
						(CamelFolderView *self,
						 gboolean enabled);
gboolean	camel_folder_view_get_thread_subject
						(CamelFolderView *self);

void		camel_folder_view_set_group_by	(CamelFolderView *self,
						 CamelFolderViewGroupBy group_by);
CamelFolderViewGroupBy
		camel_folder_view_get_group_by	(CamelFolderView *self);

void		camel_folder_view_set_thread_latest
						(CamelFolderView *self,
						 gboolean thread_latest);
gboolean	camel_folder_view_get_thread_latest
						(CamelFolderView *self);

void		camel_folder_view_set_sort_children_ascending
						(CamelFolderView *self,
						 gboolean ascending);
gboolean	camel_folder_view_get_sort_children_ascending
						(CamelFolderView *self);

void		camel_folder_view_set_localized_re
						(CamelFolderView *self,
						 const gchar *prefixes);
const gchar *	camel_folder_view_get_localized_re
						(CamelFolderView *self);

void		camel_folder_view_set_localized_re_separators
						(CamelFolderView *self,
						 const gchar * const *separators);
const gchar * const *
		camel_folder_view_get_localized_re_separators
						(CamelFolderView *self);

void		camel_folder_view_set_sort	(CamelFolderView *self,
						 CamelFolderViewColumn column,
						 CamelSortType order);
void		camel_folder_view_add_sort	(CamelFolderView *self,
						 CamelFolderViewColumn column,
						 CamelSortType order);
void		camel_folder_view_clear_sort	(CamelFolderView *self);
guint		camel_folder_view_get_sort_count	(CamelFolderView *self);
gboolean	camel_folder_view_get_sort_column
						(CamelFolderView *self,
						 guint index,
						 CamelFolderViewColumn *out_column,
						 CamelSortType *out_order);

guint		camel_folder_view_find_row_by_uid
						(CamelFolderView *self,
						 const gchar *uid);
guint		camel_folder_view_get_row_count	(CamelFolderView *self);
CamelFolderViewRow *
		camel_folder_view_get_row	(CamelFolderView *self,
						 guint row);

CamelFolderViewGeneration *
		camel_folder_view_ref_current_generation
						(CamelFolderView *self);
CamelFolderViewGeneration *
		camel_folder_view_generation_ref
						(CamelFolderViewGeneration *generation);
void		camel_folder_view_generation_unref
						(CamelFolderViewGeneration *generation);
guint		camel_folder_view_get_depth	(CamelFolderView *self,
						 const gchar *uid);
gboolean	camel_folder_view_is_expandable	(CamelFolderView *self,
						 const gchar *uid);
gboolean	camel_folder_view_get_expanded	(CamelFolderView *self,
						 const gchar *uid);
void		camel_folder_view_set_expanded	(CamelFolderView *self,
						 const gchar *uid,
						 gboolean expanded);

gboolean	camel_folder_view_is_group_row	(CamelFolderView *self,
						 guint row);
const gchar *	camel_folder_view_get_group_label
						(CamelFolderView *self,
						 guint row);

guint		camel_folder_view_get_group_count
						(CamelFolderView *self);

gchar *		camel_folder_view_save_expand_state
						(CamelFolderView *self);
void		camel_folder_view_load_expand_state
						(CamelFolderView *self,
						 const gchar *state);

G_END_DECLS

#endif /* CAMEL_FOLDER_VIEW_H */
