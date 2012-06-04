/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
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

#if !defined (__CAMEL_H_INSIDE__) && !defined (CAMEL_COMPILATION)
#error "Only <camel/camel.h> can be included directly."
#endif

#ifndef CAMEL_FOLDER_SEARCH_H
#define CAMEL_FOLDER_SEARCH_H

#include <camel/camel-folder.h>
#include <camel/camel-index.h>
#include <camel/camel-sexp.h>

/* Standard GObject macros */
#define CAMEL_TYPE_FOLDER_SEARCH \
	(camel_folder_search_get_type ())
#define CAMEL_FOLDER_SEARCH(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), CAMEL_TYPE_FOLDER_SEARCH, CamelFolderSearch))
#define CAMEL_FOLDER_SEARCH_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), CAMEL_TYPE_FOLDER_SEARCH, CamelFolderSearchClass))
#define CAMEL_IS_FOLDER_SEARCH(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), CAMEL_TYPE_FOLDER_SEARCH))
#define CAMEL_IS_FOLDER_SEARCH_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), CAMEL_TYPE_FOLDER_SEARCH))
#define CAMEL_FOLDER_SEARCH_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), CAMEL_TYPE_FOLDER_SEARCH, CamelFolderSearchClass))

G_BEGIN_DECLS

typedef struct _CamelFolderSearch CamelFolderSearch;
typedef struct _CamelFolderSearchClass CamelFolderSearchClass;
typedef struct _CamelFolderSearchPrivate CamelFolderSearchPrivate;

struct _CamelFolderSearch {
	CamelObject parent;
	CamelFolderSearchPrivate *priv;

	CamelSExp *sexp;		/* s-exp evaluator */
	gchar *last_search;	/* last searched expression */

	/* these are only valid during the search, and are reset afterwards */
	CamelFolder *folder;	/* folder for current search */
	GPtrArray *summary;	/* summary array for current search */
	GPtrArray *summary_set;	/* subset of summary to actually include in search */
	CamelMessageInfo *current; /* current message info, when searching one by one */
	CamelMimeMessage *current_message; /* cache of current message, if required */
	CamelIndex *body_index;
};

struct _CamelFolderSearchClass {
	CamelObjectClass parent_class;

	/* general bool/comparison options, usually these wont need to be set, unless it is compiling into another language */
	CamelSExpResult * (*and_)(CamelSExp *f, gint argc, CamelSExpTerm **argv, CamelFolderSearch *s);
	CamelSExpResult * (*or_)(CamelSExp *f, gint argc, CamelSExpTerm **argv, CamelFolderSearch *s);
	CamelSExpResult * (*not_)(CamelSExp *f, gint argc, CamelSExpResult **argv, CamelFolderSearch *s);
	CamelSExpResult * (*lt)(CamelSExp *f, gint argc, CamelSExpTerm **argv, CamelFolderSearch *s);
	CamelSExpResult * (*gt)(CamelSExp *f, gint argc, CamelSExpTerm **argv, CamelFolderSearch *s);
	CamelSExpResult * (*eq)(CamelSExp *f, gint argc, CamelSExpTerm **argv, CamelFolderSearch *s);

	/* search options */
	/* (match-all [boolean expression]) Apply match to all messages */
	CamelSExpResult * (*match_all)(CamelSExp *f, gint argc, CamelSExpTerm **argv, CamelFolderSearch *s);

	/* (match-threads "type" [array expression]) add all related threads */
	CamelSExpResult * (*match_threads)(CamelSExp *f, gint argc, CamelSExpTerm **argv, CamelFolderSearch *s);

	/* (body-contains "string1" "string2" ...) Returns a list of matches, or true if in single-message mode */
	CamelSExpResult * (*body_contains)(CamelSExp *f, gint argc, CamelSExpResult **argv, CamelFolderSearch *s);

	/* (body-regex "regex") Returns a list of matches, or true if in single-message mode */
	CamelSExpResult * (*body_regex)(CamelSExp *f, gint argc, CamelSExpResult **argv, CamelFolderSearch *s);

	/* (header-contains "headername" "string1" ...) List of matches, or true if in single-message mode */
	CamelSExpResult * (*header_contains)(CamelSExp *f, gint argc, CamelSExpResult **argv, CamelFolderSearch *s);

	/* (header-matches "headername" "string") */
	CamelSExpResult * (*header_matches)(CamelSExp *f, gint argc, CamelSExpResult **argv, CamelFolderSearch *s);

