/*
 * SPDX-FileCopyrightText: (C) 2013 Intel Corporation
 * SPDX-License-Identifier: LGPL-2.0-or-later
 * SPDX-FileContributor: Tristan Van Berkom <tristanvb@openismus.com>
 */

#if !defined (__LIBEDATASERVER_H_INSIDE__) && !defined (LIBEDATASERVER_COMPILATION)
#error "Only <libedataserver/libedataserver.h> should be included directly."
#endif

#ifndef E_ALPHABET_INDEX_PRIVATE_H
#define E_ALPHABET_INDEX_PRIVATE_H

#include <glib-object.h>

G_BEGIN_DECLS

#if __GNUC__ >= 4
#  define E_ALPHABET_INDEX_LOCAL __attribute__ ((visibility ("hidden")))
#else
#  define E_ALPHABET_INDEX_LOCAL
#endif

/**
 * EAlphabetIndex:
 *
 * A private opaque type describing an alphabetic index
 *
 * Since: 3.10
 **/
typedef struct _EAlphabetIndex EAlphabetIndex;

/* defined in e-alphabet-index-private.cpp, and used by by e-collator.c */

E_ALPHABET_INDEX_LOCAL EAlphabetIndex *_e_alphabet_index_cxx_new_for_language (const gchar     *language);
E_ALPHABET_INDEX_LOCAL void            _e_alphabet_index_cxx_free             (EAlphabetIndex  *alphabet_index);
E_ALPHABET_INDEX_LOCAL gint            _e_alphabet_index_cxx_get_index        (EAlphabetIndex  *alphabet_index,
									       const gchar     *word);
E_ALPHABET_INDEX_LOCAL gchar         **_e_alphabet_index_cxx_get_labels       (EAlphabetIndex  *alphabet_index,
									       gint            *n_labels,
									       gint            *underflow,
									       gint            *inflow,
									       gint            *overflow);

G_END_DECLS

#endif /* E_ALPHABET_INDEX_PRIVATE_H */
