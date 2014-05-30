/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2013 Intel Corporation
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
 * Author: Tristan Van Berkom <tristanvb@openismus.com>
 */
#if !defined (__LIBEDATASERVER_H_INSIDE__) && !defined (LIBEDATASERVER_COMPILATION)
#error "Only <libedataserver/libedataserver.h> should be included directly."
#endif

#include <glib.h>
#include <libedataserver/e-source-enumtypes.h>
#include <libedataserver/e-data-server-util.h>

#ifndef E_TRANSLITERATOR_H
#define E_TRANSLITERATOR_H

#define E_TYPE_TRANSLITERATOR (e_transliterator_get_type ())

G_BEGIN_DECLS

/**
 * ETransliterator:
 *
 * An opaque object used for string transliterations.
 *
 * Since: 3.12
 */
typedef struct _ETransliterator ETransliterator;

GType                e_transliterator_get_type         (void);
ETransliterator     *e_transliterator_new              (const gchar     *id);
ETransliterator     *e_transliterator_ref              (ETransliterator *transliterator);
void                 e_transliterator_unref            (ETransliterator *transliterator);
gchar               *e_transliterator_transliterate    (ETransliterator *transliterator,
							const gchar     *str);


G_END_DECLS

#endif /* E_TRANSLITERATOR_H */