	/* (header-starts-with "headername" "string") */
	CamelSExpResult * (*header_starts_with)(CamelSExp *f, gint argc, CamelSExpResult **argv, CamelFolderSearch *s);

	/* (header-ends-with "headername" "string") */
	CamelSExpResult * (*header_ends_with)(CamelSExp *f, gint argc, CamelSExpResult **argv, CamelFolderSearch *s);

	/* (header-exists "headername") */
	CamelSExpResult * (*header_exists)(CamelSExp *f, gint argc, CamelSExpResult **argv, CamelFolderSearch *s);

	/* (header-soundex "headername" "string") */
	CamelSExpResult * (*header_soundex)(CamelSExp *f, gint argc, CamelSExpResult **argv, CamelFolderSearch *s);

	/* (header-regex "headername" "regex_string") */
	CamelSExpResult * (*header_regex)(CamelSExp *f, gint argc, CamelSExpResult **argv, CamelFolderSearch *s);

	/* (header-full-regex "regex") */
	CamelSExpResult * (*header_full_regex)(CamelSExp *f, gint argc, CamelSExpResult **argv, CamelFolderSearch *s);

	/* (user-flag "flagname" "flagname" ...) If one of user-flag set */
	CamelSExpResult * (*user_flag)(CamelSExp *f, gint argc, CamelSExpResult **argv, CamelFolderSearch *s);

	/* (user-tag "flagname") Returns the value of a user tag.  Can only be used in match-all */
	CamelSExpResult * (*user_tag)(CamelSExp *f, gint argc, CamelSExpResult **argv, CamelFolderSearch *s);

	/* (system-flag "flagname") Returns the value of a system flag.  Can only be used in match-all */
	CamelSExpResult * (*system_flag)(CamelSExp *f, gint argc, CamelSExpResult **argv, CamelFolderSearch *s);

	/* (get-sent-date) Retrieve the date that the message was sent on as a time_t */
	CamelSExpResult * (*get_sent_date)(CamelSExp *f, gint argc, CamelSExpResult **argv, CamelFolderSearch *s);

	/* (get-received-date) Retrieve the date that the message was received on as a time_t */
	CamelSExpResult * (*get_received_date)(CamelSExp *f, gint argc, CamelSExpResult **argv, CamelFolderSearch *s);

	/* (get-current-date) Retrieve 'now' as a time_t */
	CamelSExpResult * (*get_current_date)(CamelSExp *f, gint argc, CamelSExpResult **argv, CamelFolderSearch *s);

	/* (get-relative-months) Retrieve relative seconds from 'now' and specified number of months as a time_t */
	CamelSExpResult * (*get_relative_months)(CamelSExp *f, gint argc, CamelSExpResult **argv, CamelFolderSearch *s);

	/* (get-size) Retrieve message size as an gint (in kilobytes) */
	CamelSExpResult * (*get_size)(CamelSExp *f, gint argc, CamelSExpResult **argv, CamelFolderSearch *s);

	/* (uid "uid" ...) True if the uid is in the list */
	CamelSExpResult * (*uid)(CamelSExp *f, gint argc, CamelSExpResult **argv, CamelFolderSearch *s);

	/* (message-location "folder_string") True if the message is in the folder's full name "folder_string" */
	CamelSExpResult * (*message_location)(CamelSExp *f, gint argc, CamelSExpResult **argv, CamelFolderSearch *s);

};

GType		camel_folder_search_get_type	(void);
CamelFolderSearch      *camel_folder_search_new	(void);
void camel_folder_search_construct (CamelFolderSearch *search);

/* This stuff currently gets cleared when you run a search ... what on earth was i thinking ... */
void camel_folder_search_set_folder (CamelFolderSearch *search, CamelFolder *folder);
void camel_folder_search_set_summary (CamelFolderSearch *search, GPtrArray *summary);
void camel_folder_search_set_body_index (CamelFolderSearch *search, CamelIndex *body_index);

GPtrArray *camel_folder_search_search (CamelFolderSearch *search, const gchar *expr, GPtrArray *uids, GCancellable *cancellable, GError **error);
guint32 camel_folder_search_count (CamelFolderSearch *search, const gchar *expr, GCancellable *cancellable, GError **error);
void camel_folder_search_free_result (CamelFolderSearch *search, GPtrArray *);

time_t camel_folder_search_util_add_months (time_t t, gint months);

G_END_DECLS

#endif /* CAMEL_FOLDER_SEARCH_H */
