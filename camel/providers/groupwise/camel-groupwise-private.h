/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *  camel-imap-private.h: Private info for imap.
 *
 * Authors: Siviah Nallagatla <snallagatla@novell.com>
 *
 * Copyright 2004 Novell Inc
 *
 * This program is free software; you can redistribute it and/or 
 * modify it under the terms of version 2 of the GNU General Public 
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 */

#ifndef CAMEL_GROUPWISE_PRIVATE_H
#define CAMEL_GROUPWISE_PRIVATE_H 1

#ifdef __cplusplus
extern "C" {
#pragma }
#endif /* __cplusplus */

/* need a way to configure and save this data, if this header is to
   be installed.  For now, dont install it */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifdef ENABLE_THREADS
#include "libedataserver/e-msgport.h"
#endif


#ifdef ENABLE_THREADS
#define CAMEL_GROUPWISE_FOLDER_LOCK(f, l) (e_mutex_lock(((CamelGroupwiseFolder *)f)->priv->l))
#define CAMEL_GROUPWISE_FOLDER_UNLOCK(f, l) (e_mutex_unlock(((CamelGroupwiseFolder *)f)->priv->l))
#else
#define CAMEL_GROUPWISE_FOLDER_LOCK(f, l)
#define CAMEL_GROUPWISE_FOLDER_UNLOCK(f, l)
#endif


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* CAMEL_IMAP_PRIVATE_H */

