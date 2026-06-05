/*
 * SPDX-FileCopyrightText: (C) 2026 Red Hat (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "evolution-data-server-config.h"

#include <string.h>
#ifdef HAVE_MALLOC_TRIM
#include <malloc.h>
#endif

#include <glib/gi18n-lib.h>

/**
 * SECTION: camel-folder-view
 * @short_description: Sorted, threaded, filtered view of a mail folder
 * @include: camel/camel.h
 *
 * #CamelFolderView presents the contents of a #CamelFolder as a flat
 * row-indexed list suitable for display in a tree/list widget. It
 * handles threading (flat, full, compressed, subject-based), sorting
 * (multi-column), filtering (via S-expression search) and date-based
 * grouping.
 *
 * ## Lifecycle
 *
 * Configure properties right after construction, before connecting
 * any signal handlers. Each setter emits
 * #CamelFolderView::rebuild-needed, but that is harmless while no
 * handler is connected. Once the view is configured, connect
 * #CamelFolderView::rebuild-needed and #CamelFolderView::folder-changed,
 * then call camel_folder_view_rebuild_sync() from a worker thread.
 *
 * ## Rebuild model
 *
 * The view never rebuilds on its own. Every property change emits
 * #CamelFolderView::rebuild-needed (unless the view is frozen, in
 * which case the signal is deferred until thaw). The caller decides
 * when and on which thread to call camel_folder_view_rebuild_sync().
 * This keeps the expensive search / threading work off the main
 * thread. Folder content changes (#CamelFolder::changed) are handled
 * incrementally via #CamelFolderView::folder-changed - see below.
 *
 * A typical usage pattern looks like this:
 *
 * |[<!-- language="C" -->
 * // Initial setup - runs on the main thread
 * view = camel_folder_view_new (folder, TRUE);
 * camel_folder_view_set_threading (view, CAMEL_FOLDER_VIEW_THREADING_FULL);
 * camel_folder_view_set_sort (view, CAMEL_FOLDER_VIEW_COLUMN_DATE_SENT,
 *                             CAMEL_SORT_DESCENDING);
 * // connect signals, then schedule rebuild on a worker thread
 *
 * // Worker thread
 * camel_folder_view_rebuild_sync (view, cancellable, &error);
 * // on success the view's rows are ready to read
 *
 * // Later - user changes a setting
 * camel_folder_view_freeze (view);
 * camel_folder_view_set_threading (view, CAMEL_FOLDER_VIEW_THREADING_FLAT);
 * camel_folder_view_set_filter (view, sexp);
 * camel_folder_view_thaw (view);
 * // "rebuild-needed" fires once; schedule another rebuild
 * ]|
 *
 * ## Incremental changes
 *
 * When the underlying folder changes (new messages, removed messages,
 * flag changes), the view accumulates the changes internally and emits
 * #CamelFolderView::folder-changed, always on the main thread. The
 * listener should call camel_folder_view_ref_current_generation() right
 * there, then pass the result to camel_folder_view_process_pending_changes_sync()
 * from a worker thread:
 *
 * |[<!-- language="C" -->
 * static void
 * on_folder_changed (CamelFolderView *view, gpointer data)
 * {
 *     CamelFolderViewGeneration *old_generation =
 *         camel_folder_view_ref_current_generation (view);
 *     // schedule process_pending_changes_sync (view, old_generation, ...)
 *     // in a worker thread
 * }
 *
 * g_signal_connect (view, "folder-changed",
 *     G_CALLBACK (on_folder_changed), data);
 * ]|
 *
 * The function does search / I/O in the calling thread (safe off the
 * main thread), then applies tree mutations and emits row signals on
 * the main thread internally.
 *
 * If #CamelFolderView::rebuild-needed also fires, a full rebuild via
 * camel_folder_view_rebuild_sync() takes precedence and implicitly
 * discards any pending incremental changes.
 *
 * ## Signals and threading
 *
 * The #CamelFolderView::rows-changed, #CamelFolderView::rows-inserted,
 * #CamelFolderView::rows-removed and #CamelFolderView::row-count-changed
 * signals are always emitted on the main thread. Other signals may be
 * emitted from any thread.
 *
 * ## Freeze / thaw
 *
 * camel_folder_view_freeze() and camel_folder_view_thaw() batch
 * multiple property changes into a single rebuild-needed emission.
 * While frozen, property setters and folder-changed notifications
 * silently accumulate - no signal is emitted until the freeze count
 * drops to zero. Freeze/thaw calls nest: each freeze must be
 * paired with a thaw.
 *
 * ## Rows
 *
 * A #CamelFolderViewRow is a thin, borrowed handle onto the
 * #CamelMessageInfo backing it - the message info is the single
 * source of truth for a row's data. Every accessor reads straight
 * from it (or computes and caches a derived value the first time it
 * is asked for). Rows belong to a tree generation
 * (#CamelFolderViewGeneration) and are freed together with it, on the
 * next rebuild or incremental change. camel_folder_view_get_row() and
 * the other accessors that consult @self's live tree must be called
 * from the main thread, since that is the only thread which ever
 * replaces the current generation.
 *
 * To make a row outlive the call that produced it - e.g. to read it
 * later from a worker thread, or to hold onto it across a rebuild -
 * take a reference on its generation first with
 * camel_folder_view_ref_current_generation(), and release it with
 * camel_folder_view_generation_unref() once done with the row.
 *
 * ## Grouping
 *
 * Row indices include group header rows when grouping is active.
 * Use camel_folder_view_is_group_row() to distinguish them from
 * message rows.
 *
 * Since: 3.64
 **/

#include "camel-folder-view.h"
#include "camel-debug.h"
#include "camel-enumtypes.h"
#include "camel-folder-summary.h"
#include "camel-memchunk.h"
#include "camel-message-info.h"
#include "camel-sexp.h"
#include "camel-string-utils.h"
#include "camel-utils.h"
#include "camel-vee-folder.h"
#include "camel-vee-message-info.h"

typedef struct _CamelFolderViewRow ViewRow;

enum {
	DERIVED_SUBJECT_TRIMMED = 0,
	DERIVED_SUBJECT_NORM,
	DERIVED_SENDER,
	DERIVED_SENDER_MAIL,
	DERIVED_RECIPIENTS,
	DERIVED_RECIPIENTS_MAIL,
	DERIVED_CORRESPONDENTS,
	DERIVED_LABELS,
	DERIVED_USER_HEADER_1,
	DERIVED_USER_HEADER_2,
	DERIVED_USER_HEADER_3,
	N_DERIVED_SLOTS
};

struct _CamelFolderViewRow {
	ViewRow *next;
	ViewRow *parent;
	ViewRow *child;
	CamelFolderView *view;
	CamelMessageInfo *info;
	GArray *references;
	gchar *root_subject;
	CamelSummaryMessageID id_table_key;
	guint32 order;
	gint64 sort_date;
	gint64 thread_latest_date;
	guint visible_index;
	guint has_id_table_key : 1;
	guint is_group : 1;
	guint expanded : 1;
	guint re : 1;
	gchar *group_label;
	/* lazily allocated, invalidated when data_stamp != view->data_stamp */
	const gchar **derived;
	guint data_stamp;
};

static void view_row_clear_fields (ViewRow *row);
static void view_row_clear_derived_cache (ViewRow *row);

typedef struct _SortColumn {
	CamelFolderViewColumn column;
	CamelSortType order;
} SortColumn;

typedef struct {
	GHashTable *id_table;
	GHashTable *uid_to_node;
	CamelMemChunk *node_chunks;
} ViewBuildCtx;

struct _CamelFolderViewGeneration {
	gint ref_count; /* atomic */
	GHashTable *id_table;
	GHashTable *uid_to_node;
	CamelMemChunk *node_chunks;
	ViewRow *root_children;
};

struct _CamelFolderView {
	GObject parent;

	CamelFolder *folder;
	gchar *filter_sexp;
	const gchar *ensure_uid;
	gboolean show_deleted;
	gboolean show_junk;
	CamelFolderViewThreading threading;
	gboolean thread_subject;
	gboolean thread_latest;
	gboolean sort_children_ascending;
	gchar *localized_re;
	gchar **localized_re_separators;
	GArray *sort_columns;
	CamelFolderViewGroupBy group_by;
	guint freeze_count;
	gboolean rebuild_needed;
	gboolean sort_only_changed;

	CamelFolderViewGeneration *current_gen;

	GPtrArray *visible_rows;
	gboolean visible_dirty;

	GHashTable *expanded_uids;
	gboolean default_expanded;

	GHashTable *own_addresses;
	gchar *user_headers[CAMEL_UTILS_MAX_USER_HEADERS];

	/* bump to lazily invalidate every row's derived cache */
	guint data_stamp;

	gulong folder_changed_handler;

	GMutex pending_changes_lock;
	CamelFolderChangeInfo *pending_changes;
	gint incremental_pending;
};

enum {
	SIGNAL_ROWS_CHANGED,
	SIGNAL_ROWS_INSERTED,
	SIGNAL_ROWS_REMOVED,
	SIGNAL_ROW_COUNT_CHANGED,
	SIGNAL_REBUILD_NEEDED,
	SIGNAL_FOLDER_CHANGED,
	LAST_SIGNAL
};

static guint signals[LAST_SIGNAL];

enum {
	PROP_0,
	PROP_FOLDER,
	PROP_THREADING,
	PROP_THREAD_SUBJECT,
	PROP_FILTER,
	PROP_SHOW_DELETED,
	PROP_SHOW_JUNK,
	PROP_GROUP_BY,
	PROP_THREAD_LATEST,
	PROP_SORT_CHILDREN_ASCENDING,
	PROP_LOCALIZED_RE,
	PROP_LOCALIZED_RE_SEPARATORS,
	PROP_DEFAULT_EXPANDED,
	N_PROPERTIES
};

static GParamSpec *properties[N_PROPERTIES];

G_DEFINE_TYPE (CamelFolderView, camel_folder_view, G_TYPE_OBJECT)

static guint
id_hash (gconstpointer key)
{
	const CamelSummaryMessageID *id = key;
	return id->id.part.lo;
}

static gboolean
id_equal (gconstpointer a,
          gconstpointer b)
{
	return ((const CamelSummaryMessageID *) a)->id.id ==
	       ((const CamelSummaryMessageID *) b)->id.id;
}

static void
view_row_add_child (ViewRow *parent_row,
                    ViewRow *child_row)
{
	child_row->next = parent_row->child;
	parent_row->child = child_row;
	child_row->parent = parent_row;
}

/* Deliberate aliasing: treats a ViewRow* variable's address (or a
 * ViewRow** parameter) as a ViewRow* to reuse its first field (next)
 * as a fake list-head sentinel; only ->next is ever touched this way. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Warray-bounds"
static void
view_row_parent_child (ViewRow *parent_row,
                       ViewRow *child_row)
{
	ViewRow *cc, *row;

	if (parent_row == child_row || child_row->parent == parent_row)
		return;

	row = parent_row->parent;
	while (row) {
		if (row == child_row)
			return;
		row = row->parent;
	}

	if (child_row->parent == NULL) {
		view_row_add_child (parent_row, child_row);
		return;
	}

	row = child_row->parent;
	cc = (ViewRow *) &row->child;
	while (cc->next) {
		if (cc->next == child_row) {
			cc->next = cc->next->next;
			child_row->parent = NULL;
			view_row_add_child (parent_row, child_row);
			return;
		}
		cc = cc->next;
	}
}
#pragma GCC diagnostic pop

static void
folder_view_remove_node_from_id_table (ViewBuildCtx *ctx,
                                       ViewRow *row)
{
	if (row->has_id_table_key) {
		g_hash_table_remove (ctx->id_table, &row->id_table_key);
		row->has_id_table_key = FALSE;
	}
}

/* Deliberate aliasing: treats a ViewRow* variable's address (or a
 * ViewRow** parameter) as a ViewRow* to reuse its first field (next)
 * as a fake list-head sentinel; only ->next is ever touched this way. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Warray-bounds"
static gboolean
prune_empty (ViewBuildCtx *ctx,
             ViewRow **cp,
             GCancellable *cancellable,
             GError **error)
{
	ViewRow *child, *next, *cc, *lastc;

	if (g_cancellable_set_error_if_cancelled (cancellable, error))
		return FALSE;

	lastc = (ViewRow *) cp;
	while (lastc->next) {
		cc = lastc->next;
		if (!prune_empty (ctx, &cc->child, cancellable, error))
			return FALSE;

		if (cc->info == NULL) {
			if (cc->child == NULL) {
				lastc->next = cc->next;
				folder_view_remove_node_from_id_table (ctx, cc);
				view_row_clear_fields (cc);
				camel_memchunk_free (ctx->node_chunks, cc);
				continue;
			}
			if (cc->parent || cc->child->next == NULL) {
				lastc->next = cc->next;
				folder_view_remove_node_from_id_table (ctx, cc);
				child = cc->child;
				while (child) {
					next = child->next;
					child->parent = cc->parent;
					child->next = lastc->next;
					lastc->next = child;
					child = next;
				}
				continue;
			}
		}
		lastc = cc;
	}

	return TRUE;
}
#pragma GCC diagnostic pop

/* Deliberate aliasing: treats a ViewRow* variable's address (or a
 * ViewRow** parameter) as a ViewRow* to reuse its first field (next)
 * as a fake list-head sentinel; only ->next is ever touched this way. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Warray-bounds"
static gboolean
folder_view_prune_phantom_roots (ViewBuildCtx *ctx,
                                 ViewRow **head,
                                 GCancellable *cancellable,
                                 GError **error)
{
	ViewRow *cc, *child;

	if (g_cancellable_set_error_if_cancelled (cancellable, error))
		return FALSE;

	cc = (ViewRow *) head;
	while (cc && cc->next) {
		ViewRow *row;

		child = cc->next;
		if (child->info == NULL) {
			ViewRow *members = child->child;

			if (members) {
				ViewRow *best = members;
				ViewRow *prev;
				gint64 best_date = members->sort_date;

				for (row = members->next; row; row = row->next) {
					gint64 row_date = row->sort_date;

					if (row_date != 0 && row_date != -1 &&
					    (row_date < best_date || best_date == 0 || best_date == -1)) {
						best_date = row_date;
						best = row;
					}
				}

				if (members == best) {
					members = best->next;
				} else {
					prev = members;
					while (prev->next != best) {
						prev = prev->next;
					}
					prev->next = best->next;
				}

				for (row = members; row; row = row->next) {
					row->parent = best;
				}

				best->next = child->next;
				best->parent = NULL;

				if (best->child) {
					ViewRow *last = best->child;

					while (last->next) {
						last = last->next;
					}
					last->next = members;
				} else {
					best->child = members;
				}

				cc->next = best;
				cc = best;
			} else {
				cc->next = child->next;
			}
			folder_view_remove_node_from_id_table (ctx, child);
			view_row_clear_fields (child);
			camel_memchunk_free (ctx->node_chunks, child);
		} else {
			cc = child;
		}
	}

	return TRUE;
}
#pragma GCC diagnostic pop

static void
folder_view_thread_node_by_refs (CamelFolderView *self,
                                 ViewBuildCtx *ctx,
                                 ViewRow *row,
                                 const GArray *references,
                                 gboolean create_placeholders)
{
	CamelSummaryMessageID mid_val;
	guint jj;

	if (self->threading == CAMEL_FOLDER_VIEW_THREADING_NONE || !references || references->len == 0)
		return;

	if (self->threading == CAMEL_FOLDER_VIEW_THREADING_FLAT) {
		ViewRow *thread_root = NULL, *cc;

		for (jj = 0; jj < references->len; jj++) {
			mid_val.id.id = g_array_index (references, guint64, jj);
			if (!mid_val.id.id)
				continue;

			cc = g_hash_table_lookup (ctx->id_table, &mid_val);
			if (cc) {
				while (cc->parent) {
					cc = cc->parent;
				}
				thread_root = cc;
				break;
			}
		}

		if (create_placeholders) {
			for (jj = 0; jj < references->len; jj++) {
				mid_val.id.id = g_array_index (references, guint64, jj);
				if (!mid_val.id.id)
					continue;

				cc = g_hash_table_lookup (ctx->id_table, &mid_val);
				if (!cc) {
					cc = camel_memchunk_alloc0 (ctx->node_chunks);
					cc->view = self;
					cc->expanded = self->default_expanded;
					cc->id_table_key.id.id = mid_val.id.id;
					cc->has_id_table_key = TRUE;
					g_hash_table_insert (ctx->id_table, &cc->id_table_key, cc);

					if (thread_root)
						view_row_parent_child (thread_root, cc);
					else
						thread_root = cc;
				} else {
					ViewRow *rr = cc;

					while (rr->parent) {
						rr = rr->parent;
					}

					if (!thread_root) {
						thread_root = rr;
					} else if (rr != thread_root) {
						ViewRow *ch = rr->child;

						rr->child = NULL;
						while (ch) {
							ViewRow *nx = ch->next;
							ch->next = NULL;
							ch->parent = NULL;
							view_row_parent_child (thread_root, ch);
							ch = nx;
						}
						view_row_parent_child (thread_root, rr);
					}
				}
			}
		}

		if (thread_root && thread_root != row)
			view_row_parent_child (thread_root, row);
	} else {
		ViewRow *child = row, *cc;

		if (create_placeholders) {
			for (jj = 0; jj < references->len; jj++) {
				gboolean found = FALSE;

				mid_val.id.id = g_array_index (references, guint64, jj);
				if (!mid_val.id.id)
					continue;

				cc = g_hash_table_lookup (ctx->id_table, &mid_val);
				if (cc == NULL) {
					cc = camel_memchunk_alloc0 (ctx->node_chunks);
					cc->view = self;
					cc->expanded = self->default_expanded;
					cc->id_table_key.id.id = mid_val.id.id;
					cc->has_id_table_key = TRUE;
					g_hash_table_insert (ctx->id_table, &cc->id_table_key, cc);
				} else {
					found = TRUE;
				}

				if (cc != child) {
					view_row_parent_child (cc, child);
					if (found)
						break;
				}
				child = cc;
			}
		} else {
			for (jj = 0; jj < references->len; jj++) {
				mid_val.id.id = g_array_index (references, guint64, jj);
				if (!mid_val.id.id)
					continue;

				cc = g_hash_table_lookup (ctx->id_table, &mid_val);
				if (cc && cc != row) {
					view_row_parent_child (cc, row);
					break;
				}
			}
		}
	}
}

static gchar *
skip_list_ids (gchar *s)
{
	gchar *p;

	while (g_ascii_isspace (*s)) {
		s++;
	}

	while (*s == '[') {
		p = s + 1;
		while (*p && *p != ']' && !g_ascii_isspace (*p)) {
			p++;
		}
		if (*p != ']')
			break;
		s = p + 1;
		while (g_ascii_isspace (*s)) {
			s++;
		}
		if (*s == '-' && g_ascii_isspace (s[1]))
			s += 2;
		while (g_ascii_isspace (*s)) {
			s++;
		}
	}

	return s;
}

static gboolean
check_re_prefix (const gchar *s,
                 const gchar *prefix,
                 const gchar * const *separators,
                 gint *skip_len)
{
	const gchar *start = s;
	gint prefix_len = strlen (prefix);

	if (g_ascii_strncasecmp (s, prefix, prefix_len) != 0)
		return FALSE;

	s += prefix_len;

	while (g_ascii_isdigit (*s) || (g_ascii_ispunct (*s) && *s != ':')) {
		s++;
	}

	if (*s == ':') {
		*skip_len = (gint) (s + 1 - start);
		return TRUE;
	}

	if (separators) {
		guint ii;
		for (ii = 0; separators[ii]; ii++) {
			gint sep_len = strlen (separators[ii]);
			if (sep_len > 0 && g_ascii_strncasecmp (s, separators[ii], sep_len) == 0) {
				*skip_len = (gint) (s + sep_len - start);
				return TRUE;
			}
		}
	}

	return FALSE;
}

static const gchar *
skip_re_prefixes (const gchar *s,
                  const gchar *localized_re,
                  const gchar * const *localized_separators,
                  gboolean *found_re)
{
	gboolean any_found;

	*found_re = FALSE;

	s = skip_list_ids ((gchar *) s);

	do {
		gint skip_len = 0;

		any_found = FALSE;

		while (g_ascii_isspace (*s)) {
			s++;
		}

		if (*s == 0)
			break;

		if (check_re_prefix (s, "Re", localized_separators, &skip_len) ||
		    check_re_prefix (s, "Fwd", localized_separators, &skip_len)) {
			*found_re = TRUE;
			s += skip_len;
			s = skip_list_ids ((gchar *) s);
			any_found = TRUE;
			continue;
		}

		if (localized_re && *localized_re) {
			gchar **prefixes = g_strsplit (localized_re, ",", -1);
			guint ii;

			for (ii = 0; prefixes[ii]; ii++) {
				g_strstrip (prefixes[ii]);
				if (*prefixes[ii] &&
				    check_re_prefix (s, prefixes[ii], localized_separators, &skip_len)) {
					*found_re = TRUE;
					s += skip_len;
					s = skip_list_ids ((gchar *) s);
					any_found = TRUE;
					break;
				}
			}
			g_strfreev (prefixes);
		}
	} while (any_found);

	while (g_ascii_isspace (*s)) {
		s++;
	}

	return s;
}

static gchar *
view_get_root_subject (CamelFolderView *self,
                       ViewRow *cc)
{
	const gchar *str;
	ViewRow *scan;

	str = NULL;
	cc->re = FALSE;

	if (cc->info)
		str = camel_message_info_get_subject (cc->info);
	else if (cc->root_subject)
		str = cc->root_subject;
	else {
		scan = cc->child;
		while (scan) {
			if (scan->info) {
				str = camel_message_info_get_subject (scan->info);
				break;
			}
			if (scan->root_subject) {
				str = scan->root_subject;
				break;
			}
			scan = scan->next;
		}
	}

	if (str != NULL) {
		gboolean found_re = FALSE;
		const gchar *stripped;

		stripped = skip_re_prefixes (str, self->localized_re,
			(const gchar * const *) self->localized_re_separators,
			&found_re);
		cc->re = found_re;

		if (stripped && *stripped)
			return g_strdup (stripped);
	}

	return NULL;
}

/* Deliberate aliasing: treats a ViewRow* variable's address (or a
 * ViewRow** parameter) as a ViewRow* to reuse its first field (next)
 * as a fake list-head sentinel; only ->next is ever touched this way. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Warray-bounds"
static void
view_remove_row (ViewRow **list,
                 ViewRow *row,
                 ViewRow **clast)
{
	ViewRow *cc;

	if (row->parent)
		cc = (ViewRow *) &row->parent->child;
	else
		cc = (ViewRow *) list;

	while (cc->next) {
		if (cc->next == row) {
			if (*clast == cc->next)
				*clast = cc;
			cc->next = cc->next->next;
			return;
		}
		cc = cc->next;
	}
}
#pragma GCC diagnostic pop

/* Deliberate aliasing: treats a ViewRow* variable's address (or a
 * ViewRow** parameter) as a ViewRow* to reuse its first field (next)
 * as a fake list-head sentinel; only ->next is ever touched this way. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Warray-bounds"
static gboolean
group_root_set (CamelFolderView *self,
                ViewBuildCtx *ctx,
                ViewRow **cp,
                GCancellable *cancellable,
                GError **error)
{
	GHashTable *subject_table = g_hash_table_new (g_str_hash, g_str_equal);
	ViewRow *cc, *clast, *scan, *container;

	if (g_cancellable_set_error_if_cancelled (cancellable, error)) {
		g_hash_table_destroy (subject_table);
		return FALSE;
	}

	clast = (ViewRow *) cp;
	cc = clast->next;
	while (cc) {
		gchar *old_subject = cc->root_subject;

		cc->root_subject = view_get_root_subject (self, cc);
		g_clear_pointer (&old_subject, g_free);
		if (cc->root_subject) {
			container = g_hash_table_lookup (subject_table, cc->root_subject);
			if (container == NULL ||
			    (container->info == NULL && cc->info) ||
			    (container->re == TRUE && !cc->re)) {
				g_hash_table_insert (subject_table, cc->root_subject, cc);
			}
		}
		cc = cc->next;
	}

	clast = (ViewRow *) cp;
	while (clast->next) {
		cc = clast->next;
		if (cc->root_subject &&
		    (container = g_hash_table_lookup (subject_table, cc->root_subject)) &&
		    (container != cc)) {
			if (cc->info == NULL && container->info == NULL) {
				scan = (ViewRow *) &container->child;
				while (scan->next) {
					scan = scan->next;
				}
				scan->next = cc->child;
				clast->next = cc->next;
				folder_view_remove_node_from_id_table (ctx, cc);
				view_row_clear_fields (cc);
				camel_memchunk_free (ctx->node_chunks, cc);
				continue;
			} else if (cc->info == NULL && container->info != NULL) {
				view_remove_row (cp, container, &clast);
				view_row_add_child (cc, container);
			} else if (cc->info != NULL && container->info == NULL) {
				clast->next = cc->next;
				view_row_add_child (container, cc);
				continue;
			} else if (cc->re && !container->re) {
				clast->next = cc->next;
				view_row_add_child (container, cc);
				continue;
			} else if (!cc->re && container->re) {
				view_remove_row (cp, container, &clast);
				view_row_add_child (cc, container);
			} else {
				view_remove_row (cp, container, &clast);
				view_remove_row (cp, cc, &clast);
				scan = camel_memchunk_alloc0 (ctx->node_chunks);
				scan->view = self;
				scan->root_subject = g_steal_pointer (&cc->root_subject);
				scan->re = cc->re && container->re;
				scan->next = cc->next;
				clast->next = scan;
				view_row_add_child (scan, cc);
				view_row_add_child (scan, container);
				clast = scan;
				g_hash_table_insert (subject_table, scan->root_subject, scan);
				continue;
			}
		}
		clast = cc;
	}

	g_hash_table_destroy (subject_table);

	return TRUE;
}
#pragma GCC diagnostic pop

static void
hashloop (gpointer key,
          gpointer value,
          gpointer data)
{
	ViewRow *cc = value;
	ViewRow *tail = data;

	if (cc->parent == NULL) {
		cc->next = tail->next;
		tail->next = cc;
	}
}

static gint64
compute_latest_date (ViewRow *row)
{
	gint64 latest = row->sort_date;
	ViewRow *child;

	for (child = row->child; child; child = child->next) {
		gint64 child_latest = compute_latest_date (child);
		if (child_latest > latest)
			latest = child_latest;
	}

	return latest;
}

static gboolean
compute_thread_latest_dates (ViewRow *roots,
                             GCancellable *cancellable,
                             GError **error)
{
	ViewRow *row;

	if (g_cancellable_set_error_if_cancelled (cancellable, error))
		return FALSE;

	for (row = roots; row; row = row->next) {
		if (row->is_group) {
			ViewRow *child;
			for (child = row->child; child; child = child->next) {
				child->thread_latest_date = compute_latest_date (child);
			}
		} else {
			row->thread_latest_date = compute_latest_date (row);
		}
	}

	return TRUE;
}

static gint
compare_int64 (gint64 a,
               gint64 b)
{
	return (a < b) ? -1 : (a > b) ? 1 : 0;
}

static gint
compare_uint32 (guint32 a,
                guint32 b)
{
	return (a < b) ? -1 : (a > b) ? 1 : 0;
}

static gint
compare_collate (const gchar *a,
                 const gchar *b)
{
	if (!a)
		a = "";
	if (!b)
		b = "";
	return g_utf8_collate (a, b);
}

static gint
compare_string (const gchar *a,
                const gchar *b)
{
	return g_strcmp0 (a, b);
}

static gint
compare_caseless (const gchar *a,
                  const gchar *b)
{
	if (!a)
		a = "";
	if (!b)
		b = "";
	return g_ascii_strcasecmp (a, b);
}

static const gchar *
get_trimmed_subject_from_strings (CamelFolderView *self,
                                  const gchar *subject,
                                  const gchar *mlist)
{
	gint mlist_len = 0;
	gboolean found_mlist;

	if (!subject || !*subject)
		return subject;

	if (mlist && *mlist) {
		const gchar *mlist_end;

		mlist_end = strchr (mlist, '@');
		if (mlist_end)
			mlist_len = mlist_end - mlist;
		else
			mlist_len = strlen (mlist);
	}

	do {
		gboolean found_re = TRUE;

		found_mlist = FALSE;

		while (found_re) {
			subject = skip_re_prefixes (subject, self->localized_re,
				(const gchar * const *) self->localized_re_separators,
				&found_re);

			while (*subject && g_ascii_isspace (*subject)) {
				subject++;
			}
		}

		if (mlist_len &&
		    *subject == '[' &&
		    !g_ascii_strncasecmp (subject + 1, mlist, mlist_len) &&
		    subject[1 + mlist_len] == ']') {
			subject += 1 + mlist_len + 1;
			found_mlist = TRUE;

			while (*subject && g_ascii_isspace (*subject)) {
				subject++;
			}
		}
	} while (found_mlist);

	while (*subject && g_ascii_isspace (*subject)) {
		subject++;
	}

	return subject;
}

static void
add_name_or_email (GString *addresses,
                   const gchar *address,
                   gint addr_start,
                   gboolean use_name)
{
	if (!address || !*address)
		return;

	while (*address == ' ') {
		if (addr_start >= 0)
			addr_start--;
		address++;
	}

	if (addresses->len)
		g_string_append_c (addresses, ' ');

	if (addr_start < 0) {
		g_string_append (addresses, address);
	} else if (use_name) {
		g_string_append_len (addresses, address, addr_start - 1);
	} else {
		const gchar *addr_end = strrchr (address + addr_start, '>');

		if (addr_end)
			g_string_append_len (addresses, address + addr_start,
				addr_end - address - addr_start);
		else
			g_string_append (addresses, address + addr_start);
	}
}

static gchar *
sanitize_addresses (const gchar *string,
                    gboolean return_names)
{
	GString *gstring;
	gboolean quoted = FALSE;
	const gchar *p;
	gint addr_start = -1;
	GString *addresses = g_string_new ("");

	if (!string || !*string)
		return g_string_free (addresses, FALSE);

	gstring = g_string_new ("");

	for (p = string; *p; p = g_utf8_next_char (p)) {
		gunichar c = g_utf8_get_char (p);

		if (c == '"') {
			quoted = ~quoted;
		} else if (c == '<' && !quoted && addr_start == -1) {
			addr_start = gstring->len + 1;
		} else if (c == ',' && !quoted) {
			add_name_or_email (addresses, gstring->str, addr_start, return_names);
			g_string_append_c (addresses, ',');
			g_string_truncate (gstring, 0);
			addr_start = -1;
			continue;
		}

		g_string_append_unichar (gstring, c);
	}

	add_name_or_email (addresses, gstring->str, addr_start, return_names);
	g_string_free (gstring, TRUE);

	return g_string_free (addresses, FALSE);
}

static gchar *
build_labels_string (CamelMessageInfo *info)
{
	const CamelNamedFlags *user_flags;
	GString *result = NULL;
	guint ii, len;

	user_flags = camel_message_info_get_user_flags (info);
	if (!user_flags)
		return NULL;

	len = camel_named_flags_get_length (user_flags);
	for (ii = 0; ii < len; ii++) {
		const gchar *name = camel_named_flags_get (user_flags, ii);

		if (name) {
			if (!result)
				result = g_string_new (name);
			else
				g_string_append_printf (result, ",%s", name);
		}
	}

	return result ? g_string_free (result, FALSE) : NULL;
}

static const gchar **
view_row_derived_slots (ViewRow *row)
{
	if (row->derived && row->data_stamp != row->view->data_stamp)
		view_row_clear_derived_cache (row);

	if (!row->derived) {
		row->derived = g_new0 (const gchar *, N_DERIVED_SLOTS);
		row->data_stamp = row->view->data_stamp;
	}

	return row->derived;
}

/* an interned "" stands for a memoized NULL, so it isn't recomputed every call */
static const gchar *
view_row_derived_or_null (const gchar *value)
{
	return (value && *value) ? value : NULL;
}

