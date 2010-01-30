/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Writes hashes that go to/from disk in db form. Hash keys are strings
 *
 * Author:
 *   JP Rosevear (jpr@ximian.com)
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 */

#ifndef __E_DBHASH_H__
#define __E_DBHASH_H__

#include <glib.h>

G_BEGIN_DECLS

typedef enum {
	E_DBHASH_STATUS_SAME,
	E_DBHASH_STATUS_DIFFERENT,
	E_DBHASH_STATUS_NOT_FOUND
} EDbHashStatus;

typedef struct _EDbHash EDbHash;
typedef struct _EDbHashPrivate EDbHashPrivate;

struct _EDbHash
{
	EDbHashPrivate *priv;
};

typedef void (*EDbHashFunc) (const gchar *key, gpointer user_data);

EDbHash *e_dbhash_new (const gchar *filename);

void e_dbhash_add (EDbHash *edbh, const gchar *key, const gchar *data);
void e_dbhash_remove (EDbHash *edbh, const gchar *key);

EDbHashStatus e_dbhash_compare (EDbHash *edbh, const gchar *key, const gchar *compare_data);
void e_dbhash_foreach_key (EDbHash *edbh, EDbHashFunc func, gpointer user_data);

void e_dbhash_write (EDbHash *edbh);

void e_dbhash_destroy (EDbHash *edbh);

G_END_DECLS

#endif /* __E_DBHASH_H__ */

