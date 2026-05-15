/*
 * SPDX-FileCopyrightText: (C) 2012 Intel Corporation
 * SPDX-License-Identifier: LGPL-2.0-or-later
 * SPDX-FileContributor: Tristan Van Berkom <tristanvb@openismus.com>
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