static const gchar *
view_row_get_subject_norm (ViewRow *row)
{
	const gchar **slots;

	if (!row->info)
		return NULL;

	slots = view_row_derived_slots (row);
	if (!slots[DERIVED_SUBJECT_NORM]) {
		const gchar *trimmed;

		camel_message_info_property_lock (row->info);

		trimmed = get_trimmed_subject_from_strings (row->view,
			camel_message_info_get_subject (row->info),
			camel_message_info_get_mlist (row->info));

		if (trimmed && *trimmed)
			slots[DERIVED_SUBJECT_NORM] = camel_pstring_add (g_utf8_collate_key (trimmed, -1), TRUE);
		else
			slots[DERIVED_SUBJECT_NORM] = camel_pstring_strdup ("");

		camel_message_info_property_unlock (row->info);
	}

	return slots[DERIVED_SUBJECT_NORM];
}

/**
 * camel_folder_view_row_lock:
 * @row: a #CamelFolderViewRow
 *
 * Locks the row to prevent concurrent changes from other threads.
 * Unlock with camel_folder_view_row_unlock().
 *
 * Note: See camel_folder_view_get_row() for @row's validity.
 *
 * Since: 3.64
 **/
void
camel_folder_view_row_lock (CamelFolderViewRow *row)
{
	g_return_if_fail (row != NULL);

	if (row->info)
		camel_message_info_property_lock (row->info);
}

/**
 * camel_folder_view_row_unlock:
 * @row: a #CamelFolderViewRow
 *
 * Releases the lock taken with camel_folder_view_row_lock().
 *
 * Note: See camel_folder_view_get_row() for @row's validity.
 *
 * Since: 3.64
 **/
void
camel_folder_view_row_unlock (CamelFolderViewRow *row)
{
	g_return_if_fail (row != NULL);

	if (row->info)
		camel_message_info_property_unlock (row->info);
}

/**
 * camel_folder_view_row_get_uid:
 * @row: a #CamelFolderViewRow
 *
 * Returns the message UID, or %NULL for a group header row.
 *
 * Returns: (transfer none) (nullable): the UID string
 *
 * Note: See camel_folder_view_get_row() for @row's validity.
 *
 * Since: 3.64
 **/
const gchar *
camel_folder_view_row_get_uid (CamelFolderViewRow *row)
{
	g_return_val_if_fail (row != NULL, NULL);

	return row->info ? camel_message_info_get_uid (row->info) : NULL;
}

/**
 * camel_folder_view_row_get_flags:
 * @row: a #CamelFolderViewRow
 *
 * Returns the #CamelMessageFlags.
 *
 * Returns: message flags
 *
 * Note: See camel_folder_view_get_row() for @row's validity.
 *
 * Since: 3.64
 **/
guint32
camel_folder_view_row_get_flags (CamelFolderViewRow *row)
{
	g_return_val_if_fail (row != NULL, 0);

	return row->info ? camel_message_info_get_flags (row->info) : 0;
}

/**
 * camel_folder_view_row_get_date_sent:
 * @row: a #CamelFolderViewRow
 *
 * Returns the sent date as a Unix timestamp.
 *
 * Returns: sent date, or 0
 *
 * Note: See camel_folder_view_get_row() for @row's validity.
 *
 * Since: 3.64
 **/
gint64
camel_folder_view_row_get_date_sent (CamelFolderViewRow *row)
{
	g_return_val_if_fail (row != NULL, 0);

	return row->info ? camel_message_info_get_date_sent (row->info) : 0;
}

/**
 * camel_folder_view_row_get_date_received:
 * @row: a #CamelFolderViewRow
 *
 * Returns the received date as a Unix timestamp.
 *
 * Returns: received date, or 0
 *
 * Note: See camel_folder_view_get_row() for @row's validity.
 *
 * Since: 3.64
 **/
gint64
camel_folder_view_row_get_date_received (CamelFolderViewRow *row)
{
	g_return_val_if_fail (row != NULL, 0);

	return row->info ? camel_message_info_get_date_received (row->info) : 0;
}

/**
 * camel_folder_view_row_get_size:
 * @row: a #CamelFolderViewRow
 *
 * Returns the message size in bytes.
 *
 * Returns: size in bytes, or 0
 *
 * Note: See camel_folder_view_get_row() for @row's validity.
 *
 * Since: 3.64
 **/
guint32
camel_folder_view_row_get_size (CamelFolderViewRow *row)
{
	g_return_val_if_fail (row != NULL, 0);

	return row->info ? camel_message_info_get_size (row->info) : 0;
}

/**
 * camel_folder_view_row_get_subject:
 * @row: a #CamelFolderViewRow
 *
 * Returns the raw subject header.
 *
 * Returns: (transfer none) (nullable): the subject, or %NULL
 *
 * Note: See camel_folder_view_get_row() for @row's validity.
 *
 * Since: 3.64
 **/
const gchar *
camel_folder_view_row_get_subject (CamelFolderViewRow *row)
{
	g_return_val_if_fail (row != NULL, NULL);

	return row->info ? camel_message_info_get_subject (row->info) : NULL;
}

/**
 * camel_folder_view_row_get_from:
 * @row: a #CamelFolderViewRow
 *
 * Returns the raw From header.
 *
 * Returns: (transfer none) (nullable): the From address, or %NULL
 *
 * Note: See camel_folder_view_get_row() for @row's validity.
 *
 * Since: 3.64
 **/
const gchar *
camel_folder_view_row_get_from (CamelFolderViewRow *row)
{
	g_return_val_if_fail (row != NULL, NULL);

	return row->info ? camel_message_info_get_from (row->info) : NULL;
}

/**
 * camel_folder_view_row_get_to:
 * @row: a #CamelFolderViewRow
 *
 * Returns the raw To header.
 *
 * Returns: (transfer none) (nullable): the To address, or %NULL
 *
 * Note: See camel_folder_view_get_row() for @row's validity.
 *
 * Since: 3.64
 **/
const gchar *
camel_folder_view_row_get_to (CamelFolderViewRow *row)
{
	g_return_val_if_fail (row != NULL, NULL);

	return row->info ? camel_message_info_get_to (row->info) : NULL;
}

/**
 * camel_folder_view_row_get_cc:
 * @row: a #CamelFolderViewRow
 *
 * Returns the raw CC header.
 *
 * Returns: (transfer none) (nullable): the CC address, or %NULL
 *
 * Note: See camel_folder_view_get_row() for @row's validity.
 *
 * Since: 3.64
 **/
const gchar *
camel_folder_view_row_get_cc (CamelFolderViewRow *row)
{
	g_return_val_if_fail (row != NULL, NULL);

	return row->info ? camel_message_info_get_cc (row->info) : NULL;
}

/**
 * camel_folder_view_row_get_mlist:
 * @row: a #CamelFolderViewRow
 *
 * Returns the mailing list identifier.
 *
 * Returns: (transfer none) (nullable): the mailing list, or %NULL
 *
 * Note: See camel_folder_view_get_row() for @row's validity.
 *
 * Since: 3.64
 **/
const gchar *
camel_folder_view_row_get_mlist (CamelFolderViewRow *row)
{
	g_return_val_if_fail (row != NULL, NULL);

	return row->info ? camel_message_info_get_mlist (row->info) : NULL;
}

/**
 * camel_folder_view_row_get_preview:
 * @row: a #CamelFolderViewRow
 *
 * Returns the body preview text.
 *
 * Returns: (transfer none) (nullable): the preview, or %NULL
 *
 * Note: See camel_folder_view_get_row() for @row's validity.
 *
 * Since: 3.64
 **/
const gchar *
camel_folder_view_row_get_preview (CamelFolderViewRow *row)
{
	g_return_val_if_fail (row != NULL, NULL);

	return row->info ? camel_message_info_get_preview (row->info) : NULL;
}

/**
 * camel_folder_view_row_get_subject_trimmed:
 * @row: a #CamelFolderViewRow
 *
 * Returns the subject with Re:/Fwd: prefixes and mailing list
 * tags stripped.
 *
 * Returns: (transfer none) (nullable): the trimmed subject, or %NULL
 *
 * Note: See camel_folder_view_get_row() for @row's validity.
 *
 * Since: 3.64
 **/
const gchar *
camel_folder_view_row_get_subject_trimmed (CamelFolderViewRow *row)
{
	const gchar **slots;

	g_return_val_if_fail (row != NULL, NULL);

	if (!row->info)
		return NULL;

	slots = view_row_derived_slots (row);
	if (!slots[DERIVED_SUBJECT_TRIMMED]) {
		const gchar *trimmed;

		camel_message_info_property_lock (row->info);

		trimmed = get_trimmed_subject_from_strings (row->view,
			camel_message_info_get_subject (row->info),
			camel_message_info_get_mlist (row->info));

		slots[DERIVED_SUBJECT_TRIMMED] = trimmed ? camel_pstring_strdup (trimmed) : camel_pstring_strdup ("");

		camel_message_info_property_unlock (row->info);
	}

	return view_row_derived_or_null (slots[DERIVED_SUBJECT_TRIMMED]);
}

/**
 * camel_folder_view_row_get_sender:
 * @row: a #CamelFolderViewRow
 *
 * Returns display names extracted from the From address.
 *
 * Returns: (transfer none) (nullable): the sender display names, or %NULL
 *
 * Note: See camel_folder_view_get_row() for @row's validity.
 *
 * Since: 3.64
 **/
const gchar *
camel_folder_view_row_get_sender (CamelFolderViewRow *row)
{
	const gchar **slots;

	g_return_val_if_fail (row != NULL, NULL);

	if (!row->info)
		return NULL;

	slots = view_row_derived_slots (row);
	if (!slots[DERIVED_SENDER]) {
		camel_message_info_property_lock (row->info);
		slots[DERIVED_SENDER] = camel_pstring_add (sanitize_addresses (camel_message_info_get_from (row->info), TRUE), TRUE);
		camel_message_info_property_unlock (row->info);
	}

	return view_row_derived_or_null (slots[DERIVED_SENDER]);
}

/**
 * camel_folder_view_row_get_sender_mail:
 * @row: a #CamelFolderViewRow
 *
 * Returns email addresses extracted from the From address.
 *
 * Returns: (transfer none) (nullable): the sender email addresses, or %NULL
 *
 * Note: See camel_folder_view_get_row() for @row's validity.
 *
 * Since: 3.64
 **/
const gchar *
camel_folder_view_row_get_sender_mail (CamelFolderViewRow *row)
{
	const gchar **slots;

	g_return_val_if_fail (row != NULL, NULL);

	if (!row->info)
		return NULL;

	slots = view_row_derived_slots (row);
	if (!slots[DERIVED_SENDER_MAIL]) {
		camel_message_info_property_lock (row->info);
		slots[DERIVED_SENDER_MAIL] = camel_pstring_add (sanitize_addresses (camel_message_info_get_from (row->info), FALSE), TRUE);
		camel_message_info_property_unlock (row->info);
	}

	return view_row_derived_or_null (slots[DERIVED_SENDER_MAIL]);
}

/**
 * camel_folder_view_row_get_recipients:
 * @row: a #CamelFolderViewRow
 *
 * Returns display names extracted from the To address.
 *
 * Returns: (transfer none) (nullable): the recipient display names, or %NULL
 *
 * Note: See camel_folder_view_get_row() for @row's validity.
 *
 * Since: 3.64
 **/
