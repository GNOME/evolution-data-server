/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/* e-book-backend-file-migrate-bdb.c - Migration of old BDB database to the new sqlite DB.
 *
 * Copyright (C) 2012 Intel Corporation
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
 * Authors: Tristan Van Berkom <tristanvb@openismus.com>
 *
 * Based on work by Nat Friedman, Chris Toshok and Hans Petter Jansson.
 */

#ifndef E_BOOK_BACKEND_MIGRATE_BDB_H
#define E_BOOK_BACKEND_MIGRATE_BDB_H

G_BEGIN_DECLS

gboolean e_book_backend_file_migrate_bdb (EBookSqlite    *sqlitedb,
					  const gchar    *dirname,
					  const gchar    *filename,
					  GCancellable   *cancellable,
					  GError        **error);

G_END_DECLS

#endif /* E_BOOK_BACKEND_MIGRATE_BDB_H */
