/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* Writes hashes that go to/from disk in db form. Hash keys are strings
 *
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
 * Authors: JP Rosevear (jpr@ximian.com)
 */

#ifndef EDS_DISABLE_DEPRECATED

#ifndef E_DBHASH_H
#define E_DBHASH_H

#include <glib.h>

G_BEGIN_DECLS

/**
 * EDbHashStatus:
 * @E_DBHASH_STATUS_SAME:
 *   The checksums match.
 * @E_DBHASH_STATUS_DIFFERENT:
 *   The checksums differ.
 * @E_DBHASH_STATUS_NOT_FOUND:
 *   The given key was not found.
 *
 * Return codes for e_dbhash_compare().
 **/
typedef enum {
	E_DBHASH_STATUS_SAME,
	E_DBHASH_STATUS_DIFFERENT,
	E_DBHASH_STATUS_NOT_FOUND
} EDbHashStatus;

typedef struct _EDbHash EDbHash;
typedef struct _EDbHashPrivate EDbHashPrivate;

/**
 * EDbHash:
 * @priv: private data
 *
 * Contains only private data that should be read and manipulated using the
 * functions below.
 **/
struct _EDbHash {
	EDbHashPrivate *priv;
};

/**
 * EDbHashFunc:
 * @key: a database key
 * @user_data: user data passed to e_dbhash_foreach_key()
 *
 * Callback function used in e_dbhash_foreach_key().
 **/
typedef void (*EDbHashFunc) (const gchar *key, gpointer user_data);

EDbHash *	e_dbhash_new			(const gchar *filename);
void		e_dbhash_add			(EDbHash *edbh,
						 const gchar *key,
						 const gchar *data);
void		e_dbhash_remove			(EDbHash *edbh,
						 const gchar *key);
EDbHashStatus	e_dbhash_compare		(EDbHash *edbh,
						 const gchar *key,
						 const gchar *compare_data);
void		e_dbhash_foreach_key		(EDbHash *edbh,
						 EDbHashFunc func,
						 gpointer user_data);
void		e_dbhash_write			(EDbHash *edbh);
void		e_dbhash_destroy		(EDbHash *edbh);

G_END_DECLS

#endif /* E_DBHASH_H */

#endif /* EDS_DISABLE_DEPRECATED */

