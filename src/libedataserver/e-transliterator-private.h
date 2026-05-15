/*
 * SPDX-FileCopyrightText: (C) 2013 Intel Corporation
 * SPDX-License-Identifier: LGPL-2.0-or-later
 * SPDX-FileContributor: Tristan Van Berkom <tristanvb@openismus.com>
 */

#if !defined (__LIBEDATASERVER_H_INSIDE__) && !defined (LIBEDATASERVER_COMPILATION)
#error "Only <libedataserver/libedataserver.h> should be included directly."
#endif

#ifndef E_TRANSLITERATOR_PRIVATE_H
#define E_TRANSLITERATOR_PRIVATE_H

#include <glib-object.h>

G_BEGIN_DECLS

#if __GNUC__ >= 4
#  define E_TRANSLITERATOR_LOCAL __attribute__ ((visibility ("hidden")))
#else
#  define E_TRANSLITERATOR_LOCAL
#endif

/**
 * ETransliterator:
 *
 * A private opaque type describing an alphabetic index
 *
 * Since: 3.12
 **/
typedef struct _ETransliterator ETransliterator;

/* defined in e-transliterator-private.cpp, and used by by e-collator.c */
E_TRANSLITERATOR_LOCAL ETransliterator *_e_transliterator_cxx_new             (const gchar      *transliterator_id);
E_TRANSLITERATOR_LOCAL void             _e_transliterator_cxx_free            (ETransliterator  *transliterator);
E_TRANSLITERATOR_LOCAL gchar           *_e_transliterator_cxx_transliterate   (ETransliterator  *transliterator,
									       const gchar      *str);

G_END_DECLS

#endif /* E_TRANSLITERATOR_PRIVATE_H */