const gchar *
camel_folder_view_row_get_recipients (CamelFolderViewRow *row)
{
	const gchar **slots;

	g_return_val_if_fail (row != NULL, NULL);

	if (!row->info)
		return NULL;

	slots = view_row_derived_slots (row);
	if (!slots[DERIVED_RECIPIENTS]) {
		camel_message_info_property_lock (row->info);
		slots[DERIVED_RECIPIENTS] = camel_pstring_add (sanitize_addresses (camel_message_info_get_to (row->info), TRUE), TRUE);
		camel_message_info_property_unlock (row->info);
	}

	return view_row_derived_or_null (slots[DERIVED_RECIPIENTS]);
}

/**
 * camel_folder_view_row_get_recipients_mail:
 * @row: a #CamelFolderViewRow
 *
 * Returns email addresses extracted from the To address.
 *
 * Returns: (transfer none) (nullable): the recipient email addresses, or %NULL
 *
 * Note: See camel_folder_view_get_row() for @row's validity.
 *
 * Since: 3.64
 **/
const gchar *
camel_folder_view_row_get_recipients_mail (CamelFolderViewRow *row)
{
	const gchar **slots;

	g_return_val_if_fail (row != NULL, NULL);

	if (!row->info)
		return NULL;

	slots = view_row_derived_slots (row);
	if (!slots[DERIVED_RECIPIENTS_MAIL]) {
		camel_message_info_property_lock (row->info);
		slots[DERIVED_RECIPIENTS_MAIL] = camel_pstring_add (sanitize_addresses (camel_message_info_get_to (row->info), FALSE), TRUE);
		camel_message_info_property_unlock (row->info);
	}

	return view_row_derived_or_null (slots[DERIVED_RECIPIENTS_MAIL]);
}

/**
 * camel_folder_view_row_get_correspondents:
 * @row: a #CamelFolderViewRow
 *
 * Returns the sender or recipients depending on whether the
 * message was sent by one of the user's own addresses (set via
 * camel_folder_view_add_own_address()).
 *
 * Returns: (transfer none) (nullable): the correspondents, or %NULL
 *
 * Note: See camel_folder_view_get_row() for @row's validity.
 *
 * Since: 3.64
 **/
const gchar *
camel_folder_view_row_get_correspondents (CamelFolderViewRow *row)
{
	const gchar **slots;

	g_return_val_if_fail (row != NULL, NULL);

	if (!row->info)
		return NULL;

	slots = view_row_derived_slots (row);
	if (!slots[DERIVED_CORRESPONDENTS]) {
		gboolean is_sent = FALSE;
		gchar *tmp;

		camel_message_info_property_lock (row->info);

		if (row->view->own_addresses) {
			gchar *from_mail = sanitize_addresses (camel_message_info_get_from (row->info), FALSE);
			if (from_mail && *from_mail)
				is_sent = g_hash_table_contains (row->view->own_addresses, from_mail);
			g_free (from_mail);
		}

		if (is_sent)
			tmp = sanitize_addresses (camel_message_info_get_to (row->info), TRUE);
		else
			tmp = sanitize_addresses (camel_message_info_get_from (row->info), TRUE);

		slots[DERIVED_CORRESPONDENTS] = camel_pstring_add (tmp, TRUE);

		camel_message_info_property_unlock (row->info);
	}

	return view_row_derived_or_null (slots[DERIVED_CORRESPONDENTS]);
}

/**
 * camel_folder_view_row_get_labels:
 * @row: a #CamelFolderViewRow
 *
 * Returns a string of label user-flags.
 *
 * Returns: (transfer none) (nullable): the labels string, or %NULL
 *
 * Note: See camel_folder_view_get_row() for @row's validity.
 *
 * Since: 3.64
 **/
const gchar *
camel_folder_view_row_get_labels (CamelFolderViewRow *row)
{
	const gchar **slots;

	g_return_val_if_fail (row != NULL, NULL);

	if (!row->info)
		return NULL;

	slots = view_row_derived_slots (row);
	if (!slots[DERIVED_LABELS]) {
		gchar *tmp;

		camel_message_info_property_lock (row->info);
		tmp = build_labels_string (row->info);
		slots[DERIVED_LABELS] = tmp ? camel_pstring_add (tmp, TRUE) : camel_pstring_strdup ("");
		camel_message_info_property_unlock (row->info);
	}

	return view_row_derived_or_null (slots[DERIVED_LABELS]);
}

/**
 * camel_folder_view_row_get_followup_flag:
 * @row: a #CamelFolderViewRow
 *
 * Returns the follow-up flag text. The returned pointer is valid only
 * while @row is locked with camel_folder_view_row_lock().
 *
 * Returns: (transfer none) (nullable): the flag text, or %NULL
 *
 * Note: See camel_folder_view_get_row() for @row's validity.
 *
 * Since: 3.64
 **/
const gchar *
camel_folder_view_row_get_followup_flag (CamelFolderViewRow *row)
{
	const gchar *tag;

	g_return_val_if_fail (row != NULL, NULL);

	if (!row->info)
		return NULL;

	camel_message_info_property_lock (row->info);
	tag = camel_message_info_get_user_tag (row->info, "follow-up");
	tag = (tag && *tag) ? tag : NULL;
	camel_message_info_property_unlock (row->info);

	return tag;
}

/**
 * camel_folder_view_row_get_followup_completed:
 * @row: a #CamelFolderViewRow
 *
 * Returns whether the follow-up flag has been completed.
 *
 * Returns: %TRUE if completed
 *
 * Note: See camel_folder_view_get_row() for @row's validity.
 *
 * Since: 3.64
 **/
gboolean
camel_folder_view_row_get_followup_completed (CamelFolderViewRow *row)
{
	const gchar *completed;
	gboolean result;

	g_return_val_if_fail (row != NULL, FALSE);

	if (!row->info)
		return FALSE;

	camel_message_info_property_lock (row->info);
	completed = camel_message_info_get_user_tag (row->info, "completed-on");
	result = completed && *completed;
	camel_message_info_property_unlock (row->info);

	return result;
}

/**
 * camel_folder_view_row_get_followup_due_by:
 * @row: a #CamelFolderViewRow
 *
 * Returns the follow-up due date as a Unix timestamp.
 *
 * Returns: due date, or 0
 *
 * Note: See camel_folder_view_get_row() for @row's validity.
 *
 * Since: 3.64
 **/
gint64
camel_folder_view_row_get_followup_due_by (CamelFolderViewRow *row)
{
	const gchar *due_by_tag;
	gint64 result = 0;

	g_return_val_if_fail (row != NULL, 0);

	if (!row->info)
		return 0;

	camel_message_info_property_lock (row->info);

	due_by_tag = camel_message_info_get_user_tag (row->info, "due-by");
	if (due_by_tag && *due_by_tag) {
		GDateTime *dt = g_date_time_new_from_iso8601 (due_by_tag, NULL);

		if (dt) {
			result = g_date_time_to_unix (dt);
			g_date_time_unref (dt);
		}
	}

	camel_message_info_property_unlock (row->info);

	return result;
}

/**
 * camel_folder_view_row_get_score:
 * @row: a #CamelFolderViewRow
 *
 * Returns the message score.
 *
 * Returns: the score, or 0
 *
 * Note: See camel_folder_view_get_row() for @row's validity.
 *
 * Since: 3.64
 **/
gint
camel_folder_view_row_get_score (CamelFolderViewRow *row)
{
	const gchar *score_tag;
	gint result;

	g_return_val_if_fail (row != NULL, 0);

	if (!row->info)
		return 0;

	camel_message_info_property_lock (row->info);
	score_tag = camel_message_info_get_user_tag (row->info, "score");
	result = (score_tag && *score_tag) ? (gint) g_ascii_strtoll (score_tag, NULL, 10) : 0;
	camel_message_info_property_unlock (row->info);

	return result;
}

/**
 * camel_folder_view_row_get_location:
 * @row: a #CamelFolderViewRow
 *
 * Returns the folder display name. Useful for virtual folders
 * to show where a message lives.
 *
 * Returns: (transfer none) (nullable): the folder name, or %NULL
 *
 * Note: See camel_folder_view_get_row() for @row's validity.
 *
 * Since: 3.64
 **/
const gchar *
camel_folder_view_row_get_location (CamelFolderViewRow *row)
{
	CamelFolder *real_folder;

	g_return_val_if_fail (row != NULL, NULL);

	if (!row->info || !row->view->folder)
		return NULL;

	real_folder = row->view->folder;
	if (CAMEL_IS_VEE_MESSAGE_INFO (row->info)) {
		real_folder = camel_vee_folder_get_location (CAMEL_VEE_FOLDER (row->view->folder), CAMEL_VEE_MESSAGE_INFO (row->info), NULL);

		if (!real_folder)
			return NULL;
	}

	return camel_folder_get_display_name (real_folder);
}

/**
 * camel_folder_view_row_get_user_header:
 * @row: a #CamelFolderViewRow
 * @index: zero-based user header index (0 to %CAMEL_UTILS_MAX_USER_HEADERS - 1)
 *
 * Returns the value of the user-defined header at @index. The
 * header names are set via camel_folder_view_set_user_headers().
 *
 * Returns: (transfer none) (nullable): the header value, or %NULL
 *
 * Note: See camel_folder_view_get_row() for @row's validity.
 *
 * Since: 3.64
 **/
const gchar *
camel_folder_view_row_get_user_header (CamelFolderViewRow *row,
                                       guint index)
{
	const gchar **slots;
	guint slot_idx;

	g_return_val_if_fail (row != NULL, NULL);
	g_return_val_if_fail (index < CAMEL_UTILS_MAX_USER_HEADERS, NULL);

	if (!row->info || !row->view->user_headers[index])
		return NULL;

	slot_idx = DERIVED_USER_HEADER_1 + index;
	slots = view_row_derived_slots (row);
	if (!slots[slot_idx]) {
		CamelNameValueArray *user_hdr_array = camel_message_info_dup_user_headers (row->info);
		const gchar *value = NULL;

		if (user_hdr_array) {
			guint kk;

			for (kk = 0; kk < camel_name_value_array_get_length (user_hdr_array); kk++) {
				const gchar *hdr_name = NULL, *hdr_value = NULL;

				camel_name_value_array_get (user_hdr_array, kk, &hdr_name, &hdr_value);
				if (hdr_name && g_ascii_strcasecmp (hdr_name, row->view->user_headers[index]) == 0) {
					value = hdr_value;
					break;
				}
			}
		}

		slots[slot_idx] = camel_pstring_strdup (value ? value : "");
		camel_name_value_array_free (user_hdr_array);
	}

	return view_row_derived_or_null (slots[slot_idx]);
}

/**
 * camel_folder_view_row_get_color:
 * @row: a #CamelFolderViewRow
 *
 * Returns the user-defined color tag on the message, if any. The
 * returned pointer is valid only while @row is locked with
 * camel_folder_view_row_lock().
 *
 * Returns: (transfer none) (nullable): the color string, or %NULL
 *
 * Note: See camel_folder_view_get_row() for @row's validity.
 *
 * Since: 3.64
 **/
const gchar *
camel_folder_view_row_get_color (CamelFolderViewRow *row)
{
	const gchar *color_tag;

	g_return_val_if_fail (row != NULL, NULL);

	if (!row->info)
		return NULL;

	camel_message_info_property_lock (row->info);
	color_tag = camel_message_info_get_user_tag (row->info, "color");
	color_tag = (color_tag && *color_tag) ? color_tag : NULL;
	camel_message_info_property_unlock (row->info);

	return color_tag;
}

/**
 * camel_folder_view_row_get_ignore_thread:
 * @row: a #CamelFolderViewRow
 *
 * Returns whether the message has the "ignore-thread" user flag set.
 *
 * Returns: %TRUE if the thread is ignored
 *
 * Note: See camel_folder_view_get_row() for @row's validity.
 *
 * Since: 3.64
 **/
gboolean
camel_folder_view_row_get_ignore_thread (CamelFolderViewRow *row)
{
	g_return_val_if_fail (row != NULL, FALSE);

	return row->info ? camel_message_info_get_user_flag (row->info, "ignore-thread") : FALSE;
}

static gint
get_status_value (guint32 flags)
{
	if (!(flags & CAMEL_MESSAGE_SEEN) && (flags & CAMEL_MESSAGE_ANSWERED))
		return 4;
	else if (!(flags & CAMEL_MESSAGE_SEEN) && (flags & CAMEL_MESSAGE_FORWARDED))
		return 5;
	else if (flags & CAMEL_MESSAGE_ANSWERED)
		return 2;
	else if (flags & CAMEL_MESSAGE_FORWARDED)
		return 3;
	else if (flags & CAMEL_MESSAGE_SEEN)
		return 1;
	else
		return 0;
}

static gint
compare_row_by_sort_column (ViewRow *aa,
                            ViewRow *bb,
                            CamelFolderViewColumn column)
{
	gint result;

	camel_folder_view_row_lock (aa);
	camel_folder_view_row_lock (bb);

	switch (column) {
	case CAMEL_FOLDER_VIEW_COLUMN_DATE_SENT:
		result = compare_int64 (camel_message_info_get_date_sent (aa->info),
			camel_message_info_get_date_sent (bb->info));
		break;
	case CAMEL_FOLDER_VIEW_COLUMN_DATE_RECEIVED:
		result = compare_int64 (camel_message_info_get_date_received (aa->info),
			camel_message_info_get_date_received (bb->info));
		break;
	case CAMEL_FOLDER_VIEW_COLUMN_SUBJECT:
	case CAMEL_FOLDER_VIEW_COLUMN_SUBJECT_TRIMMED:
		result = compare_string (view_row_get_subject_norm (aa), view_row_get_subject_norm (bb));
		break;
	case CAMEL_FOLDER_VIEW_COLUMN_FROM:
		result = compare_collate (camel_message_info_get_from (aa->info), camel_message_info_get_from (bb->info));
		break;
	case CAMEL_FOLDER_VIEW_COLUMN_SENDER:
		result = compare_collate (camel_folder_view_row_get_sender (aa), camel_folder_view_row_get_sender (bb));
		break;
	case CAMEL_FOLDER_VIEW_COLUMN_SENDER_MAIL:
		result = compare_collate (camel_folder_view_row_get_sender_mail (aa), camel_folder_view_row_get_sender_mail (bb));
		break;
	case CAMEL_FOLDER_VIEW_COLUMN_CORRESPONDENTS:
		result = compare_collate (camel_folder_view_row_get_correspondents (aa), camel_folder_view_row_get_correspondents (bb));
		break;
	case CAMEL_FOLDER_VIEW_COLUMN_TO:
		result = compare_collate (camel_message_info_get_to (aa->info), camel_message_info_get_to (bb->info));
		break;
	case CAMEL_FOLDER_VIEW_COLUMN_RECIPIENTS:
		result = compare_collate (camel_folder_view_row_get_recipients (aa), camel_folder_view_row_get_recipients (bb));
		break;
	case CAMEL_FOLDER_VIEW_COLUMN_RECIPIENTS_MAIL:
		result = compare_collate (camel_folder_view_row_get_recipients_mail (aa), camel_folder_view_row_get_recipients_mail (bb));
		break;
	case CAMEL_FOLDER_VIEW_COLUMN_CC:
		result = compare_collate (camel_message_info_get_cc (aa->info), camel_message_info_get_cc (bb->info));
		break;
	case CAMEL_FOLDER_VIEW_COLUMN_SIZE:
		result = compare_uint32 (camel_message_info_get_size (aa->info), camel_message_info_get_size (bb->info));
		break;
	case CAMEL_FOLDER_VIEW_COLUMN_STATUS:
		result = compare_uint32 (get_status_value (camel_message_info_get_flags (aa->info)),
			get_status_value (camel_message_info_get_flags (bb->info)));
		break;
	case CAMEL_FOLDER_VIEW_COLUMN_FLAGGED:
		result = compare_uint32 (
			(camel_message_info_get_flags (bb->info) & CAMEL_MESSAGE_FLAGGED) != 0,
			(camel_message_info_get_flags (aa->info) & CAMEL_MESSAGE_FLAGGED) != 0);
		break;
	case CAMEL_FOLDER_VIEW_COLUMN_ATTACHMENT:
		result = compare_uint32 (
			(camel_message_info_get_flags (bb->info) & CAMEL_MESSAGE_ATTACHMENTS) != 0,
			(camel_message_info_get_flags (aa->info) & CAMEL_MESSAGE_ATTACHMENTS) != 0);
		break;
	case CAMEL_FOLDER_VIEW_COLUMN_SCORE:
		result = compare_uint32 (camel_folder_view_row_get_score (aa), camel_folder_view_row_get_score (bb));
		break;
	case CAMEL_FOLDER_VIEW_COLUMN_LABELS:
		result = compare_caseless (camel_folder_view_row_get_labels (aa), camel_folder_view_row_get_labels (bb));
		break;
	case CAMEL_FOLDER_VIEW_COLUMN_FOLLOWUP_FLAG:
		result = compare_collate (camel_folder_view_row_get_followup_flag (aa), camel_folder_view_row_get_followup_flag (bb));
		break;
	case CAMEL_FOLDER_VIEW_COLUMN_FOLLOWUP_DUE_BY:
		result = compare_int64 (camel_folder_view_row_get_followup_due_by (aa), camel_folder_view_row_get_followup_due_by (bb));
		break;
	case CAMEL_FOLDER_VIEW_COLUMN_LOCATION:
		result = compare_collate (camel_folder_view_row_get_location (aa), camel_folder_view_row_get_location (bb));
		break;
	case CAMEL_FOLDER_VIEW_COLUMN_UID:
		result = compare_string (camel_message_info_get_uid (aa->info), camel_message_info_get_uid (bb->info));
		break;
	case CAMEL_FOLDER_VIEW_COLUMN_USER_HEADER_1:
		result = compare_collate (camel_folder_view_row_get_user_header (aa, 0), camel_folder_view_row_get_user_header (bb, 0));
		break;
	case CAMEL_FOLDER_VIEW_COLUMN_USER_HEADER_2:
		result = compare_collate (camel_folder_view_row_get_user_header (aa, 1), camel_folder_view_row_get_user_header (bb, 1));
		break;
	case CAMEL_FOLDER_VIEW_COLUMN_USER_HEADER_3:
		result = compare_collate (camel_folder_view_row_get_user_header (aa, 2), camel_folder_view_row_get_user_header (bb, 2));
		break;
	default:
		result = 0;
		break;
	}

	camel_folder_view_row_unlock (bb);
	camel_folder_view_row_unlock (aa);

	return result;
}

typedef struct _SortData {
	CamelFolderView *self;
	gboolean is_toplevel;
} SortData;

static gint
sort_row_cb (gconstpointer a,
             gconstpointer b,
             gpointer user_data)
{
	SortData *sd = user_data;
	const ViewRow *ra = ((ViewRow **) a)[0];
	const ViewRow *rb = ((ViewRow **) b)[0];

	if (ra->info == NULL)
		ra = ra->child;
	if (rb->info == NULL)
		rb = rb->child;

	if (!ra || !rb)
		return 0;

	if (sd->is_toplevel && sd->self->sort_columns->len > 0) {
		guint ii;
		const ViewRow *orig_a = ((ViewRow **) a)[0];
		const ViewRow *orig_b = ((ViewRow **) b)[0];

		for (ii = 0; ii < sd->self->sort_columns->len; ii++) {
			SortColumn *col = &g_array_index (sd->self->sort_columns, SortColumn, ii);
			gint result;

			if (sd->self->thread_latest &&
			    (col->column == CAMEL_FOLDER_VIEW_COLUMN_DATE_SENT ||
			     col->column == CAMEL_FOLDER_VIEW_COLUMN_DATE_RECEIVED)) {
				gint64 da = orig_a->thread_latest_date;
				gint64 db = orig_b->thread_latest_date;
				result = (da < db) ? -1 : (da > db) ? 1 : 0;
			} else {
				result = compare_row_by_sort_column ((ViewRow *) ra, (ViewRow *) rb, col->column);
			}

			if (result != 0)
				return col->order == CAMEL_SORT_DESCENDING ? -result : result;
		}
		return 0;
	} else {
		const ViewRow *orig_a = ((ViewRow **) a)[0];
		const ViewRow *orig_b = ((ViewRow **) b)[0];
		gint64 da = orig_a->sort_date;
		gint64 db = orig_b->sort_date;

		if (da != db) {
			gint result = da < db ? -1 : 1;
			return sd->self->sort_children_ascending ? result : -result;
		}

		if (orig_a->order == orig_b->order)
			return 0;

		return orig_a->order < orig_b->order ? -1 : 1;
	}
}

