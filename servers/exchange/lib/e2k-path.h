/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifndef __E_PATH__
#define __E_PATH__

#include <glib.h>

/* FIXME: deprecated
   This is used exclusively for the legacy imap cache code.  DO NOT use this in any new code */

typedef gboolean (*EPathFindFoldersCallback) (const gchar *physical_path,
					      const gchar *path,
					      gpointer user_data);

gchar *   e_path_to_physical  (const gchar *prefix, const gchar *vpath);

gboolean e_path_find_folders (const gchar *prefix,
			      EPathFindFoldersCallback callback,
			      gpointer data);

gint      e_path_rmdir        (const gchar *prefix, const gchar *vpath);
#endif /* __E_PATH__ */
