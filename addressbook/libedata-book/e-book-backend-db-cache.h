/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * Authors: Devashish Sharma <sdevashish@novell.com>
 *
 * This library is free software you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef E_BOOK_BACKEND_DB_CACHE_H
#define E_BOOK_BACKEND_DB_CACHE_H

#ifndef EDS_DISABLE_DEPRECATED

#include <libebook-contacts/libebook-contacts.h>

G_BEGIN_DECLS

/* Avoid including <db.h> in a public header file. */
struct __db;

EContact *	e_book_backend_db_cache_get_contact
						(struct __db *db,
						 const gchar *uid);
gchar *		e_book_backend_db_cache_get_filename
						(struct __db *db);
void		e_book_backend_db_cache_set_filename
						(struct __db *db,
						 const gchar *filename);
gboolean	e_book_backend_db_cache_add_contact
						(struct __db *db,
						 EContact *contact);
gboolean	e_book_backend_db_cache_remove_contact
						(struct __db *db,
						 const gchar *uid);
gboolean	e_book_backend_db_cache_check_contact
						(struct __db *db,
						 const gchar *uid);
GList *		e_book_backend_db_cache_get_contacts
						(struct __db *db,
						 const gchar *query);
gboolean	e_book_backend_db_cache_exists	(const gchar *uri);
void		e_book_backend_db_cache_set_populated
						(struct __db *db);
gboolean	e_book_backend_db_cache_is_populated
						(struct __db *db);
GPtrArray *	e_book_backend_db_cache_search	(struct __db *db,
						 const gchar *query);
void		e_book_backend_db_cache_set_time
						(struct __db *db,
						 const gchar *t);
gchar *		e_book_backend_db_cache_get_time
						(struct __db *db);

G_END_DECLS

#endif /* EDS_DISABLE_DEPRECATED */

#endif /* E_BOOK_BACKEND_DB_CACHE_H */