static gboolean
sort_view_nodes (CamelFolderView *self,
                 ViewRow **cp,
                 gboolean is_toplevel,
                 GCancellable *cancellable,
                 GError **error)
{
	ViewRow *cc, *head, **carray;
	SortData sd;
	gint size = 0;

	if (g_cancellable_set_error_if_cancelled (cancellable, error))
		return FALSE;

	cc = *cp;
	while (cc) {
		if (cc->child) {
			if (!sort_view_nodes (self, &cc->child, FALSE, cancellable, error))
				return FALSE;
		}
		size++;
		cc = cc->next;
	}

	if (size < 2)
		return TRUE;

	carray = g_new (ViewRow *, size);
	cc = *cp;
	size = 0;
	while (cc) {
		carray[size] = cc;
		cc = cc->next;
		size++;
	}

	sd.self = self;
	sd.is_toplevel = is_toplevel;
	g_qsort_with_data (carray, size, sizeof (ViewRow *), sort_row_cb, &sd);

	size--;
	head = carray[size];
	head->next = NULL;
	size--;
	while (size >= 0) {
		cc = carray[size];
		cc->next = head;
		head = cc;
		size--;
	}
	*cp = head;

	g_free (carray);

	return TRUE;
}
/* Returns a newly allocated group label string; caller must g_free() */
static gchar *
get_date_group_label (gint64 msg_date,
                      gint64 now)
{
	GDateTime *dt_now, *dt_msg;
	gint now_year, now_month, now_day;
	gint msg_year, msg_month;
	gint prev_month, prev_month_year;

	if (msg_date <= 0)
		return g_strdup (_("Older"));

	if (msg_date > now)
		return g_strdup (_("Future"));

	dt_now = g_date_time_new_from_unix_local (now);
	dt_msg = g_date_time_new_from_unix_local (msg_date);

	now_year = g_date_time_get_year (dt_now);
	now_month = g_date_time_get_month (dt_now);
	now_day = g_date_time_get_day_of_month (dt_now);
	msg_year = g_date_time_get_year (dt_msg);
	msg_month = g_date_time_get_month (dt_msg);

	g_date_time_unref (dt_now);
	g_date_time_unref (dt_msg);

	if (msg_year == now_year && msg_month == now_month) {
		gint64 diff = now - msg_date;

		if (diff < 86400)
			return g_strdup (_("Today"));
		if (diff < 172800)
			return g_strdup (_("Yesterday"));
		if (diff < 604800)
			return g_strdup (_("Last 7 Days"));

		return g_strdup (_("This Month"));
	}

	/* "Yesterday" can cross into the previous month (e.g. now is the 1st) */
	if (now - msg_date < 172800)
		return g_strdup (_("Yesterday"));

	/* "Last 7 Days" can cross into the previous month */
	if (now - msg_date < 604800 && now_day <= 7)
		return g_strdup (_("Last 7 Days"));

	if (now_month > 1) {
		prev_month = now_month - 1;
		prev_month_year = now_year;
	} else {
		prev_month = 12;
		prev_month_year = now_year - 1;
	}

	if (msg_year == prev_month_year && msg_month == prev_month)
		return g_strdup (_("Last Month"));

	/* Earlier months in the current year - use localized month name */
	if (msg_year == now_year) {
		const gchar * const month_names[] = {
			NC_("group-label", "January"),
			NC_("group-label", "February"),
			NC_("group-label", "March"),
			NC_("group-label", "April"),
			NC_("group-label", "May"),
			NC_("group-label", "June"),
			NC_("group-label", "July"),
			NC_("group-label", "August"),
			NC_("group-label", "September"),
			NC_("group-label", "October"),
			NC_("group-label", "November"),
			NC_("group-label", "December")
		};

		return g_strdup (g_dpgettext2 (GETTEXT_PACKAGE, "group-label", month_names[msg_month - 1]));
	}

	/* Previous years, up to 5 years back - use year number */
	if (msg_year >= now_year - 5)
		return g_strdup_printf ("%d", msg_year);

	return g_strdup (_("Older"));
}

static gboolean
flatten_node (GPtrArray *visible_rows,
              ViewRow *row,
              guint depth,
              GCancellable *cancellable,
              GError **error)
{
	if (g_cancellable_set_error_if_cancelled (cancellable, error))
		return FALSE;

	while (row) {
		row->visible_index = visible_rows->len;
		g_ptr_array_add (visible_rows, row);

		if (row->child && row->expanded) {
			if (!flatten_node (visible_rows, row->child, depth + 1, cancellable, error))
				return FALSE;
		}

		row = row->next;
	}

	return TRUE;
}

static gboolean
folder_view_flatten_visible_rows (GPtrArray *visible_rows,
                                  ViewRow *root,
                                  GCancellable *cancellable,
                                  GError **error)
{
	if (g_cancellable_set_error_if_cancelled (cancellable, error))
		return FALSE;

	g_ptr_array_set_size (visible_rows, 0);

	return flatten_node (visible_rows, root, 0, cancellable, error);
}

static gboolean
folder_view_rebuild_visible_rows (CamelFolderView *self,
                                  GCancellable *cancellable,
                                  GError **error)
{
	if (!folder_view_flatten_visible_rows (self->visible_rows, self->current_gen->root_children, cancellable, error))
		return FALSE;

	self->visible_dirty = FALSE;

	return TRUE;
}

static void
view_row_clear_derived_cache (ViewRow *row)
{
	if (row->derived) {
		guint ii;

		for (ii = 0; ii < N_DERIVED_SLOTS; ii++) {
			camel_pstring_free (row->derived[ii]);
		}
		g_free (row->derived);
		row->derived = NULL;
	}
}

static void
view_row_clear_fields (ViewRow *row)
{
	g_clear_object (&row->info);
	view_row_clear_derived_cache (row);
	g_clear_pointer (&row->references, g_array_unref);
	g_free (row->root_subject);
	g_free (row->group_label);
	row->root_subject = NULL;
	row->group_label = NULL;
}

static void
folder_view_free_node_fields (ViewRow *row)
{
	while (row) {
		if (row->child)
			folder_view_free_node_fields (row->child);
		view_row_clear_fields (row);
		row = row->next;
	}
}

static CamelFolderViewGeneration *
camel_folder_view_generation_new (GHashTable *id_table,
                                  GHashTable *uid_to_node,
                                  CamelMemChunk *node_chunks,
                                  ViewRow *root_children)
{
	CamelFolderViewGeneration *generation;

	generation = g_new0 (CamelFolderViewGeneration, 1);
	generation->ref_count = 1;
	generation->id_table = id_table;
	generation->uid_to_node = uid_to_node;
	generation->node_chunks = node_chunks;
	generation->root_children = root_children;

	return generation;
}

/**
 * camel_folder_view_ref_current_generation:
 * @self: a #CamelFolderView
 *
 * References the tree generation currently backing @self's rows.
 * As long as the returned #CamelFolderViewGeneration is held, any
 * #CamelFolderViewRow obtained from @self before this call stays
 * valid, even across a rebuild or incremental change that would
 * otherwise replace @self's internal tree. Release with
 * camel_folder_view_generation_unref() when done.
 *
 * Returns: (transfer full): the current #CamelFolderViewGeneration
 *
 * Since: 3.64
 **/
CamelFolderViewGeneration *
camel_folder_view_ref_current_generation (CamelFolderView *self)
{
	g_return_val_if_fail (CAMEL_IS_FOLDER_VIEW (self), NULL);
	g_return_val_if_fail (self->current_gen != NULL, NULL);

	return camel_folder_view_generation_ref (self->current_gen);
}

/**
 * camel_folder_view_generation_ref:
 * @generation: a #CamelFolderViewGeneration
 *
 * Returns: (transfer full): @generation, with its reference count increased
 *
 * Since: 3.64
 **/
CamelFolderViewGeneration *
camel_folder_view_generation_ref (CamelFolderViewGeneration *generation)
{
	g_return_val_if_fail (generation != NULL, NULL);

	g_atomic_int_inc (&generation->ref_count);

	return generation;
}

/**
 * camel_folder_view_generation_unref:
 * @generation: (nullable): a #CamelFolderViewGeneration
 *
 * Decreases the reference count of @generation, freeing its
 * underlying tree once the count reaches zero.
 *
 * Since: 3.64
 **/
void
camel_folder_view_generation_unref (CamelFolderViewGeneration *generation)
{
	if (!generation)
		return;

	if (g_atomic_int_dec_and_test (&generation->ref_count)) {
		folder_view_free_node_fields (generation->root_children);
		g_hash_table_destroy (generation->id_table);
		g_hash_table_destroy (generation->uid_to_node);
		camel_memchunk_destroy (generation->node_chunks);
		g_free (generation);
	}
}

static void
folder_view_clear_root_subjects (ViewRow *row)
{
	while (row) {
		g_free (row->root_subject);
		row->root_subject = NULL;
		if (row->child)
			folder_view_clear_root_subjects (row->child);
		row = row->next;
	}
}

static gboolean
thread_items (CamelFolderView *self,
              ViewBuildCtx *ctx,
              GPtrArray *infos,
              ViewRow **head_out,
              GCancellable *cancellable,
              GError **error)
{
	GHashTable *no_id_table;
	ViewRow *cc, *head;
	gboolean success = TRUE;
	guint ii;

	if (g_cancellable_set_error_if_cancelled (cancellable, error))
		return FALSE;

	no_id_table = g_hash_table_new (NULL, NULL);

	for (ii = 0; ii < infos->len && success; ii++) {
		CamelMessageInfo *info = g_ptr_array_index (infos, ii);
		CamelSummaryMessageID message_id_val;
		const GArray *references;
		guint64 mid;

		camel_message_info_property_lock (info);

		mid = camel_message_info_get_message_id (info);
		references = camel_message_info_get_references (info);

		message_id_val.id.id = mid;

		if (mid) {
			cc = g_hash_table_lookup (ctx->id_table, &message_id_val);
			if (cc && cc->order) {
				cc = camel_memchunk_alloc0 (ctx->node_chunks);
				g_hash_table_insert (no_id_table, info, cc);
			} else if (!cc) {
				cc = camel_memchunk_alloc0 (ctx->node_chunks);
				cc->id_table_key.id.id = mid;
				cc->has_id_table_key = TRUE;
				g_hash_table_insert (ctx->id_table, &cc->id_table_key, cc);
			}
		} else {
			cc = camel_memchunk_alloc0 (ctx->node_chunks);
			g_hash_table_insert (no_id_table, info, cc);
		}

		cc->view = self;
		cc->info = g_object_ref (info);
		cc->order = ii + 1;
		cc->sort_date = camel_message_info_get_date_sent (info);
		if (cc->sort_date <= 0)
			cc->sort_date = camel_message_info_get_date_received (info);
		cc->expanded = self->default_expanded;
		if (references && references->len > 0)
			cc->references = g_array_ref ((GArray *) references);

		g_hash_table_insert (ctx->uid_to_node, (gpointer) camel_message_info_get_uid (info), cc);

		folder_view_thread_node_by_refs (self, ctx, cc, references, TRUE);

		camel_message_info_property_unlock (info);
	}

	head = NULL;
	g_hash_table_foreach (ctx->id_table, hashloop, &head);
	g_hash_table_foreach (no_id_table, hashloop, &head);
	g_hash_table_destroy (no_id_table);

	if (success && self->threading != CAMEL_FOLDER_VIEW_THREADING_NONE) {
		success = prune_empty (ctx, &head, cancellable, error);

		if (success && self->thread_subject)
			success = group_root_set (self, ctx, &head, cancellable, error);
	}

	if (success)
		success = folder_view_prune_phantom_roots (ctx, &head, cancellable, error);

	if (success)
		folder_view_clear_root_subjects (head);

	*head_out = head;

	return success;
}

static gboolean
folder_view_insert_group_rows (CamelFolderView *self,
                               ViewBuildCtx *ctx,
                               ViewRow **head,
                               GCancellable *cancellable,
                               GError **error)
{
	ViewRow *cc, *prev, *group_row, *scan, *last_in_group;
	const gchar *last_label = NULL;
	gint64 now;

	if (self->group_by == CAMEL_FOLDER_VIEW_GROUP_BY_NONE)
		return TRUE;

	if (g_cancellable_set_error_if_cancelled (cancellable, error))
		return FALSE;

	now = g_get_real_time () / G_USEC_PER_SEC;
	prev = NULL;
	cc = *head;

	while (cc) {
		gchar *label;
		gint64 date = 0;

		date = cc->sort_date;

		label = get_date_group_label (date, now);

		if (last_label == NULL || g_strcmp0 (last_label, label) != 0) {
			group_row = camel_memchunk_alloc0 (ctx->node_chunks);
			group_row->view = self;
			group_row->is_group = TRUE;
			group_row->expanded = TRUE;
			group_row->group_label = label;

			if (prev) {
				group_row->next = cc;
				prev->next = group_row;
			} else {
				group_row->next = cc;
				*head = group_row;
			}

			group_row->child = cc;
			cc->parent = group_row;

			scan = cc->next;
			last_in_group = cc;

			while (scan && !scan->is_group) {
				gchar *scan_label;
				gint64 scan_date = 0;

				scan_date = scan->sort_date;

				scan_label = get_date_group_label (scan_date, now);

				if (g_strcmp0 (label, scan_label) != 0) {
					g_free (scan_label);
					break;
				}

				g_free (scan_label);
				scan->parent = group_row;
				last_in_group = scan;
				scan = scan->next;
			}

			group_row->next = NULL;
			if (prev)
				prev->next = group_row;

			group_row->next = scan;

			last_in_group->next = NULL;

			last_label = label;
			prev = group_row;
			cc = group_row->next;
		} else {
			g_free (label);
			prev = cc;
			cc = cc->next;
		}
	}

	return TRUE;
}

static gboolean
folder_view_apply_expand_state_recurse (CamelFolderView *self,
                                        ViewRow *row,
                                        gboolean effective_default,
                                        GCancellable *cancellable,
                                        GError **error)
{
	if (g_cancellable_set_error_if_cancelled (cancellable, error))
		return FALSE;

	while (row) {
		if (row->info) {
			const gchar *uid = camel_message_info_get_uid (row->info);

			if (g_hash_table_contains (self->expanded_uids, uid))
				row->expanded = !effective_default;
			else if (row->child)
				row->expanded = effective_default;
		}
		if (row->child) {
			if (!folder_view_apply_expand_state_recurse (self, row->child, effective_default, cancellable, error))
				return FALSE;
		}
		row = row->next;
	}

	return TRUE;
}

static gboolean
folder_view_apply_expand_state_to_root (CamelFolderView *self,
                                        ViewRow *root,
                                        gboolean effective_default,
                                        GCancellable *cancellable,
                                        GError **error)
{
	return folder_view_apply_expand_state_recurse (self, root, effective_default, cancellable, error);
}

static void
folder_view_rebuild_expand_hash_recurse (CamelFolderView *self,
                                         ViewRow *row)
{
	while (row) {
		if (row->info && row->child && row->expanded != self->default_expanded)
			g_hash_table_add (self->expanded_uids,
				(gpointer) camel_pstring_strdup (camel_message_info_get_uid (row->info)));
		if (row->child)
			folder_view_rebuild_expand_hash_recurse (self, row->child);
		row = row->next;
	}
}

static gboolean
folder_view_apply_expand_state (CamelFolderView *self,
                                gboolean effective_default,
                                GCancellable *cancellable,
                                GError **error)
{
	if (!folder_view_apply_expand_state_recurse (self, self->current_gen->root_children, effective_default, cancellable, error))
		return FALSE;

	if (effective_default != self->default_expanded) {
		g_hash_table_remove_all (self->expanded_uids);
		folder_view_rebuild_expand_hash_recurse (self, self->current_gen->root_children);
	}

	return TRUE;
}

static gchar *
folder_view_build_effective_filter (CamelFolderView *self)
{
	const gchar *hide_sexp = NULL;
	gchar *base_filter;

	if (!self->show_deleted && !self->show_junk)
		hide_sexp = "(match-all (not (or (system-flag \"deleted\") (system-flag \"junk\"))))";
	else if (!self->show_deleted)
		hide_sexp = "(match-all (not (system-flag \"deleted\")))";
	else if (!self->show_junk)
		hide_sexp = "(match-all (not (system-flag \"junk\")))";

	if (hide_sexp && self->filter_sexp && *self->filter_sexp)
		base_filter = g_strconcat ("(and ", hide_sexp, " ", self->filter_sexp, ")", NULL);
	else if (hide_sexp)
		base_filter = g_strdup (hide_sexp);
	else if (self->filter_sexp && *self->filter_sexp)
		base_filter = g_strdup (self->filter_sexp);
	else
		base_filter = NULL;

	if (base_filter && self->ensure_uid && *self->ensure_uid) {
		GString *combined;

		combined = g_string_new ("(or (uid");
		camel_sexp_encode_string (combined, self->ensure_uid);
		g_string_append (combined, ") ");
		g_string_append (combined, base_filter);
		g_string_append_c (combined, ')');
		g_free (base_filter);

		return g_string_free (combined, FALSE);
	}

	return base_filter;
}

static gboolean
folder_view_message_hidden_by_flags (CamelFolderView *self,
                                     CamelMessageInfo *info)
{
	guint32 flags;

	if (!info)
		return FALSE;

	if (self->ensure_uid && *self->ensure_uid &&
	    g_strcmp0 (camel_message_info_get_uid (info), self->ensure_uid) == 0)
		return FALSE;

	flags = camel_message_info_get_flags (info);

	if (!self->show_deleted && (flags & CAMEL_MESSAGE_DELETED) != 0)
		return TRUE;
	if (!self->show_junk && (flags & CAMEL_MESSAGE_JUNK) != 0)
		return TRUE;

	return FALSE;
}

static gboolean
folder_view_build_tree (CamelFolderView *self,
                        ViewBuildCtx *ctx,
                        ViewRow **root_out,
                        GCancellable *cancellable,
                        GError **error)
{
	CamelFolderSummary *summary;
	GPtrArray *uids = NULL;
	GPtrArray *infos;
	ViewRow *row;
	gchar *effective_filter;
	guint ii;

	if (g_cancellable_set_error_if_cancelled (cancellable, error))
		return FALSE;

	if (!self->folder)
		return TRUE;

	summary = camel_folder_get_folder_summary (self->folder);

	effective_filter = folder_view_build_effective_filter (self);
	if (effective_filter) {
		gboolean search_ok;

		search_ok = camel_folder_search_sync (self->folder, effective_filter, &uids, cancellable, error);
		g_free (effective_filter);

		if (!search_ok)
			return FALSE;
	}

	if (g_cancellable_set_error_if_cancelled (cancellable, error)) {
		g_clear_pointer (&uids, g_ptr_array_unref);
		return FALSE;
	}

	if (!uids)
		uids = camel_folder_summary_dup_uids (summary);

	if (!uids)
		return TRUE;

	camel_folder_summary_prepare_fetch_all (summary, NULL);

	infos = g_ptr_array_new_with_free_func (g_object_unref);

	for (ii = 0; ii < uids->len; ii++) {
		const gchar *uid = g_ptr_array_index (uids, ii);
		CamelMessageInfo *info;

		info = camel_folder_get_message_info (self->folder, uid);
		if (info)
			g_ptr_array_add (infos, info);
	}

	g_ptr_array_unref (uids);

	if (self->threading != CAMEL_FOLDER_VIEW_THREADING_NONE) {
		if (!thread_items (self, ctx, infos, root_out, cancellable, error)) {
			g_ptr_array_unref (infos);
			return FALSE;
		}
	} else {
		/* Flat list - just create nodes */
		for (ii = 0; ii < infos->len; ii++) {
			CamelMessageInfo *info = g_ptr_array_index (infos, ii);

			row = camel_memchunk_alloc0 (ctx->node_chunks);

			row->view = self;
			row->info = g_object_ref (info);
			row->order = ii + 1;
			row->sort_date = camel_message_info_get_date_sent (info);
			if (row->sort_date <= 0)
				row->sort_date = camel_message_info_get_date_received (info);
			row->expanded = self->default_expanded;

			g_hash_table_insert (ctx->uid_to_node, (gpointer) camel_message_info_get_uid (info), row);

			row->next = *root_out;
			*root_out = row;
		}
	}

	g_ptr_array_unref (infos);

	if (g_cancellable_set_error_if_cancelled (cancellable, error))
		return FALSE;

	if (self->thread_latest) {
		if (!compute_thread_latest_dates (*root_out, cancellable, error))
			return FALSE;
	}

	if (!sort_view_nodes (self, root_out, TRUE, cancellable, error))
		return FALSE;

	if (self->group_by != CAMEL_FOLDER_VIEW_GROUP_BY_NONE) {
		if (!folder_view_insert_group_rows (self, ctx, root_out, cancellable, error))
			return FALSE;
	}

	return TRUE;
}

typedef struct {
	CamelFolderView *self;
	ViewBuildCtx workspace;
	ViewRow *root;
	GMutex done_lock;
	GCond done_cond;
	gboolean done;
} RebuildSwapData;

static void
folder_view_swap_tree (CamelFolderView *self,
                       ViewBuildCtx *workspace,
                       ViewRow *root)
{
	CamelFolderViewGeneration *old_gen = self->current_gen;

	self->current_gen = camel_folder_view_generation_new (workspace->id_table, workspace->uid_to_node, workspace->node_chunks, root);

	camel_folder_view_generation_unref (old_gen);

	folder_view_apply_expand_state (self, self->default_expanded, NULL, NULL);
	folder_view_rebuild_visible_rows (self, NULL, NULL);
}

static void
folder_view_rebuild_swap (CamelFolderView *self,
                          ViewBuildCtx *workspace,
                          ViewRow *root)
{
	folder_view_swap_tree (self, workspace, root);

	self->rebuild_needed = FALSE;
	self->sort_only_changed = FALSE;
	g_signal_emit (self, signals[SIGNAL_ROW_COUNT_CHANGED], 0);
}

static gboolean
folder_view_rebuild_swap_cb (gpointer user_data)
{
	RebuildSwapData *data = user_data;

	if (g_atomic_int_get (&data->self->incremental_pending) > 0)
		return G_SOURCE_CONTINUE;

	folder_view_rebuild_swap (data->self, &data->workspace, data->root);

	g_mutex_lock (&data->done_lock);
	data->done = TRUE;
	g_cond_signal (&data->done_cond);
	g_mutex_unlock (&data->done_lock);

	return G_SOURCE_REMOVE;
}

