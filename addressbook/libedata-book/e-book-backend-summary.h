/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * pas-backend-summary.h
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * Authors:
 *   Chris Toshok <toshok@ximian.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License, version 2, as published by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

#ifndef __E_BOOK_BACKEND_SUMMARY_H__
#define __E_BOOK_BACKEND_SUMMARY_H__

#include <glib.h>
#include <glib-object.h>
#include <libedata-book/e-data-book-types.h>
#include <libebook/e-contact.h>

G_BEGIN_DECLS

#define E_TYPE_BACKEND_SUMMARY        (e_book_backend_summary_get_type ())
#define E_BOOK_BACKEND_SUMMARY(o)          (G_TYPE_CHECK_INSTANCE_CAST ((o), E_TYPE_BACKEND_SUMMARY, EBookBackendSummary))
#define E_BOOK_BACKEND_SUMMARY_CLASS(k)    (G_TYPE_CHECK_CLASS_CAST((k), E_BOOK_BACKEND_TYPE, EBookBackendSummaryClass))
#define E_IS_BACKEND_SUMMARY(o)       (G_TYPE_CHECK_INSTANCE_TYPE ((o), E_TYPE_BACKEND_SUMMARY))
#define E_IS_BACKEND_SUMMARY_CLASS(k) (G_TYPE_CHECK_CLASS_TYPE ((k), E_TYPE_BACKEND_SUMMARY))
#define E_BOOK_BACKEND_SUMMARY_GET_CLASS(k) (G_TYPE_INSTANCE_GET_CLASS ((obj), E_TYPE_BACKEND_SUMMARY, EBookBackendSummaryClass))

typedef struct _EBookBackendSummaryPrivate EBookBackendSummaryPrivate;

struct _EBookBackendSummary{
	GObject parent_object;
	EBookBackendSummaryPrivate *priv;
};

struct _EBookBackendSummaryClass{
	GObjectClass parent_class;
};

EBookBackendSummary* e_book_backend_summary_new              (const gchar *summary_path,
							 gint flush_timeout_millis);
GType              e_book_backend_summary_get_type         (void);

/* returns FALSE if the load fails for any reason (including that the
   summary is out of date), TRUE if it succeeds */
gboolean           e_book_backend_summary_load             (EBookBackendSummary *summary);
/* returns FALSE if the save fails, TRUE if it succeeds (or isn't required due to no changes) */
gboolean           e_book_backend_summary_save              (EBookBackendSummary *summary);

void               e_book_backend_summary_add_contact       (EBookBackendSummary *summary, EContact *contact);
void               e_book_backend_summary_remove_contact    (EBookBackendSummary *summary, const gchar *id);
gboolean           e_book_backend_summary_check_contact     (EBookBackendSummary *summary, const gchar *id);

void               e_book_backend_summary_touch             (EBookBackendSummary *summary);

/* returns TRUE if the summary's mtime is >= @t. */
gboolean           e_book_backend_summary_is_up_to_date     (EBookBackendSummary *summary, time_t t);

gboolean           e_book_backend_summary_is_summary_query  (EBookBackendSummary *summary, const gchar *query);
GPtrArray*         e_book_backend_summary_search            (EBookBackendSummary *summary, const gchar *query);
gchar *              e_book_backend_summary_get_summary_vcard (EBookBackendSummary *summary, const gchar *id);

G_END_DECLS

#endif /* __E_BOOK_BACKEND_SUMMARY_H__ */
