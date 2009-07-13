/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  Authors: Jeffrey Stedfast <fejj@ximian.com>
 *
 *  Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU Lesser General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 */

#ifndef __CAMEL_GPG_CONTEXT_H__
#define __CAMEL_GPG_CONTEXT_H__

#include <camel/camel-cipher-context.h>

#define CAMEL_GPG_CONTEXT_TYPE     (camel_gpg_context_get_type ())
#define CAMEL_GPG_CONTEXT(obj)     (CAMEL_CHECK_CAST((obj), CAMEL_GPG_CONTEXT_TYPE, CamelGpgContext))
#define CAMEL_GPG_CONTEXT_CLASS(k) (CAMEL_CHECK_CLASS_CAST ((k), CAMEL_GPG_CONTEXT_TYPE, CamelGpgContextClass))
#define CAMEL_IS_GPG_CONTEXT(o)    (CAMEL_CHECK_TYPE((o), CAMEL_GPG_CONTEXT_TYPE))

G_BEGIN_DECLS

typedef struct _CamelGpgContext CamelGpgContext;
typedef struct _CamelGpgContextClass CamelGpgContextClass;

struct _CamelGpgContext {
	CamelCipherContext parent_object;

	gboolean always_trust;
};

struct _CamelGpgContextClass {
	CamelCipherContextClass parent_class;

};

CamelType camel_gpg_context_get_type (void);

CamelCipherContext *camel_gpg_context_new (CamelSession *session);

void camel_gpg_context_set_always_trust (CamelGpgContext *ctx, gboolean trust);

G_END_DECLS

#endif /* __CAMEL_GPG_CONTEXT_H__ */