static void
folder_view_rebuild (CamelFolderView *self,
                     GCancellable *cancellable,
                     GError **error)
{
	ViewBuildCtx workspace;
	ViewRow *root = NULL;
	gboolean success;
	GMainContext *main_ctx;
	RebuildSwapData swap_data;

	workspace.id_table = g_hash_table_new_full (id_hash, id_equal, NULL, NULL);
	workspace.uid_to_node = g_hash_table_new (g_str_hash, g_str_equal);
	workspace.node_chunks = camel_memchunk_new (32, sizeof (ViewRow));

	success = folder_view_build_tree (self, &workspace, &root, cancellable, error);

	if (!success) {
		folder_view_free_node_fields (root);
		camel_memchunk_destroy (workspace.node_chunks);
		g_hash_table_destroy (workspace.uid_to_node);
		g_hash_table_destroy (workspace.id_table);
		return;
	}

	memset (&swap_data, 0, sizeof (swap_data));
	swap_data.self = self;
	swap_data.workspace = workspace;
	swap_data.root = root;
	g_mutex_init (&swap_data.done_lock);
	g_cond_init (&swap_data.done_cond);

	g_idle_add_full (G_PRIORITY_DEFAULT_IDLE + 1, folder_view_rebuild_swap_cb, &swap_data, NULL);

	main_ctx = g_main_context_default ();
	if (g_main_context_acquire (main_ctx)) {
		while (!swap_data.done) {
			g_main_context_iteration (main_ctx, TRUE);
		}
		g_main_context_release (main_ctx);
	} else {
		g_mutex_lock (&swap_data.done_lock);
		while (!swap_data.done) {
			g_cond_wait (&swap_data.done_cond, &swap_data.done_lock);
		}
		g_mutex_unlock (&swap_data.done_lock);
	}

	g_mutex_clear (&swap_data.done_lock);
	g_cond_clear (&swap_data.done_cond);

#ifdef HAVE_MALLOC_TRIM
	malloc_trim (0);
#endif
}

static void
folder_view_folder_changed_cb (CamelFolder *folder,
                               CamelFolderChangeInfo *changes,
                               gpointer user_data)
{
	CamelFolderView *self = CAMEL_FOLDER_VIEW (user_data);

	if (self->freeze_count > 0) {
		self->rebuild_needed = TRUE;
		return;
	}

	g_mutex_lock (&self->pending_changes_lock);
	if (!self->pending_changes)
		self->pending_changes = camel_folder_change_info_new ();
	camel_folder_change_info_cat (self->pending_changes, changes);
	g_mutex_unlock (&self->pending_changes_lock);

	g_signal_emit (self, signals[SIGNAL_FOLDER_CHANGED], 0);
}

static void
folder_view_mark_dirty (CamelFolderView *self)
{
	self->rebuild_needed = TRUE;
	self->sort_only_changed = FALSE;

	if (self->freeze_count == 0)
		g_signal_emit (self, signals[SIGNAL_REBUILD_NEEDED], 0);
}

