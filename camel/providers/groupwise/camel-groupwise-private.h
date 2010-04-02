/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Authors: Siviah Nallagatla <snallagatla@novell.com>
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301
 * USA
 */

#ifndef CAMEL_GROUPWISE_PRIVATE_H
#define CAMEL_GROUPWISE_PRIVATE_H

/* need a way to configure and save this data, if this header is to
   be installed.  For now, dont install it */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifdef ENABLE_THREADS
#define CAMEL_GROUPWISE_FOLDER_LOCK(f, l) \
	(g_static_mutex_lock(&((CamelGroupwiseFolder *)f)->priv->l))
#define CAMEL_GROUPWISE_FOLDER_UNLOCK(f, l) \
	(g_static_mutex_unlock(&((CamelGroupwiseFolder *)f)->priv->l))
#define CAMEL_GROUPWISE_FOLDER_REC_LOCK(f, l) \
	(g_static_rec_mutex_lock(&((CamelGroupwiseFolder *)f)->priv->l))
#define CAMEL_GROUPWISE_FOLDER_REC_UNLOCK(f, l) \
	(g_static_rec_mutex_unlock(&((CamelGroupwiseFolder *)f)->priv->l))
#else
#define GROUPWISE_FOLDER_LOCK(f, l)
#define GROUPWISE_FOLDER_UNLOCK(f, l)
#define GROUPWISE_FOLDER_REC_LOCK(f, l)
#define GROUPWISE_FOLDER_REC_UNLOCK(f, l)
#endif

#endif /* CAMEL_IMAP_PRIVATE_H */
