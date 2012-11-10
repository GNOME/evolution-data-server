/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/* e-book-backend-file-migrate-bdb.c - Migration of old BDB database to the new sqlite DB.
 *
 * Copyright (C) 2012 Openismus GmbH
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
 * Authors: Tristan Van Berkom <tristanvb@openismus.com>
 *
 * Based on work by Nat Friedman, Chris Toshok and Hans Petter Jansson.
 */

#ifndef E_BOOK_BACKEND_MIGRATE_BDB_H
#define E_BOOK_BACKEND_MIGRATE_BDB_H

G_BEGIN_DECLS

gboolean e_book_backend_file_migrate_bdb (EBookBackendSqliteDB  *sqlitedb,
					  const gchar           *sqlite_folder_id,
					  const gchar           *dirname,
					  const gchar           *filename,
					  GError               **error);

G_END_DECLS

#endif /* E_BOOK_BACKEND_MIGRATE_BDB_H */