static void
folder_view_mark_sort_dirty (CamelFolderView *self)
{
	if (!self->rebuild_needed)
		self->sort_only_changed = TRUE;
	self->rebuild_needed = TRUE;

	if (self->freeze_count == 0)
		g_signal_emit (self, signals[SIGNAL_REBUILD_NEEDED], 0);
}
static void
camel_folder_view_set_property (GObject *object,
                                guint property_id,
                                const GValue *value,
                                GParamSpec *pspec)
{
	CamelFolderView *self = CAMEL_FOLDER_VIEW (object);

	switch (property_id) {
	case PROP_FOLDER:
		g_set_object (&self->folder, g_value_get_object (value));
		break;
	case PROP_THREADING:
		camel_folder_view_set_threading (self, g_value_get_enum (value));
		break;
	case PROP_THREAD_SUBJECT:
		camel_folder_view_set_thread_subject (self, g_value_get_boolean (value));
		break;
	case PROP_FILTER:
		camel_folder_view_set_filter (self, g_value_get_string (value));
		break;
	case PROP_SHOW_DELETED:
		camel_folder_view_set_show_deleted (self, g_value_get_boolean (value));
		break;
	case PROP_SHOW_JUNK:
		camel_folder_view_set_show_junk (self, g_value_get_boolean (value));
		break;
	case PROP_GROUP_BY:
		camel_folder_view_set_group_by (self, g_value_get_enum (value));
		break;
	case PROP_THREAD_LATEST:
		camel_folder_view_set_thread_latest (self, g_value_get_boolean (value));
		break;
	case PROP_SORT_CHILDREN_ASCENDING:
		camel_folder_view_set_sort_children_ascending (self, g_value_get_boolean (value));
		break;
	case PROP_LOCALIZED_RE:
		camel_folder_view_set_localized_re (self, g_value_get_string (value));
		break;
	case PROP_LOCALIZED_RE_SEPARATORS:
		camel_folder_view_set_localized_re_separators (self, g_value_get_boxed (value));
		break;
	case PROP_DEFAULT_EXPANDED:
		self->default_expanded = g_value_get_boolean (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
	}
}

static void
camel_folder_view_get_property (GObject *object,
                                guint property_id,
                                GValue *value,
                                GParamSpec *pspec)
{
	CamelFolderView *self = CAMEL_FOLDER_VIEW (object);

	switch (property_id) {
	case PROP_FOLDER:
		g_value_set_object (value, self->folder);
		break;
	case PROP_THREADING:
		g_value_set_enum (value, self->threading);
		break;
	case PROP_THREAD_SUBJECT:
		g_value_set_boolean (value, self->thread_subject);
		break;
	case PROP_FILTER:
		g_value_set_string (value, self->filter_sexp);
		break;
	case PROP_SHOW_DELETED:
		g_value_set_boolean (value, self->show_deleted);
		break;
	case PROP_SHOW_JUNK:
		g_value_set_boolean (value, self->show_junk);
		break;
	case PROP_GROUP_BY:
		g_value_set_enum (value, self->group_by);
		break;
	case PROP_THREAD_LATEST:
		g_value_set_boolean (value, self->thread_latest);
		break;
	case PROP_SORT_CHILDREN_ASCENDING:
		g_value_set_boolean (value, self->sort_children_ascending);
		break;
	case PROP_LOCALIZED_RE:
		g_value_set_string (value, self->localized_re);
		break;
	case PROP_LOCALIZED_RE_SEPARATORS:
		g_value_set_boxed (value, self->localized_re_separators);
		break;
	case PROP_DEFAULT_EXPANDED:
		g_value_set_boolean (value, self->default_expanded);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
	}
}

static void
camel_folder_view_constructed (GObject *object)
{
	CamelFolderView *self = CAMEL_FOLDER_VIEW (object);

	G_OBJECT_CLASS (camel_folder_view_parent_class)->constructed (object);

	if (self->folder) {
		self->folder_changed_handler = g_signal_connect (
			self->folder, "changed",
			G_CALLBACK (folder_view_folder_changed_cb), self);
	}
}

static void
camel_folder_view_finalize (GObject *object)
{
	CamelFolderView *self = CAMEL_FOLDER_VIEW (object);
	guint ii;

	if (self->folder && self->folder_changed_handler) {
		g_signal_handler_disconnect (self->folder, self->folder_changed_handler);
		self->folder_changed_handler = 0;
	}

	g_clear_object (&self->folder);
	g_free (self->filter_sexp);
	camel_pstring_free (self->ensure_uid);
	g_free (self->localized_re);
	g_strfreev (self->localized_re_separators);
	g_array_unref (self->sort_columns);
	g_hash_table_destroy (self->expanded_uids);
	g_ptr_array_unref (self->visible_rows);
	camel_folder_view_generation_unref (self->current_gen);
	g_clear_pointer (&self->own_addresses, g_hash_table_destroy);
	for (ii = 0; ii < CAMEL_UTILS_MAX_USER_HEADERS; ii++) {
		g_free (self->user_headers[ii]);
	}
	g_clear_pointer (&self->pending_changes, camel_folder_change_info_free);
	g_mutex_clear (&self->pending_changes_lock);

	G_OBJECT_CLASS (camel_folder_view_parent_class)->finalize (object);
}

static void
camel_folder_view_class_init (CamelFolderViewClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->set_property = camel_folder_view_set_property;
	object_class->get_property = camel_folder_view_get_property;
	object_class->constructed = camel_folder_view_constructed;
	object_class->finalize = camel_folder_view_finalize;

	properties[PROP_FOLDER] = g_param_spec_object (
		"folder", NULL, NULL,
		CAMEL_TYPE_FOLDER,
		G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

	properties[PROP_THREADING] = g_param_spec_enum (
		"threading", NULL, NULL,
		CAMEL_TYPE_FOLDER_VIEW_THREADING,
		CAMEL_FOLDER_VIEW_THREADING_NONE,
		G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

	properties[PROP_THREAD_SUBJECT] = g_param_spec_boolean (
		"thread-subject", NULL, NULL,
		FALSE,
		G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

	properties[PROP_FILTER] = g_param_spec_string (
		"filter", NULL, NULL,
		NULL,
		G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

	properties[PROP_SHOW_DELETED] = g_param_spec_boolean (
		"show-deleted", NULL, NULL,
		TRUE,
		G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

	properties[PROP_SHOW_JUNK] = g_param_spec_boolean (
		"show-junk", NULL, NULL,
		TRUE,
		G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

	properties[PROP_GROUP_BY] = g_param_spec_enum (
		"group-by", NULL, NULL,
		CAMEL_TYPE_FOLDER_VIEW_GROUP_BY,
		CAMEL_FOLDER_VIEW_GROUP_BY_NONE,
		G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

	properties[PROP_THREAD_LATEST] = g_param_spec_boolean (
		"thread-latest", NULL, NULL,
		FALSE,
		G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

	properties[PROP_SORT_CHILDREN_ASCENDING] = g_param_spec_boolean (
		"sort-children-ascending", NULL, NULL,
		TRUE,
		G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

	properties[PROP_LOCALIZED_RE] = g_param_spec_string (
		"localized-re", NULL, NULL,
		NULL,
		G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

	properties[PROP_LOCALIZED_RE_SEPARATORS] = g_param_spec_boxed (
		"localized-re-separators", NULL, NULL,
		G_TYPE_STRV,
		G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

	properties[PROP_DEFAULT_EXPANDED] = g_param_spec_boolean (
		"default-expanded", NULL, NULL,
		TRUE,
		G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

	g_object_class_install_properties (object_class, N_PROPERTIES, properties);

	signals[SIGNAL_ROWS_CHANGED] = g_signal_new (
		"rows-changed", G_TYPE_FROM_CLASS (klass),
		G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
		G_TYPE_NONE, 2, G_TYPE_UINT, G_TYPE_UINT);

	signals[SIGNAL_ROWS_INSERTED] = g_signal_new (
		"rows-inserted", G_TYPE_FROM_CLASS (klass),
		G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
		G_TYPE_NONE, 2, G_TYPE_UINT, G_TYPE_UINT);

	signals[SIGNAL_ROWS_REMOVED] = g_signal_new (
		"rows-removed", G_TYPE_FROM_CLASS (klass),
		G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
		G_TYPE_NONE, 2, G_TYPE_UINT, G_TYPE_UINT);

	signals[SIGNAL_ROW_COUNT_CHANGED] = g_signal_new (
		"row-count-changed", G_TYPE_FROM_CLASS (klass),
		G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
		G_TYPE_NONE, 0);

	signals[SIGNAL_REBUILD_NEEDED] = g_signal_new (
		"rebuild-needed", G_TYPE_FROM_CLASS (klass),
		G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
		G_TYPE_NONE, 0);

	signals[SIGNAL_FOLDER_CHANGED] = g_signal_new (
		"folder-changed", G_TYPE_FROM_CLASS (klass),
		G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
		G_TYPE_NONE, 0);
}

static void
camel_folder_view_init (CamelFolderView *self)
{
	self->sort_columns = g_array_new (FALSE, FALSE, sizeof (SortColumn));
	self->expanded_uids = g_hash_table_new_full (g_str_hash, g_str_equal, (GDestroyNotify) camel_pstring_free, NULL);
	self->current_gen = camel_folder_view_generation_new (
		g_hash_table_new_full (id_hash, id_equal, NULL, NULL),
		g_hash_table_new (g_str_hash, g_str_equal),
		camel_memchunk_new (32, sizeof (ViewRow)),
		NULL);
	self->visible_rows = g_ptr_array_new ();
	self->default_expanded = TRUE;
	self->show_deleted = TRUE;
	self->show_junk = TRUE;
	self->sort_children_ascending = TRUE;
	self->rebuild_needed = TRUE;
	g_mutex_init (&self->pending_changes_lock);
}

/**
 * camel_folder_view_new:
 * @folder: a #CamelFolder to display
 * @default_expanded: whether new thread nodes start expanded
 *
 * Creates a new #CamelFolderView for @folder. Configure properties
 * before connecting signals; property setters emit
 * #CamelFolderView::rebuild-needed, which is harmless while no
 * handler is connected.
 *
 * Returns: (transfer full): a new #CamelFolderView
 *
 * Since: 3.64
 **/
CamelFolderView *
camel_folder_view_new (CamelFolder *folder,
                       gboolean default_expanded)
{
	g_return_val_if_fail (CAMEL_IS_FOLDER (folder), NULL);

	return g_object_new (CAMEL_TYPE_FOLDER_VIEW,
		"folder", folder,
		"default-expanded", default_expanded,
		NULL);
}

/**
 * camel_folder_view_get_folder:
 * @self: a #CamelFolderView
 *
 * Returns the #CamelFolder this view was created for.
 *
 * Returns: (transfer none): a #CamelFolder
 *
 * Since: 3.64
 **/
CamelFolder *
camel_folder_view_get_folder (CamelFolderView *self)
{
	g_return_val_if_fail (CAMEL_IS_FOLDER_VIEW (self), NULL);

	return self->folder;
}

/**
 * camel_folder_view_freeze:
 * @self: a #CamelFolderView
 *
 * Suppresses rebuilds until a matching camel_folder_view_thaw().
 * Calls nest.
 *
 * Since: 3.64
 **/
void
camel_folder_view_freeze (CamelFolderView *self)
{
	g_return_if_fail (CAMEL_IS_FOLDER_VIEW (self));

	self->freeze_count++;
}

/**
 * camel_folder_view_thaw:
 * @self: a #CamelFolderView
 *
 * Decrements the freeze count. When it reaches zero and a rebuild is
 * pending, #CamelFolderView::rebuild-needed is emitted so the caller
 * can schedule the rebuild at an appropriate time.
 *
 * Since: 3.64
 **/
void
camel_folder_view_thaw (CamelFolderView *self)
{
	g_return_if_fail (CAMEL_IS_FOLDER_VIEW (self));
	g_return_if_fail (self->freeze_count > 0);

	self->freeze_count--;

	if (self->freeze_count == 0 && self->rebuild_needed)
		g_signal_emit (self, signals[SIGNAL_REBUILD_NEEDED], 0);
}

typedef struct _PendingApplyData {
	CamelFolderView *self;
	/* uid_changed: UIDs whose cached derived data must be invalidated */
	GPtrArray *changed_uids; /* (element-type utf8) (owned) */
	/* uid_removed: UIDs to unlink from the tree */
	GPtrArray *removed_uids; /* (element-type utf8) (owned) */
	/* uid_added: nodes already created + threaded, to splice in */
	GPtrArray *added_nodes; /* (element-type ViewRow) (owned) */
	gboolean has_adds;
	gboolean has_removes;
	/* signalling back to the worker thread */
	GMutex done_lock;
	GCond done_cond;
	gboolean done;
} PendingApplyData;

typedef struct _ThreadedApplyData {
	CamelFolderView *self;
	ViewBuildCtx workspace;
	ViewRow *new_root;
	GPtrArray *new_visible_rows; /* (element-type ViewRow) (owned) */
	GPtrArray *removed_uids; /* (element-type utf8) (owned) */
	GPtrArray *added_uids; /* (element-type utf8) (owned) */
	GPtrArray *changed_uids; /* (element-type utf8) (owned) */
	GPtrArray *candidate_uids; /* (element-type utf8) (owned) */
	gboolean has_adds;
	gboolean has_removes;
	GMutex done_lock;
	GCond done_cond;
	gboolean done;
} ThreadedApplyData;

typedef struct _ThreadedIncrementalApplyData {
	CamelFolderView *self;
	GPtrArray *changed_uids; /* (element-type utf8) (not owned) */
	GMutex done_lock;
	GCond done_cond;
	gboolean done;
} ThreadedIncrementalApplyData;

typedef struct {
	ViewRow *row;
	guint old_index;
	gboolean was_visible;
} RepositionedRow;

static gboolean
view_row_is_visible_in (GPtrArray *visible_rows,
                        ViewRow *row)
{
	return row->visible_index < visible_rows->len &&
		g_ptr_array_index (visible_rows, row->visible_index) == row;
}

static gboolean
view_row_is_currently_visible (CamelFolderView *self,
                               ViewRow *row)
{
	return view_row_is_visible_in (self->visible_rows, row);
}

typedef struct {
	gchar *label; /* owned */
	guint index;
	gboolean matched;
} GroupHeaderEntry;

static void
group_header_entry_free (gpointer data)
{
	GroupHeaderEntry *entry = data;

	g_free (entry->label);
	g_free (entry);
}

static gint
guint_compare_ascending (gconstpointer aa,
                         gconstpointer bb)
{
	guint va = *(const guint *) aa;
	guint vb = *(const guint *) bb;

	return (va > vb) - (va < vb);
}

static gint
guint_compare_descending (gconstpointer aa,
                          gconstpointer bb)
{
	return guint_compare_ascending (bb, aa);
}

static void
emit_row_range_signals (CamelFolderView *self,
                        GArray *positions,
                        guint signal_id,
                        gboolean descending)
{
	guint ii = 0;

	if (!positions || positions->len == 0)
		return;

	g_array_sort (positions, descending ? guint_compare_descending : guint_compare_ascending);

	while (ii < positions->len) {
		guint anchor = g_array_index (positions, guint, ii);
		guint run_end = anchor;
		guint jj = ii + 1;

		while (jj < positions->len) {
			guint next = g_array_index (positions, guint, jj);
			gboolean matches = descending ?
				(run_end > 0 && next == run_end - 1) :
				(next == run_end + 1);

			if (!matches)
				break;

			run_end = next;
			jj++;
		}

		if (descending)
			g_signal_emit (self, signals[signal_id], 0, run_end, anchor);
		else
			g_signal_emit (self, signals[signal_id], 0, anchor, run_end);

		ii = jj;
	}
}

/* Deliberate aliasing: treats a ViewRow* variable's address (or a
 * ViewRow** parameter) as a ViewRow* to reuse its first field (next)
 * as a fake list-head sentinel; only ->next is ever touched this way. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Warray-bounds"
static void
folder_view_unlink_node (ViewBuildCtx *ctx,
                         ViewRow **root,
                         ViewRow *row)
{
	ViewRow *cc, *parent;

	parent = row->parent;

	if (parent) {
		cc = (ViewRow *) &parent->child;
	} else {
		cc = (ViewRow *) root;
	}

	while (cc->next) {
		if (cc->next == row) {
			cc->next = row->next;
			break;
		}
		cc = cc->next;
	}

	if (row->child) {
		ViewRow *ch = row->child;
		ViewRow *last = ch;
		ViewRow *pp, *xx;

		while (last->next) {
			last = last->next;
		}

		if (parent) {
			pp = (ViewRow *) &parent->child;

			while (pp->next && pp->next != row->next) {
				pp = pp->next;
			}
			last->next = pp->next;
			pp->next = ch;

			for (xx = ch; xx != last->next; xx = xx->next) {
				xx->parent = parent;
			}
		} else {
			last->next = row->next;
			pp = (ViewRow *) root;

			while (pp->next && pp->next != row->next) {
				pp = pp->next;
			}
			pp->next = ch;

			for (xx = ch; xx != last->next; xx = xx->next) {
				xx->parent = NULL;
			}
		}
	}

	row->next = NULL;
	row->parent = NULL;
	row->child = NULL;

	if (row->info)
		g_hash_table_remove (ctx->uid_to_node, camel_message_info_get_uid (row->info));

	view_row_clear_fields (row);
	camel_memchunk_free (ctx->node_chunks, row);
}
#pragma GCC diagnostic pop

static void
folder_view_thread_remove_node (CamelFolderView *self,
                                ViewBuildCtx *ctx,
                                ViewRow **root,
                                ViewRow *row,
                                GPtrArray **out_orphaned,
                                GPtrArray **out_promoted)
{
	if (row->parent && row->parent->is_group) {
		ViewRow *group = row->parent;

		folder_view_remove_node_from_id_table (ctx, row);
		folder_view_unlink_node (ctx, root, row);
		if (group->child == NULL)
			folder_view_unlink_node (ctx, root, group);
	} else {
		if (row->child && self->threading != CAMEL_FOLDER_VIEW_THREADING_NONE) {
			ViewRow *ch;

			if (!row->parent) {
				if (!*out_orphaned)
					*out_orphaned = g_ptr_array_new ();
				for (ch = row->child; ch; ch = ch->next) {
					g_ptr_array_add (*out_orphaned, ch);
				}
			} else {
				if (!*out_promoted)
					*out_promoted = g_ptr_array_new ();
				for (ch = row->child; ch; ch = ch->next) {
					g_ptr_array_add (*out_promoted, ch);
				}
			}
		}
		folder_view_remove_node_from_id_table (ctx, row);
		folder_view_unlink_node (ctx, root, row);
	}
}

static gboolean
folder_view_apply_pending_in_main_thread (gpointer user_data)
{
	PendingApplyData *data = user_data;
	CamelFolderView *self = data->self;
	ViewBuildCtx ctx = { self->current_gen->id_table, self->current_gen->uid_to_node, self->current_gen->node_chunks };
	GPtrArray *orphaned = NULL;
	GPtrArray *promoted = NULL;
	GArray *removed_positions;
	GArray *inserted_positions;
	GArray *changed_positions;
	GPtrArray *old_group_headers = NULL;
	guint ii;

	/* Only ever reached for THREADING_NONE batches - threaded batches go
	 * through folder_view_apply_threaded_changes_in_main_thread() instead. */

	removed_positions = g_array_new (FALSE, FALSE, sizeof (guint));
	inserted_positions = g_array_new (FALSE, FALSE, sizeof (guint));
	changed_positions = g_array_new (FALSE, FALSE, sizeof (guint));

	/* 1. Removes */
	for (ii = 0; ii < data->removed_uids->len; ii++) {
		const gchar *uid = g_ptr_array_index (data->removed_uids, ii);
		ViewRow *row = g_hash_table_lookup (ctx.uid_to_node, uid);

		if (!row)
			continue;

		if (view_row_is_currently_visible (self, row))
			g_array_append_val (removed_positions, row->visible_index);

		folder_view_thread_remove_node (self, &ctx, &self->current_gen->root_children, row, &orphaned, &promoted);
	}

	/* 2. Changes - invalidate cached derived data, or drop messages
	   newly hidden by the show-deleted/show-junk properties */
	for (ii = 0; ii < data->changed_uids->len; ii++) {
		const gchar *uid = g_ptr_array_index (data->changed_uids, ii);
		ViewRow *row = g_hash_table_lookup (ctx.uid_to_node, uid);

		if (!row)
			continue;

		if (folder_view_message_hidden_by_flags (self, row->info)) {
			if (view_row_is_currently_visible (self, row))
				g_array_append_val (removed_positions, row->visible_index);

			folder_view_thread_remove_node (self, &ctx, &self->current_gen->root_children, row, &orphaned, &promoted);
			data->has_removes = TRUE;
		} else {
			view_row_clear_derived_cache (row);
		}
	}

	/* 3. Adds - register and splice in nodes */
	if (data->added_nodes && data->added_nodes->len > 0) {
		for (ii = 0; ii < data->added_nodes->len; ii++) {
			ViewRow *row = g_ptr_array_index (data->added_nodes, ii);

			if (row->info)
				g_hash_table_insert (ctx.uid_to_node, (gpointer) camel_message_info_get_uid (row->info), row);
		}

		for (ii = 0; ii < data->added_nodes->len; ii++) {
			ViewRow *cc = g_ptr_array_index (data->added_nodes, ii);

			if (!cc->parent) {
				cc->next = self->current_gen->root_children;
				self->current_gen->root_children = cc;
			}
		}

		if (self->thread_latest)
			compute_thread_latest_dates (self->current_gen->root_children, NULL, NULL);

		sort_view_nodes (self, &self->current_gen->root_children, TRUE, NULL, NULL);

		if (self->group_by != CAMEL_FOLDER_VIEW_GROUP_BY_NONE) {
			ViewRow *row = self->current_gen->root_children;
			ViewRow *prev = NULL;

			old_group_headers = g_ptr_array_new_with_free_func (group_header_entry_free);

			while (row) {
				ViewRow *next = row->next;

				if (row->is_group) {
					GroupHeaderEntry *entry;
					ViewRow *ch = row->child;
					ViewRow *last = ch;
					ViewRow *xx;

					entry = g_new0 (GroupHeaderEntry, 1);
					entry->label = g_strdup (row->group_label);
					entry->index = row->visible_index;
					g_ptr_array_add (old_group_headers, entry);

					if (ch) {
						while (last->next) {
							last = last->next;
						}
						last->next = next;
					}

					if (prev)
						prev->next = ch ? ch : next;
					else
						self->current_gen->root_children = ch ? ch : next;

					for (xx = ch; xx && xx != next; xx = xx->next) {
						xx->parent = NULL;
					}

					view_row_clear_fields (row);
					camel_memchunk_free (ctx.node_chunks, row);
					row = prev ? prev : (ViewRow *) &self->current_gen->root_children;
				} else {
					prev = row;
				}
				row = next;
			}
			folder_view_insert_group_rows (self, &ctx, &self->current_gen->root_children, NULL, NULL);
		}

		folder_view_apply_expand_state (self, self->default_expanded, NULL, NULL);
	}

	/* Rebuild visible rows and emit signals */
	folder_view_rebuild_visible_rows (self, NULL, NULL);

	for (ii = 0; ii < data->changed_uids->len; ii++) {
		const gchar *uid = g_ptr_array_index (data->changed_uids, ii);
		ViewRow *row = g_hash_table_lookup (ctx.uid_to_node, uid);

		if (row && view_row_is_currently_visible (self, row))
			g_array_append_val (changed_positions, row->visible_index);
	}

	if (data->has_removes || data->has_adds) {
		for (ii = 0; ii < data->added_nodes->len; ii++) {
			ViewRow *row = g_ptr_array_index (data->added_nodes, ii);

			if (view_row_is_currently_visible (self, row))
				g_array_append_val (inserted_positions, row->visible_index);
		}

		if (old_group_headers) {
			GPtrArray *new_group_headers;
			guint jj;

			new_group_headers = g_ptr_array_new_with_free_func (group_header_entry_free);

			for (ii = 0; ii < self->visible_rows->len; ii++) {
				ViewRow *vr = g_ptr_array_index (self->visible_rows, ii);

				if (vr->is_group) {
					GroupHeaderEntry *entry = g_new0 (GroupHeaderEntry, 1);

					entry->label = g_strdup (vr->group_label);
					entry->index = vr->visible_index;
					g_ptr_array_add (new_group_headers, entry);
				}
			}

			for (ii = 0; ii < old_group_headers->len; ii++) {
				GroupHeaderEntry *old_entry = g_ptr_array_index (old_group_headers, ii);
				GroupHeaderEntry *match = NULL;

				for (jj = 0; jj < new_group_headers->len; jj++) {
					GroupHeaderEntry *candidate = g_ptr_array_index (new_group_headers, jj);

					if (!candidate->matched && g_strcmp0 (old_entry->label, candidate->label) == 0) {
						match = candidate;
						break;
					}
				}

				if (match) {
					match->matched = TRUE;

					if (match->index != old_entry->index) {
						g_array_append_val (removed_positions, old_entry->index);
						g_array_append_val (inserted_positions, match->index);
					}
				} else {
					g_array_append_val (removed_positions, old_entry->index);
				}
			}

			for (jj = 0; jj < new_group_headers->len; jj++) {
				GroupHeaderEntry *entry = g_ptr_array_index (new_group_headers, jj);

				if (!entry->matched)
					g_array_append_val (inserted_positions, entry->index);
			}

			g_ptr_array_unref (new_group_headers);
		}

		emit_row_range_signals (self, removed_positions, SIGNAL_ROWS_REMOVED, TRUE);
		emit_row_range_signals (self, inserted_positions, SIGNAL_ROWS_INSERTED, FALSE);
	}

	emit_row_range_signals (self, changed_positions, SIGNAL_ROWS_CHANGED, FALSE);

	if (data->has_removes || data->has_adds)
		g_signal_emit (self, signals[SIGNAL_ROW_COUNT_CHANGED], 0);

	g_clear_pointer (&old_group_headers, g_ptr_array_unref);
	g_array_unref (removed_positions);
	g_array_unref (inserted_positions);
	g_array_unref (changed_positions);

	g_atomic_int_add (&self->incremental_pending, -1);

	g_mutex_lock (&data->done_lock);
	data->done = TRUE;
	g_cond_signal (&data->done_cond);
	g_mutex_unlock (&data->done_lock);

	return G_SOURCE_REMOVE;
}

static GPtrArray *
folder_view_gather_reposition_candidates (GHashTable *old_uid_to_node,
                                          GHashTable *new_uid_to_node,
                                          GPtrArray *removed_uids,
                                          GPtrArray *added_uids,
                                          GPtrArray *changed_uids)
{
	GPtrArray *candidates;
	GHashTable *seen;
	GQueue *queue;
	guint ii;

	candidates = g_ptr_array_new ();
	seen = g_hash_table_new (g_str_hash, g_str_equal);
	queue = g_queue_new ();

	for (ii = 0; ii < removed_uids->len; ii++) {
		g_queue_push_tail (queue, g_ptr_array_index (removed_uids, ii));
	}
	for (ii = 0; ii < added_uids->len; ii++) {
		g_queue_push_tail (queue, g_ptr_array_index (added_uids, ii));
	}
	for (ii = 0; ii < changed_uids->len; ii++) {
		g_queue_push_tail (queue, g_ptr_array_index (changed_uids, ii));
	}

	while (!g_queue_is_empty (queue)) {
		const gchar *uid = g_queue_pop_head (queue);
		ViewRow *old_row, *new_row, *child;

		if (g_hash_table_contains (seen, uid))
			continue;

		g_hash_table_add (seen, (gpointer) uid);
		g_ptr_array_add (candidates, (gpointer) uid);

		old_row = g_hash_table_lookup (old_uid_to_node, uid);
		new_row = g_hash_table_lookup (new_uid_to_node, uid);

		if (old_row) {
			for (child = old_row->child; child; child = child->next) {
				if (child->info)
					g_queue_push_tail (queue, (gpointer) camel_message_info_get_uid (child->info));
			}
		}

		if (new_row) {
			for (child = new_row->child; child; child = child->next) {
				if (child->info)
					g_queue_push_tail (queue, (gpointer) camel_message_info_get_uid (child->info));
			}
		}
	}

	g_queue_free (queue);
	g_hash_table_destroy (seen);

	return candidates;
}

static gboolean
folder_view_apply_threaded_changes_in_main_thread (gpointer user_data)
{
	ThreadedApplyData *data = user_data;
	CamelFolderView *self = data->self;
	GArray *removed_positions;
	GArray *inserted_positions;
	GArray *changed_positions;
	guint ii;

	removed_positions = g_array_new (FALSE, FALSE, sizeof (guint));
	inserted_positions = g_array_new (FALSE, FALSE, sizeof (guint));
	changed_positions = g_array_new (FALSE, FALSE, sizeof (guint));

	for (ii = 0; ii < data->candidate_uids->len; ii++) {
		const gchar *uid = g_ptr_array_index (data->candidate_uids, ii);
		ViewRow *old_row = g_hash_table_lookup (self->current_gen->uid_to_node, uid);
		ViewRow *new_row = g_hash_table_lookup (data->workspace.uid_to_node, uid);
		gboolean was_visible = old_row && view_row_is_visible_in (self->visible_rows, old_row);
		gboolean is_visible = new_row && view_row_is_visible_in (data->new_visible_rows, new_row);

		if (was_visible && !is_visible) {
			g_array_append_val (removed_positions, old_row->visible_index);
		} else if (!was_visible && is_visible) {
			g_array_append_val (inserted_positions, new_row->visible_index);
		} else if (was_visible && is_visible && old_row->visible_index != new_row->visible_index) {
			g_array_append_val (removed_positions, old_row->visible_index);
			g_array_append_val (inserted_positions, new_row->visible_index);
		}
	}

	for (ii = 0; ii < data->changed_uids->len; ii++) {
		const gchar *uid = g_ptr_array_index (data->changed_uids, ii);
		ViewRow *new_row = g_hash_table_lookup (data->workspace.uid_to_node, uid);

		if (new_row && view_row_is_visible_in (data->new_visible_rows, new_row))
			g_array_append_val (changed_positions, new_row->visible_index);
	}

	if (camel_debug ("folder-view")) {
		printf ("[folder-view] threaded apply: candidates=%u removed=%u inserted=%u changed=%u\n",
			data->candidate_uids->len, removed_positions->len, inserted_positions->len, changed_positions->len);
	}

	folder_view_swap_tree (self, &data->workspace, data->new_root);

	emit_row_range_signals (self, removed_positions, SIGNAL_ROWS_REMOVED, TRUE);
	emit_row_range_signals (self, inserted_positions, SIGNAL_ROWS_INSERTED, FALSE);
	emit_row_range_signals (self, changed_positions, SIGNAL_ROWS_CHANGED, FALSE);

	if (data->has_removes || data->has_adds)
		g_signal_emit (self, signals[SIGNAL_ROW_COUNT_CHANGED], 0);

	g_array_unref (removed_positions);
	g_array_unref (inserted_positions);
	g_array_unref (changed_positions);

	g_atomic_int_add (&self->incremental_pending, -1);

	g_mutex_lock (&data->done_lock);
	data->done = TRUE;
	g_cond_signal (&data->done_cond);
	g_mutex_unlock (&data->done_lock);

	return G_SOURCE_REMOVE;
}

static gboolean
folder_view_process_pending_changes_flat (CamelFolderView *self,
                                          CamelFolderViewGeneration *old_generation,
                                          CamelFolderChangeInfo *changes,
                                          GCancellable *cancellable,
                                          GError **error)
{
	PendingApplyData data;
	GHashTable *added_filter_set;
	guint ii;

	memset (&data, 0, sizeof (data));
	data.self = self;
	data.changed_uids = g_ptr_array_new_with_free_func ((GDestroyNotify) camel_pstring_free);
	data.removed_uids = g_ptr_array_new_with_free_func ((GDestroyNotify) camel_pstring_free);
	data.added_nodes = g_ptr_array_new ();
	g_mutex_init (&data.done_lock);
	g_cond_init (&data.done_cond);

	/* Phase 1 - worker thread: gather data */

	/* Collect removed UIDs */
	if (changes->uid_removed) {
		for (ii = 0; ii < changes->uid_removed->len; ii++) {
			g_ptr_array_add (data.removed_uids, (gpointer) camel_pstring_strdup (g_ptr_array_index (changes->uid_removed, ii)));
		}
		data.has_removes = data.removed_uids->len > 0;
	}

	/* Collect changed UIDs */
	if (changes->uid_changed) {
		for (ii = 0; ii < changes->uid_changed->len; ii++) {
			g_ptr_array_add (data.changed_uids, (gpointer) camel_pstring_strdup (g_ptr_array_index (changes->uid_changed, ii)));
		}
	}

	/* Process added UIDs */
	added_filter_set = NULL;
	if (changes->uid_added && changes->uid_added->len > 0) {
		GPtrArray *added_uids = changes->uid_added;
		gchar *effective_filter = folder_view_build_effective_filter (self);

		/* Filter check: if a filter is active, use (and (uid ...) <filter>) */
		if (effective_filter) {
			GString *combined;
			GPtrArray *matched_uids = NULL;

			combined = g_string_new ("(and (uid");
			for (ii = 0; ii < added_uids->len; ii++) {
				g_string_append_c (combined, ' ');
				g_string_append_c (combined, '"');
				g_string_append (combined, (const gchar *) g_ptr_array_index (added_uids, ii));
				g_string_append_c (combined, '"');
			}
			g_string_append (combined, ") ");
			g_string_append (combined, effective_filter);
			g_string_append_c (combined, ')');
			g_free (effective_filter);

			if (!camel_folder_search_sync (self->folder, combined->str, &matched_uids, cancellable, error)) {
				g_string_free (combined, TRUE);
				g_ptr_array_unref (data.changed_uids);
				g_ptr_array_unref (data.removed_uids);
				g_ptr_array_unref (data.added_nodes);
				g_mutex_clear (&data.done_lock);
				g_cond_clear (&data.done_cond);
				/* Put changes back */
				g_mutex_lock (&self->pending_changes_lock);
				if (self->pending_changes)
					camel_folder_change_info_cat (self->pending_changes, changes);
				else
					self->pending_changes = g_steal_pointer (&changes);
				g_mutex_unlock (&self->pending_changes_lock);
				g_clear_pointer (&changes, camel_folder_change_info_free);
				return FALSE;
			}
			g_string_free (combined, TRUE);

			if (matched_uids) {
				added_filter_set = g_hash_table_new (g_str_hash, g_str_equal);
				for (ii = 0; ii < matched_uids->len; ii++) {
					g_hash_table_add (added_filter_set, g_ptr_array_index (matched_uids, ii));
				}
			}

			/* matched_uids elements are owned by the search result; keep alive until we're done */
			/* We'll free matched_uids after building nodes */

			for (ii = 0; ii < added_uids->len; ii++) {
				const gchar *uid = g_ptr_array_index (added_uids, ii);
				CamelMessageInfo *info;
				ViewRow *row;

				if (added_filter_set && !g_hash_table_contains (added_filter_set, uid))
					continue;

				if (g_hash_table_contains (old_generation->uid_to_node, uid))
					continue;

				info = camel_folder_get_message_info (self->folder, uid);
				if (!info)
					continue;

				row = camel_memchunk_alloc0 (old_generation->node_chunks);
				row->view = self;
				row->info = info;
				row->order = g_hash_table_size (old_generation->uid_to_node) + 1;
				row->sort_date = camel_message_info_get_date_sent (info);
				if (row->sort_date <= 0)
					row->sort_date = camel_message_info_get_date_received (info);
				row->expanded = self->default_expanded;

				g_ptr_array_add (data.added_nodes, row);
				data.has_adds = TRUE;
			}

			g_clear_pointer (&added_filter_set, g_hash_table_destroy);
			g_clear_pointer (&matched_uids, g_ptr_array_unref);
		} else {
			/* No filter - add all */
			for (ii = 0; ii < added_uids->len; ii++) {
				const gchar *uid = g_ptr_array_index (added_uids, ii);
				CamelMessageInfo *info;
				ViewRow *row;

				if (g_hash_table_contains (old_generation->uid_to_node, uid))
					continue;

				info = camel_folder_get_message_info (self->folder, uid);
				if (!info)
					continue;

				row = camel_memchunk_alloc0 (old_generation->node_chunks);
				row->view = self;
				row->info = info;
				row->order = g_hash_table_size (old_generation->uid_to_node) + 1;
				row->sort_date = camel_message_info_get_date_sent (info);
				if (row->sort_date <= 0)
					row->sort_date = camel_message_info_get_date_received (info);
				row->expanded = self->default_expanded;

				g_ptr_array_add (data.added_nodes, row);
				data.has_adds = TRUE;
			}
		}
	}

	camel_folder_change_info_free (changes);

	/* Phase 2 - schedule tree mutations on the main thread */
	g_atomic_int_inc (&self->incremental_pending);
	g_idle_add (folder_view_apply_pending_in_main_thread, &data);

	g_mutex_lock (&data.done_lock);
	while (!data.done) {
		g_cond_wait (&data.done_cond, &data.done_lock);
	}
	g_mutex_unlock (&data.done_lock);

	g_ptr_array_unref (data.changed_uids);
	g_ptr_array_unref (data.removed_uids);
	g_ptr_array_unref (data.added_nodes);
	g_mutex_clear (&data.done_lock);
	g_cond_clear (&data.done_cond);

	return TRUE;
}

static gboolean
folder_view_apply_threaded_incremental_in_main_thread (gpointer user_data)
{
	ThreadedIncrementalApplyData *data = user_data;
	CamelFolderView *self = data->self;
	ViewBuildCtx ctx = { self->current_gen->id_table, self->current_gen->uid_to_node, self->current_gen->node_chunks };
	GPtrArray *orphaned = NULL;
	GPtrArray *promoted = NULL;
	GArray *removed_positions;
	GArray *changed_positions;
	gboolean has_removes = FALSE;
	guint ii;

	removed_positions = g_array_new (FALSE, FALSE, sizeof (guint));
	changed_positions = g_array_new (FALSE, FALSE, sizeof (guint));

	for (ii = 0; ii < data->changed_uids->len; ii++) {
		const gchar *uid = g_ptr_array_index (data->changed_uids, ii);
		ViewRow *row = g_hash_table_lookup (ctx.uid_to_node, uid);

		if (!row)
			continue;

		if (folder_view_message_hidden_by_flags (self, row->info)) {
			if (view_row_is_currently_visible (self, row))
				g_array_append_val (removed_positions, row->visible_index);

			folder_view_thread_remove_node (self, &ctx, &self->current_gen->root_children, row, &orphaned, &promoted);
			has_removes = TRUE;
		} else {
			view_row_clear_derived_cache (row);
		}
	}

	if (has_removes) {
		if (self->thread_latest)
			compute_thread_latest_dates (self->current_gen->root_children, NULL, NULL);

		folder_view_rebuild_visible_rows (self, NULL, NULL);
	}

	for (ii = 0; ii < data->changed_uids->len; ii++) {
		const gchar *uid = g_ptr_array_index (data->changed_uids, ii);
		ViewRow *row = g_hash_table_lookup (ctx.uid_to_node, uid);

		if (row && view_row_is_currently_visible (self, row))
			g_array_append_val (changed_positions, row->visible_index);
	}

	emit_row_range_signals (self, removed_positions, SIGNAL_ROWS_REMOVED, TRUE);
	emit_row_range_signals (self, changed_positions, SIGNAL_ROWS_CHANGED, FALSE);

	if (has_removes)
		g_signal_emit (self, signals[SIGNAL_ROW_COUNT_CHANGED], 0);

	g_clear_pointer (&orphaned, g_ptr_array_unref);
	g_clear_pointer (&promoted, g_ptr_array_unref);
	g_array_unref (removed_positions);
	g_array_unref (changed_positions);

	g_atomic_int_add (&self->incremental_pending, -1);

	g_mutex_lock (&data->done_lock);
	data->done = TRUE;
	g_cond_signal (&data->done_cond);
	g_mutex_unlock (&data->done_lock);

	return G_SOURCE_REMOVE;
}

static gboolean
folder_view_process_threaded_incremental (CamelFolderView *self,
                                          GPtrArray *changed_uids,
                                          GCancellable *cancellable,
                                          GError **error)
{
	ThreadedIncrementalApplyData data;

	if (g_cancellable_set_error_if_cancelled (cancellable, error))
		return FALSE;

	memset (&data, 0, sizeof (data));
	data.self = self;
	data.changed_uids = changed_uids;
	g_mutex_init (&data.done_lock);
	g_cond_init (&data.done_cond);

	g_atomic_int_inc (&self->incremental_pending);
	g_idle_add (folder_view_apply_threaded_incremental_in_main_thread, &data);

	g_mutex_lock (&data.done_lock);
	while (!data.done) {
		g_cond_wait (&data.done_cond, &data.done_lock);
	}
	g_mutex_unlock (&data.done_lock);

	g_mutex_clear (&data.done_lock);
	g_cond_clear (&data.done_cond);

	return TRUE;
}

static gboolean
folder_view_threaded_change_needs_rebuild (CamelFolderView *self,
                                           CamelFolderViewGeneration *old_generation,
                                           GPtrArray *changed_uids)
{
	guint ii;

	for (ii = 0; ii < changed_uids->len; ii++) {
		const gchar *uid = g_ptr_array_index (changed_uids, ii);
		ViewRow *row = g_hash_table_lookup (old_generation->uid_to_node, uid);
		gboolean is_flat_child;

		if (!row)
			continue;

		if (!folder_view_message_hidden_by_flags (self, row->info))
			continue;

		is_flat_child = self->threading == CAMEL_FOLDER_VIEW_THREADING_FLAT &&
			row->parent != NULL && !row->parent->is_group;

		if (!is_flat_child)
			return TRUE;
	}

	return FALSE;
}

static gboolean
folder_view_process_pending_changes_threaded (CamelFolderView *self,
                                              CamelFolderViewGeneration *old_generation,
                                              CamelFolderChangeInfo *changes,
                                              GCancellable *cancellable,
                                              GError **error)
{
	ThreadedApplyData data;
	gboolean success;
	guint ii;

	memset (&data, 0, sizeof (data));
	data.self = self;
	data.removed_uids = g_ptr_array_new_with_free_func ((GDestroyNotify) camel_pstring_free);
	data.added_uids = g_ptr_array_new_with_free_func ((GDestroyNotify) camel_pstring_free);
	data.changed_uids = g_ptr_array_new_with_free_func ((GDestroyNotify) camel_pstring_free);
	g_mutex_init (&data.done_lock);
	g_cond_init (&data.done_cond);

	if (changes->uid_removed) {
		for (ii = 0; ii < changes->uid_removed->len; ii++) {
			g_ptr_array_add (data.removed_uids, (gpointer) camel_pstring_strdup (g_ptr_array_index (changes->uid_removed, ii)));
		}
	}

	if (changes->uid_added) {
		for (ii = 0; ii < changes->uid_added->len; ii++) {
			g_ptr_array_add (data.added_uids, (gpointer) camel_pstring_strdup (g_ptr_array_index (changes->uid_added, ii)));
		}
	}

	if (changes->uid_changed) {
		for (ii = 0; ii < changes->uid_changed->len; ii++) {
			g_ptr_array_add (data.changed_uids, (gpointer) camel_pstring_strdup (g_ptr_array_index (changes->uid_changed, ii)));
		}
	}

	data.has_removes = data.removed_uids->len > 0;
	data.has_adds = data.added_uids->len > 0;

	camel_folder_change_info_free (changes);

	if (!data.has_adds && !data.has_removes &&
	    !folder_view_threaded_change_needs_rebuild (self, old_generation, data.changed_uids)) {
		success = folder_view_process_threaded_incremental (self, data.changed_uids, cancellable, error);

		g_ptr_array_unref (data.removed_uids);
		g_ptr_array_unref (data.added_uids);
		g_ptr_array_unref (data.changed_uids);
		g_mutex_clear (&data.done_lock);
		g_cond_clear (&data.done_cond);

		return success;
	}

	data.workspace.id_table = g_hash_table_new_full (id_hash, id_equal, NULL, NULL);
	data.workspace.uid_to_node = g_hash_table_new (g_str_hash, g_str_equal);
	data.workspace.node_chunks = camel_memchunk_new (32, sizeof (ViewRow));

	success = folder_view_build_tree (self, &data.workspace, &data.new_root, cancellable, error);

	if (!success) {
		folder_view_free_node_fields (data.new_root);
		camel_memchunk_destroy (data.workspace.node_chunks);
		g_hash_table_destroy (data.workspace.uid_to_node);
		g_hash_table_destroy (data.workspace.id_table);
		g_ptr_array_unref (data.removed_uids);
		g_ptr_array_unref (data.added_uids);
		g_ptr_array_unref (data.changed_uids);
		g_mutex_clear (&data.done_lock);
		g_cond_clear (&data.done_cond);
		return FALSE;
	}

	folder_view_apply_expand_state_to_root (self, data.new_root, self->default_expanded, cancellable, NULL);

	data.new_visible_rows = g_ptr_array_new ();
	folder_view_flatten_visible_rows (data.new_visible_rows, data.new_root, cancellable, NULL);

	data.candidate_uids = folder_view_gather_reposition_candidates (old_generation->uid_to_node, data.workspace.uid_to_node,
		data.removed_uids, data.added_uids, data.changed_uids);

	if (camel_debug ("folder-view")) {
		printf ("[folder-view] threaded rebuild: removed=%u added=%u changed=%u candidates=%u\n",
			data.removed_uids->len, data.added_uids->len, data.changed_uids->len, data.candidate_uids->len);
	}

	g_atomic_int_inc (&self->incremental_pending);
	g_idle_add (folder_view_apply_threaded_changes_in_main_thread, &data);

	g_mutex_lock (&data.done_lock);
	while (!data.done) {
		g_cond_wait (&data.done_cond, &data.done_lock);
	}
	g_mutex_unlock (&data.done_lock);

	g_ptr_array_unref (data.removed_uids);
	g_ptr_array_unref (data.added_uids);
	g_ptr_array_unref (data.changed_uids);
	g_ptr_array_unref (data.candidate_uids);
	g_ptr_array_unref (data.new_visible_rows);
	g_mutex_clear (&data.done_lock);
	g_cond_clear (&data.done_cond);

	return TRUE;
}

/**
 * camel_folder_view_process_pending_changes_sync:
 * @self: a #CamelFolderView
 * @old_generation: (not nullable): the generation from
 *   camel_folder_view_ref_current_generation(), obtained on the main
 *   thread before dispatching to the worker thread this is called from
 * @cancellable: (nullable): a #GCancellable, or %NULL
 * @error: (nullable): return location for a #GError, or %NULL
 *
 * Processes accumulated folder changes. Call this from a worker thread
 * when the #CamelFolderView::folder-changed signal fires.
 *
 * Returns: %TRUE on success, %FALSE on cancellation or error
 *
 * Since: 3.64
 **/
gboolean
camel_folder_view_process_pending_changes_sync (CamelFolderView *self,
                                                CamelFolderViewGeneration *old_generation,
                                                GCancellable *cancellable,
                                                GError **error)
{
	CamelFolderChangeInfo *changes;

	g_return_val_if_fail (CAMEL_IS_FOLDER_VIEW (self), FALSE);
	g_return_val_if_fail (old_generation != NULL, FALSE);

	if (self->freeze_count > 0)
		return TRUE;

	g_mutex_lock (&self->pending_changes_lock);
	changes = g_steal_pointer (&self->pending_changes);
	g_mutex_unlock (&self->pending_changes_lock);

	if (!changes || !camel_folder_change_info_changed (changes)) {
		g_clear_pointer (&changes, camel_folder_change_info_free);
		return TRUE;
	}

	if (g_cancellable_set_error_if_cancelled (cancellable, error)) {
		g_mutex_lock (&self->pending_changes_lock);
		if (self->pending_changes)
			camel_folder_change_info_cat (self->pending_changes, changes);
		else
			self->pending_changes = g_steal_pointer (&changes);
		g_mutex_unlock (&self->pending_changes_lock);
		g_clear_pointer (&changes, camel_folder_change_info_free);
		return FALSE;
	}

	if (self->threading != CAMEL_FOLDER_VIEW_THREADING_NONE)
		return folder_view_process_pending_changes_threaded (self, old_generation, changes, cancellable, error);

	return folder_view_process_pending_changes_flat (self, old_generation, changes, cancellable, error);
}

/**
 * camel_folder_view_rebuild_sync:
 * @self: a #CamelFolderView
 * @cancellable: (nullable): a #GCancellable, or %NULL
 * @error: (nullable): return location for a #GError, or %NULL
 *
 * Rebuilds the view from the folder data. The @cancellable can be used
 * to abort the rebuild early. On success, the
 * #CamelFolderView::row-count-changed signal is emitted.
 *
 * Returns: %TRUE on success, %FALSE on cancellation or error
 *
 * Since: 3.64
 **/
gboolean
camel_folder_view_rebuild_sync (CamelFolderView *self,
                                GCancellable *cancellable,
                                GError **error)
{
	GError *local_error = NULL;

	g_return_val_if_fail (CAMEL_IS_FOLDER_VIEW (self), FALSE);

	folder_view_rebuild (self, cancellable, &local_error);

	if (local_error) {
		g_propagate_error (error, local_error);
		return FALSE;
	}

	if (g_cancellable_set_error_if_cancelled (cancellable, error))
		return FALSE;

	g_mutex_lock (&self->pending_changes_lock);
	g_clear_pointer (&self->pending_changes, camel_folder_change_info_free);
	g_mutex_unlock (&self->pending_changes_lock);

	return TRUE;
}

/**
 * camel_folder_view_sort:
 * @self: a #CamelFolderView
 *
 * Attempts to re-sort the view in memory without a full rebuild.
 * This succeeds only when the pending change is a sort-only change.
 * Call this from the main thread; it does not emit any signals - the
 * caller is responsible for refreshing the UI.
 *
 * Returns: %TRUE if the view was re-sorted in place, %FALSE if
 *    a full rebuild is needed
 *
 * Since: 3.64
 **/
gboolean
camel_folder_view_sort (CamelFolderView *self)
{
	ViewBuildCtx ctx;
	ViewRow *row, *prev;

	g_return_val_if_fail (CAMEL_IS_FOLDER_VIEW (self), FALSE);

	if (!self->rebuild_needed)
		return TRUE;

	if (!self->sort_only_changed || !self->current_gen->root_children)
		return FALSE;

	ctx.id_table = self->current_gen->id_table;
	ctx.uid_to_node = self->current_gen->uid_to_node;
	ctx.node_chunks = self->current_gen->node_chunks;

	sort_view_nodes (self, &self->current_gen->root_children, TRUE, NULL, NULL);

	if (self->group_by != CAMEL_FOLDER_VIEW_GROUP_BY_NONE) {
		row = self->current_gen->root_children;
		prev = NULL;

		while (row) {
			ViewRow *next = row->next;

			if (row->is_group) {
				ViewRow *ch = row->child;
				ViewRow *last = ch;
				ViewRow *xx;

				if (ch) {
					while (last->next) {
						last = last->next;
					}
					last->next = next;
				}

				if (prev)
					prev->next = ch ? ch : next;
				else
					self->current_gen->root_children = ch ? ch : next;

				for (xx = ch; xx && xx != next; xx = xx->next) {
					xx->parent = NULL;
				}

				view_row_clear_fields (row);
				camel_memchunk_free (ctx.node_chunks, row);
				row = prev ? prev : (ViewRow *) &self->current_gen->root_children;
			} else {
				prev = row;
			}
			row = next;
		}
		folder_view_insert_group_rows (self, &ctx, &self->current_gen->root_children, NULL, NULL);
	}

	folder_view_apply_expand_state (self, self->default_expanded, NULL, NULL);
	folder_view_rebuild_visible_rows (self, NULL, NULL);

	self->rebuild_needed = FALSE;
	self->sort_only_changed = FALSE;

	return TRUE;
}

/**
 * camel_folder_view_is_frozen:
 * @self: a #CamelFolderView
 *
 * Returns: whether the view is currently frozen
 *
 * Since: 3.64
 **/
gboolean
camel_folder_view_is_frozen (CamelFolderView *self)
{
	g_return_val_if_fail (CAMEL_IS_FOLDER_VIEW (self), FALSE);

	return self->freeze_count > 0;
}
/**
 * camel_folder_view_clear_own_addresses:
 * @self: a #CamelFolderView
 *
 * Removes all addresses previously added with
 * camel_folder_view_add_own_address(). Existing cached
 * %CAMEL_FOLDER_VIEW_COLUMN_CORRESPONDENTS values are invalidated.
 *
 * Since: 3.64
 **/
void
camel_folder_view_clear_own_addresses (CamelFolderView *self)
{
	g_return_if_fail (CAMEL_IS_FOLDER_VIEW (self));

	if (!self->own_addresses)
		return;

	g_hash_table_remove_all (self->own_addresses);
	self->data_stamp++;
}

/**
 * camel_folder_view_add_own_address:
 * @self: a #CamelFolderView
 * @email: an email address belonging to the user
 *
 * Adds @email to the set of the user's own addresses. These are used
 * to compute the %CAMEL_FOLDER_VIEW_COLUMN_CORRESPONDENTS column: if
 * the sender matches one of the own addresses, the recipients are
 * shown instead.
 *
 * Since: 3.64
 **/
void
camel_folder_view_add_own_address (CamelFolderView *self,
                                   const gchar *email)
{
	g_return_if_fail (CAMEL_IS_FOLDER_VIEW (self));
	g_return_if_fail (email != NULL);

	if (!self->own_addresses)
		self->own_addresses = g_hash_table_new_full (camel_strcase_hash, camel_strcase_equal, g_free, NULL);

	g_hash_table_add (self->own_addresses, g_strdup (email));
}

/**
 * camel_folder_view_remove_own_address:
 * @self: a #CamelFolderView
 * @email: an email address to remove
 *
 * Removes @email from the set of the user's own addresses.
 *
 * Since: 3.64
 **/
void
camel_folder_view_remove_own_address (CamelFolderView *self,
                                      const gchar *email)
{
	g_return_if_fail (CAMEL_IS_FOLDER_VIEW (self));
	g_return_if_fail (email != NULL);

	if (self->own_addresses)
		g_hash_table_remove (self->own_addresses, email);
}

/**
 * camel_folder_view_set_user_headers:
 * @self: a #CamelFolderView
 * @headers: (nullable) (array zero-terminated=1): a %NULL-terminated
 *   array of header names to extract, or %NULL
 *
 * Sets the list of user-defined headers. Each header maps to a
 * slot: the first to %CAMEL_FOLDER_VIEW_COLUMN_USER_HEADER_1, the
 * second to %CAMEL_FOLDER_VIEW_COLUMN_USER_HEADER_2, and so on (up
 * to %CAMEL_UTILS_MAX_USER_HEADERS). The value is then available via
 * camel_folder_view_row_get_user_header(). Existing cached values
 * are invalidated.
 *
 * Since: 3.64
 **/
void
camel_folder_view_set_user_headers (CamelFolderView *self,
                                    const gchar * const *headers)
{
	guint ii, n_headers;

	g_return_if_fail (CAMEL_IS_FOLDER_VIEW (self));

	n_headers = headers ? g_strv_length ((gchar **) headers) : 0;
	if (n_headers > CAMEL_UTILS_MAX_USER_HEADERS)
		n_headers = CAMEL_UTILS_MAX_USER_HEADERS;

	for (ii = 0; ii < CAMEL_UTILS_MAX_USER_HEADERS; ii++) {
		g_free (self->user_headers[ii]);
		self->user_headers[ii] = (ii < n_headers) ? g_strdup (headers[ii]) : NULL;
	}

	self->data_stamp++;
}

/**
 * camel_folder_view_set_filter:
 * @self: a #CamelFolderView
 * @sexp: (nullable): an S-expression filter, or %NULL for none
 *
 * Sets a search filter. Only messages matching @sexp are shown.
 *
 * Since: 3.64
 **/
void
camel_folder_view_set_filter (CamelFolderView *self,
                              const gchar *sexp)
{
	g_return_if_fail (CAMEL_IS_FOLDER_VIEW (self));

	if (g_strcmp0 (self->filter_sexp, sexp) == 0)
		return;

	g_free (self->filter_sexp);
	self->filter_sexp = g_strdup (sexp);

	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FILTER]);
	folder_view_mark_dirty (self);
}

/**
 * camel_folder_view_get_filter:
 * @self: a #CamelFolderView
 *
 * Returns: (nullable): the current filter S-expression, or %NULL
 *
 * Since: 3.64
 **/
const gchar *
camel_folder_view_get_filter (CamelFolderView *self)
{
	g_return_val_if_fail (CAMEL_IS_FOLDER_VIEW (self), NULL);

	return self->filter_sexp;
}

/**
 * camel_folder_view_set_ensure_uid:
 * @self: a #CamelFolderView
 * @uid: (nullable): a message UID to always keep visible, or %NULL
 *
 * Since: 3.64
 **/
void
camel_folder_view_set_ensure_uid (CamelFolderView *self,
                                  const gchar *uid)
{
	g_return_if_fail (CAMEL_IS_FOLDER_VIEW (self));

	if (g_strcmp0 (self->ensure_uid, uid) == 0)
		return;

	camel_pstring_free (self->ensure_uid);
	self->ensure_uid = uid ? camel_pstring_strdup (uid) : NULL;

	folder_view_mark_dirty (self);
}

/**
 * camel_folder_view_get_ensure_uid:
 * @self: a #CamelFolderView
 *
 * Returns: (nullable): the UID set with camel_folder_view_set_ensure_uid(), or %NULL
 *
 * Since: 3.64
 **/
const gchar *
camel_folder_view_get_ensure_uid (CamelFolderView *self)
{
	g_return_val_if_fail (CAMEL_IS_FOLDER_VIEW (self), NULL);

	return self->ensure_uid;
}

/**
 * camel_folder_view_set_show_deleted:
 * @self: a #CamelFolderView
 * @show_deleted: whether to show deleted messages
 *
 * When %FALSE, messages with the deleted flag set are hidden from
 * the view. Toggling this triggers #CamelFolderView::rebuild-needed.
 *
 * Since: 3.64
 **/
void
camel_folder_view_set_show_deleted (CamelFolderView *self,
                                    gboolean show_deleted)
{
	g_return_if_fail (CAMEL_IS_FOLDER_VIEW (self));

	show_deleted = !!show_deleted;

	if (self->show_deleted == show_deleted)
		return;

	self->show_deleted = show_deleted;

	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SHOW_DELETED]);
	folder_view_mark_dirty (self);
}

/**
 * camel_folder_view_get_show_deleted:
 * @self: a #CamelFolderView
 *
 * Returns: whether deleted messages are shown
 *
 * Since: 3.64
 **/
gboolean
camel_folder_view_get_show_deleted (CamelFolderView *self)
{
	g_return_val_if_fail (CAMEL_IS_FOLDER_VIEW (self), FALSE);

	return self->show_deleted;
}

/**
 * camel_folder_view_set_show_junk:
 * @self: a #CamelFolderView
 * @show_junk: whether to show junk messages
 *
 * When %FALSE, messages with the junk flag set are hidden from
 * the view. Toggling this triggers #CamelFolderView::rebuild-needed.
 *
 * Since: 3.64
 **/
void
camel_folder_view_set_show_junk (CamelFolderView *self,
                                 gboolean show_junk)
{
	g_return_if_fail (CAMEL_IS_FOLDER_VIEW (self));

	show_junk = !!show_junk;

	if (self->show_junk == show_junk)
		return;

	self->show_junk = show_junk;

	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SHOW_JUNK]);
	folder_view_mark_dirty (self);
}

/**
 * camel_folder_view_get_show_junk:
 * @self: a #CamelFolderView
 *
 * Returns: whether junk messages are shown
 *
 * Since: 3.64
 **/
gboolean
camel_folder_view_get_show_junk (CamelFolderView *self)
{
	g_return_val_if_fail (CAMEL_IS_FOLDER_VIEW (self), FALSE);

	return self->show_junk;
}

/**
 * camel_folder_view_set_threading:
 * @self: a #CamelFolderView
 * @mode: a #CamelFolderViewThreading mode
 *
 * Sets the threading mode.
 *
 * Since: 3.64
 **/
void
camel_folder_view_set_threading (CamelFolderView *self,
                                 CamelFolderViewThreading mode)
{
	g_return_if_fail (CAMEL_IS_FOLDER_VIEW (self));

	if (self->threading == mode)
		return;

	self->threading = mode;

	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_THREADING]);
	folder_view_mark_dirty (self);
}

/**
 * camel_folder_view_get_threading:
 * @self: a #CamelFolderView
 *
 * Returns: the current threading mode, as #CamelFolderViewThreading
 *
 * Since: 3.64
 **/
CamelFolderViewThreading
camel_folder_view_get_threading (CamelFolderView *self)
{
	g_return_val_if_fail (CAMEL_IS_FOLDER_VIEW (self), CAMEL_FOLDER_VIEW_THREADING_NONE);

	return self->threading;
}

/**
 * camel_folder_view_set_thread_subject:
 * @self: a #CamelFolderView
 * @enabled: whether to enable subject-based threading
 *
 * When enabled, messages with matching subjects (after stripping
 * Re:/Fwd: prefixes) are grouped under the same thread root,
 * even without In-Reply-To/References headers.
 *
 * Since: 3.64
 **/
void
camel_folder_view_set_thread_subject (CamelFolderView *self,
                                      gboolean enabled)
{
	g_return_if_fail (CAMEL_IS_FOLDER_VIEW (self));

	if (self->thread_subject == enabled)
		return;

	self->thread_subject = enabled;

	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_THREAD_SUBJECT]);
	folder_view_mark_dirty (self);
}

/**
 * camel_folder_view_get_thread_subject:
 * @self: a #CamelFolderView
 *
 * Returns: whether subject-based threading is enabled
 *
 * Since: 3.64
 **/
gboolean
camel_folder_view_get_thread_subject (CamelFolderView *self)
{
	g_return_val_if_fail (CAMEL_IS_FOLDER_VIEW (self), FALSE);

	return self->thread_subject;
}

/**
 * camel_folder_view_set_group_by:
 * @self: a #CamelFolderView
 * @group_by: a #CamelFolderViewGroupBy mode
 *
 * Sets the grouping mode. Group rows are inserted between messages
 * and can be queried with camel_folder_view_is_group_row() and
 * camel_folder_view_get_group_label().
 *
 * Since: 3.64
 **/
void
camel_folder_view_set_group_by (CamelFolderView *self,
                                CamelFolderViewGroupBy group_by)
{
	g_return_if_fail (CAMEL_IS_FOLDER_VIEW (self));

	if (self->group_by == group_by)
		return;

	self->group_by = group_by;

	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_GROUP_BY]);
	folder_view_mark_dirty (self);
}

/**
 * camel_folder_view_get_group_by:
 * @self: a #CamelFolderView
 *
 * Returns: the current grouping mode, as #CamelFolderViewGroupBy
 *
 * Since: 3.64
 **/
CamelFolderViewGroupBy
camel_folder_view_get_group_by (CamelFolderView *self)
{
	g_return_val_if_fail (CAMEL_IS_FOLDER_VIEW (self), CAMEL_FOLDER_VIEW_GROUP_BY_NONE);

	return self->group_by;
}

/**
 * camel_folder_view_set_thread_latest:
 * @self: a #CamelFolderView
 * @thread_latest: whether to sort threads by the newest message
 *
 * When enabled, threads are sorted by the date of their newest
 * message rather than the thread root's date.
 *
 * Since: 3.64
 **/
void
camel_folder_view_set_thread_latest (CamelFolderView *self,
                                     gboolean thread_latest)
{
	g_return_if_fail (CAMEL_IS_FOLDER_VIEW (self));

	if (self->thread_latest == thread_latest)
		return;

	self->thread_latest = thread_latest;

	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_THREAD_LATEST]);
	folder_view_mark_dirty (self);
}

/**
 * camel_folder_view_get_thread_latest:
 * @self: a #CamelFolderView
 *
 * Returns: whether threads are sorted by their newest message
 *
 * Since: 3.64
 **/
gboolean
camel_folder_view_get_thread_latest (CamelFolderView *self)
{
	g_return_val_if_fail (CAMEL_IS_FOLDER_VIEW (self), FALSE);

	return self->thread_latest;
}

/**
 * camel_folder_view_set_sort_children_ascending:
 * @self: a #CamelFolderView
 * @ascending: whether to sort children ascending
 *
 * Controls the sort direction for child messages within threads.
 * The primary sort order (set by camel_folder_view_set_sort())
 * applies to root-level items; children always sort by date
 * in the direction given here.
 *
 * Since: 3.64
 **/
void
camel_folder_view_set_sort_children_ascending (CamelFolderView *self,
                                               gboolean ascending)
{
	g_return_if_fail (CAMEL_IS_FOLDER_VIEW (self));

	if (self->sort_children_ascending == ascending)
		return;

	self->sort_children_ascending = ascending;

	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SORT_CHILDREN_ASCENDING]);
	folder_view_mark_sort_dirty (self);
}

/**
 * camel_folder_view_get_sort_children_ascending:
 * @self: a #CamelFolderView
 *
 * Returns: whether children are sorted ascending
 *
 * Since: 3.64
 **/
gboolean
camel_folder_view_get_sort_children_ascending (CamelFolderView *self)
{
	g_return_val_if_fail (CAMEL_IS_FOLDER_VIEW (self), TRUE);

	return self->sort_children_ascending;
}

/**
 * camel_folder_view_set_localized_re:
 * @self: a #CamelFolderView
 * @prefixes: (nullable): comma-separated localized reply/forward prefixes
 *
 * Sets additional prefixes to strip when comparing subjects
 * for threading (e.g. "SV,VS" for Scandinavian languages).
 *
 * Since: 3.64
 **/
void
camel_folder_view_set_localized_re (CamelFolderView *self,
                                    const gchar *prefixes)
{
	g_return_if_fail (CAMEL_IS_FOLDER_VIEW (self));

	if (g_strcmp0 (self->localized_re, prefixes) == 0)
		return;

	g_free (self->localized_re);
	self->localized_re = g_strdup (prefixes);

	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_LOCALIZED_RE]);
	folder_view_mark_dirty (self);
}

/**
 * camel_folder_view_get_localized_re:
 * @self: a #CamelFolderView
 *
 * Returns: (nullable): the current localized prefixes
 *
 * Since: 3.64
 **/
const gchar *
camel_folder_view_get_localized_re (CamelFolderView *self)
{
	g_return_val_if_fail (CAMEL_IS_FOLDER_VIEW (self), NULL);

	return self->localized_re;
}

/**
 * camel_folder_view_set_localized_re_separators:
 * @self: a #CamelFolderView
 * @separators: (nullable): %NULL-terminated array of separator strings
 *
 * Sets separators between the prefix and the subject, beyond the
 * default ": ". For example, Japanese mail uses full-width colon.
 *
 * Since: 3.64
 **/
void
camel_folder_view_set_localized_re_separators (CamelFolderView *self,
                                               const gchar * const *separators)
{
	g_return_if_fail (CAMEL_IS_FOLDER_VIEW (self));

	g_strfreev (self->localized_re_separators);
	self->localized_re_separators = g_strdupv ((gchar **) separators);

	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_LOCALIZED_RE_SEPARATORS]);
	folder_view_mark_dirty (self);
}

/**
 * camel_folder_view_get_localized_re_separators:
 * @self: a #CamelFolderView
 *
 * Returns: (nullable) (transfer none): the current separators
 *
 * Since: 3.64
 **/
const gchar * const *
camel_folder_view_get_localized_re_separators (CamelFolderView *self)
{
	g_return_val_if_fail (CAMEL_IS_FOLDER_VIEW (self), NULL);

	return (const gchar * const *) self->localized_re_separators;
}

/**
 * camel_folder_view_set_sort:
 * @self: a #CamelFolderView
 * @column: the column to sort by, as #CamelFolderViewColumn
 * @order: ascending or descending
 *
 * Sets a single-column sort, replacing any previous sort columns.
 * Use camel_folder_view_add_sort() for multi-column sorting.
 *
 * Since: 3.64
 **/
void
camel_folder_view_set_sort (CamelFolderView *self,
                            CamelFolderViewColumn column,
                            CamelSortType order)
{
	SortColumn col;

	g_return_if_fail (CAMEL_IS_FOLDER_VIEW (self));

	g_array_set_size (self->sort_columns, 0);

	col.column = column;
	col.order = order;
	g_array_append_val (self->sort_columns, col);

	folder_view_mark_sort_dirty (self);
}

/**
 * camel_folder_view_add_sort:
 * @self: a #CamelFolderView
 * @column: the column to add, as #CamelFolderViewColumn
 * @order: ascending or descending
 *
 * Appends a secondary (or further) sort column. The first column
 * should be set with camel_folder_view_set_sort().
 *
 * Since: 3.64
 **/
void
camel_folder_view_add_sort (CamelFolderView *self,
                            CamelFolderViewColumn column,
                            CamelSortType order)
{
	SortColumn col;

	g_return_if_fail (CAMEL_IS_FOLDER_VIEW (self));

	col.column = column;
	col.order = order;
	g_array_append_val (self->sort_columns, col);

	folder_view_mark_sort_dirty (self);
}

/**
 * camel_folder_view_clear_sort:
 * @self: a #CamelFolderView
 *
 * Removes all sort columns. Messages fall back to UID order.
 *
 * Since: 3.64
 **/
void
camel_folder_view_clear_sort (CamelFolderView *self)
{
	g_return_if_fail (CAMEL_IS_FOLDER_VIEW (self));

	if (self->sort_columns->len == 0)
		return;

	g_array_set_size (self->sort_columns, 0);

	folder_view_mark_sort_dirty (self);
}

/**
 * camel_folder_view_get_sort_count:
 * @self: a #CamelFolderView
 *
 * Returns: the number of active sort columns
 *
 * Since: 3.64
 **/
guint
camel_folder_view_get_sort_count (CamelFolderView *self)
{
	g_return_val_if_fail (CAMEL_IS_FOLDER_VIEW (self), 0);

	return self->sort_columns->len;
}

/**
 * camel_folder_view_get_sort_column:
 * @self: a #CamelFolderView
 * @index: zero-based sort column index
 * @out_column: (out) (optional): return location for #CamelFolderViewColumn
 * @out_order: (out) (optional): return location for #CamelSortType
 *
 * Retrieves the sort column at @index.
 *
 * Returns: %TRUE if @index is valid, %FALSE otherwise
 *
 * Since: 3.64
 **/
gboolean
camel_folder_view_get_sort_column (CamelFolderView *self,
                                   guint index,
                                   CamelFolderViewColumn *out_column,
                                   CamelSortType *out_order)
{
	SortColumn *col;

	g_return_val_if_fail (CAMEL_IS_FOLDER_VIEW (self), FALSE);

	if (index >= self->sort_columns->len)
		return FALSE;

	col = &g_array_index (self->sort_columns, SortColumn, index);

	if (out_column)
		*out_column = col->column;
	if (out_order)
		*out_order = col->order;

	return TRUE;
}

/**
 * camel_folder_view_find_row_by_uid:
 * @self: a #CamelFolderView
 * @uid: message UID
 *
 * Returns the visible row index for the message with @uid,
 * or %G_MAXUINT if not found.
 *
 * Returns: the row index, or %G_MAXUINT
 *
 * Note: Call from the main thread.
 *
 * Since: 3.64
 **/
guint
camel_folder_view_find_row_by_uid (CamelFolderView *self,
                                   const gchar *uid)
{
	ViewRow *row;

	g_return_val_if_fail (CAMEL_IS_FOLDER_VIEW (self), G_MAXUINT);

	if (!uid)
		return G_MAXUINT;

	if (self->visible_dirty)
		folder_view_rebuild_visible_rows (self, NULL, NULL);

	row = g_hash_table_lookup (self->current_gen->uid_to_node, uid);
	if (!row)
		return G_MAXUINT;

	if (row->visible_index < self->visible_rows->len &&
	    g_ptr_array_index (self->visible_rows, row->visible_index) == row)
		return row->visible_index;

	return G_MAXUINT;
}

/**
 * camel_folder_view_get_row_count:
 * @self: a #CamelFolderView
 *
 * Returns the total number of visible rows, including group header
 * rows when grouping is active.
 *
 * Returns: the row count
 *
 * Note: Call from the main thread.
 *
 * Since: 3.64
 **/
guint
camel_folder_view_get_row_count (CamelFolderView *self)
{
	g_return_val_if_fail (CAMEL_IS_FOLDER_VIEW (self), 0);

	if (self->visible_dirty)
		folder_view_rebuild_visible_rows (self, NULL, NULL);

	return self->visible_rows->len;
}

/**
 * camel_folder_view_get_row:
 * @self: a #CamelFolderView
 * @row: row index
 *
 * Returns the #CamelFolderViewRow at @row, or %NULL if @row is
 * out of range or a group header row.
 *
 * Returns: (transfer none) (nullable): a #CamelFolderViewRow, or %NULL.
 *   The pointer is valid until the next rebuild, sort, or change signal.
 *   To keep it (and the row's data) valid beyond that, hold a reference
 *   to its generation via camel_folder_view_ref_current_generation().
 *
 * Note: Call from the main thread.
 *
 * Since: 3.64
 **/
CamelFolderViewRow *
camel_folder_view_get_row (CamelFolderView *self,
                           guint row)
{
	ViewRow *view_row;

	g_return_val_if_fail (CAMEL_IS_FOLDER_VIEW (self), NULL);

	if (self->visible_dirty)
		folder_view_rebuild_visible_rows (self, NULL, NULL);

	if (row >= self->visible_rows->len)
		return NULL;

	view_row = g_ptr_array_index (self->visible_rows, row);

	if (!view_row || view_row->is_group)
		return NULL;

	return view_row;
}

static guint
view_row_depth (ViewRow *row)
{
	guint depth = 0;

	if (!row)
		return 0;

	row = row->parent;
	while (row) {
		if (!row->is_group)
			depth++;
		row = row->parent;
	}

	return depth;
}

static guint
view_row_depth_compressed (ViewRow *row)
{
	guint depth = 0;

	if (!row)
		return 0;

	while (row->parent && !row->parent->is_group) {
		gboolean parent_is_root = !row->parent->parent || row->parent->parent->is_group;

		if (!row->child || row->parent->child != row || row->next || parent_is_root ||
		    (row->parent->parent && !row->parent->parent->is_group &&
		     (row->parent->parent->child != row->parent || row->parent->next)))
			depth++;
		row = row->parent;
	}

	return depth;
}

/**
 * camel_folder_view_get_depth:
 * @self: a #CamelFolderView
 * @uid: a message UID present in the view
 *
 * Returns the threading depth of @uid. Root messages have depth 0.
 * In compressed threading mode, straight-line chains are collapsed.
 *
 * Returns: the depth, or 0 if @uid is not found
 *
 * Note: Call from the main thread.
 *
 * Since: 3.64
 **/
guint
camel_folder_view_get_depth (CamelFolderView *self,
                             const gchar *uid)
{
	ViewRow *row;

	g_return_val_if_fail (CAMEL_IS_FOLDER_VIEW (self), 0);
	g_return_val_if_fail (uid != NULL, 0);

	row = g_hash_table_lookup (self->current_gen->uid_to_node, uid);

	if (self->threading == CAMEL_FOLDER_VIEW_THREADING_FLAT)
		return (row && row->parent) ? 1 : 0;

	if (self->threading == CAMEL_FOLDER_VIEW_THREADING_COMPRESSED)
		return view_row_depth_compressed (row);

	return view_row_depth (row);
}

/**
 * camel_folder_view_is_expandable:
 * @self: a #CamelFolderView
 * @uid: a message UID present in the view
 *
 * Returns: whether @uid has children (i.e. can be expanded)
 *
 * Note: Call from the main thread.
 *
 * Since: 3.64
 **/
gboolean
camel_folder_view_is_expandable (CamelFolderView *self,
                                 const gchar *uid)
{
	ViewRow *row;

	g_return_val_if_fail (CAMEL_IS_FOLDER_VIEW (self), FALSE);
	g_return_val_if_fail (uid != NULL, FALSE);

	row = g_hash_table_lookup (self->current_gen->uid_to_node, uid);

	if (!row || !row->child)
		return FALSE;

	if (self->threading == CAMEL_FOLDER_VIEW_THREADING_FLAT)
		return row->parent == NULL;

	return TRUE;
}

/**
 * camel_folder_view_get_expanded:
 * @self: a #CamelFolderView
 * @uid: a message UID present in the view
 *
 * Returns: whether @uid's children are visible
 *
 * Note: Call from the main thread.
 *
 * Since: 3.64
 **/
gboolean
camel_folder_view_get_expanded (CamelFolderView *self,
                                const gchar *uid)
{
	ViewRow *row;

	g_return_val_if_fail (CAMEL_IS_FOLDER_VIEW (self), FALSE);
	g_return_val_if_fail (uid != NULL, FALSE);

	row = g_hash_table_lookup (self->current_gen->uid_to_node, uid);

	return row ? row->expanded : FALSE;
}

/**
 * camel_folder_view_set_expanded:
 * @self: a #CamelFolderView
 * @uid: a message UID present in the view
 * @expanded: %TRUE to expand, %FALSE to collapse
 *
 * Expands or collapses @uid's children. The state is remembered
 * across rebuilds and can be persisted with
 * camel_folder_view_save_expand_state().
 *
 * Note: Call from the main thread.
 *
 * Since: 3.64
 **/
void
camel_folder_view_set_expanded (CamelFolderView *self,
                                const gchar *uid,
                                gboolean expanded)
{
	ViewRow *row;

	g_return_if_fail (CAMEL_IS_FOLDER_VIEW (self));
	g_return_if_fail (uid != NULL);

	row = g_hash_table_lookup (self->current_gen->uid_to_node, uid);
	if (!row || row->expanded == (guint) expanded)
		return;

	row->expanded = expanded;

	if (expanded != self->default_expanded)
		g_hash_table_add (self->expanded_uids, (gpointer) camel_pstring_strdup (uid));
	else
		g_hash_table_remove (self->expanded_uids, uid);

	self->visible_dirty = TRUE;

	if (self->freeze_count == 0) {
		folder_view_rebuild_visible_rows (self, NULL, NULL);
		g_signal_emit (self, signals[SIGNAL_ROW_COUNT_CHANGED], 0);
	}
}

/**
 * camel_folder_view_is_group_row:
 * @self: a #CamelFolderView
 * @row: row index
 *
 * Returns: whether @row is a group header row
 *
 * Note: Call from the main thread.
 *
 * Since: 3.64
 **/
gboolean
camel_folder_view_is_group_row (CamelFolderView *self,
                                guint row)
{
	ViewRow *view_row;

	g_return_val_if_fail (CAMEL_IS_FOLDER_VIEW (self), FALSE);

	if (self->visible_dirty)
		folder_view_rebuild_visible_rows (self, NULL, NULL);

	if (row >= self->visible_rows->len)
		return FALSE;

	view_row = g_ptr_array_index (self->visible_rows, row);

	return view_row ? view_row->is_group : FALSE;
}

/**
 * camel_folder_view_get_group_label:
 * @self: a #CamelFolderView
 * @row: row index of a group header
 *
 * Returns: (nullable): the localized group label, or %NULL if
 *   @row is not a group row
 *
 * Note: Call from the main thread.
 *
 * Since: 3.64
 **/
const gchar *
camel_folder_view_get_group_label (CamelFolderView *self,
                                   guint row)
{
	ViewRow *view_row;

	g_return_val_if_fail (CAMEL_IS_FOLDER_VIEW (self), NULL);

	if (self->visible_dirty)
		folder_view_rebuild_visible_rows (self, NULL, NULL);

	if (row >= self->visible_rows->len)
		return NULL;

	view_row = g_ptr_array_index (self->visible_rows, row);

	return (view_row && view_row->is_group) ? view_row->group_label : NULL;
}

/**
 * camel_folder_view_get_group_count:
 * @self: a #CamelFolderView
 *
 * Returns: the number of group header rows, or 0 when grouping
 *   is not active
 *
 * Note: Call from the main thread.
 *
 * Since: 3.64
 **/
guint
camel_folder_view_get_group_count (CamelFolderView *self)
{
	ViewRow *row;
	guint count = 0;

	g_return_val_if_fail (CAMEL_IS_FOLDER_VIEW (self), 0);

	for (row = self->current_gen->root_children; row; row = row->next) {
		if (row->is_group)
			count++;
	}

	return count;
}

static guint
folder_view_count_expandable (ViewRow *row)
{
	guint count = 0;

	while (row) {
		if (row->child) {
			count++;
			count += folder_view_count_expandable (row->child);
		}
		row = row->next;
	}

	return count;
}

static void
folder_view_save_non_exceptions (CamelFolderView *self,
                                 GString *state,
                                 ViewRow *row)
{
	while (row) {
		if (row->child) {
			if (row->info) {
				const gchar *uid = camel_message_info_get_uid (row->info);

				if (!g_hash_table_contains (self->expanded_uids, uid)) {
					g_string_append_c (state, '\n');
					g_string_append (state, uid);
				}
			}
			folder_view_save_non_exceptions (self, state, row->child);
		}
		row = row->next;
	}
}

/**
 * camel_folder_view_save_expand_state:
 * @self: a #CamelFolderView
 *
 * Serializes the expand/collapse state of all nodes into a string
 * that can later be restored with camel_folder_view_load_expand_state().
 *
 * Only the minority set (expanded or collapsed, whichever is smaller)
 * is saved, keeping the serialized state compact when most threads
 * share the same state.
 *
 * Returns: (transfer full): serialized state string
 *
 * Since: 3.64
 **/
gchar *
camel_folder_view_save_expand_state (CamelFolderView *self)
{
	GString *state;
	guint n_exceptions, n_expandable, n_normal;

	g_return_val_if_fail (CAMEL_IS_FOLDER_VIEW (self), NULL);

	n_exceptions = g_hash_table_size (self->expanded_uids);

	n_expandable = folder_view_count_expandable (self->current_gen->root_children);

	n_normal = n_expandable > n_exceptions ? n_expandable - n_exceptions : 0;

	if (n_exceptions <= n_normal) {
		GHashTableIter iter;
		gpointer key;

		state = g_string_new (self->default_expanded ? "E" : "C");

		g_hash_table_iter_init (&iter, self->expanded_uids);
		while (g_hash_table_iter_next (&iter, &key, NULL)) {
			g_string_append_c (state, '\n');
			g_string_append (state, (const gchar *) key);
		}
	} else {
		state = g_string_new (self->default_expanded ? "C" : "E");

		folder_view_save_non_exceptions (self, state, self->current_gen->root_children);
	}

	return g_string_free (state, FALSE);
}

/**
 * camel_folder_view_load_expand_state:
 * @self: a #CamelFolderView
 * @state: (nullable): a string from camel_folder_view_save_expand_state()
 *
 * Restores expand/collapse state. Applies immediately to existing
 * nodes and is remembered for future rebuilds.
 *
 * The format uses a header line ("E" or "C") to indicate the default
 * expand state that was active when the state was saved, followed by
 * UIDs that were exceptions to that default. When the saved default
 * differs from the current #CamelFolderView:default-expanded, the
 * exception set is translated so that the original per-node expand
 * states are preserved.
 *
 * Since: 3.64
 **/
void
camel_folder_view_load_expand_state (CamelFolderView *self,
                                     const gchar *state)
{
	g_return_if_fail (CAMEL_IS_FOLDER_VIEW (self));

	g_hash_table_remove_all (self->expanded_uids);

	if (state && *state) {
		gchar **lines = g_strsplit (state, "\n", -1);

		if (g_strcmp0 (lines[0], "E") == 0 || g_strcmp0 (lines[0], "C") == 0) {
			gboolean saved_default = lines[0][0] == 'E';
			guint ii;

			for (ii = 1; lines[ii]; ii++) {
				if (lines[ii][0])
					g_hash_table_add (self->expanded_uids, (gpointer) camel_pstring_strdup (lines[ii]));
			}

			folder_view_apply_expand_state (self, saved_default, NULL, NULL);
		}

		g_strfreev (lines);
	}

	self->visible_dirty = TRUE;

	if (self->freeze_count == 0) {
		folder_view_rebuild_visible_rows (self, NULL, NULL);
		g_signal_emit (self, signals[SIGNAL_ROW_COUNT_CHANGED], 0);
	}
}
