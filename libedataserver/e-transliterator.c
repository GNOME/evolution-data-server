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

/**
 * SECTION: e-transliterator
 * @include: libedataserver/libedataserver.h
 * @short_description: Collation services for locale sensitive sorting
 *
 * The #ETransliterator is a wrapper object around ICU collation services and
 * provides features to sort words in locale specific ways. The transliterator
 * also provides some API for determining features of the active alphabet
 * in the user's locale, and which words should be sorted under which
 * letter in the user's alphabet.
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <stdio.h>
#include <string.h>

#include "e-transliterator.h"
#include "e-transliterator-private.h"

G_DEFINE_BOXED_TYPE (ETransliterator,
		     e_transliterator,
		     e_transliterator_ref, 
		     e_transliterator_unref)

struct _ETransliterator
{
	ECxxTransliterator *transliterator;

	volatile gint       ref_count;
};

/*****************************************************
 *                        API                        *
 *****************************************************/

/**
 * e_transliterator_new:
 * @id: The id of the transliterator to create.
 *
 * Creates a new #ETransliterator for the given @id,
 * IDs are defined by ICU libraries and can be of the
 * form "Any-Latin", "Han-Latin" etc.
 *
 * Transliterator services exist for all script types
 * to convert into Latin, however the same is not true
 * for other scripts (i.e. you cannot transliterate
 * Greek into Chinese or Japanese).
 *
 * For more details on ICU transliteration services,
 * visit this link:
 *     http://userguide.icu-project.org/transforms/general
 *
 * Returns: (transfer full): A newly created #ETransliterator.
 *
 * Since: 3.12
 */
ETransliterator *
e_transliterator_new (const gchar     *id)
{
	ETransliterator *transliterator;

	transliterator = g_slice_new0 (ETransliterator);
	transliterator->transliterator = _e_transliterator_cxx_new (id);
	transliterator->ref_count = 1;

	return transliterator;
}

/**
 * e_transliterator_ref:
 * @transliterator: An #ETransliterator
 *
 * Increases the reference count of @transliterator.
 *
 * Returns: (transfer full): @transliterator
 *
 * Since: 3.12
 */
ETransliterator *
e_transliterator_ref (ETransliterator *transliterator)
{
	g_return_val_if_fail (transliterator != NULL, NULL);

	g_atomic_int_inc (&transliterator->ref_count);

	return transliterator;
}

/**
 * e_transliterator_unref:
 * @transliterator: An #ETransliterator
 *
 * Decreases the reference count of @transliterator.
 * If the reference count reaches 0 then the transliterator is freed
 *
 * Since: 3.12
 */
void
e_transliterator_unref (ETransliterator *transliterator)
{
	g_return_if_fail (transliterator != NULL);

	if (g_atomic_int_dec_and_test (&transliterator->ref_count)) {

		if (transliterator->transliterator)
			_e_transliterator_cxx_free (transliterator->transliterator);

		g_slice_free (ETransliterator, transliterator);
	}
}

/**
 * e_transliterator_transliterate:
 * @transliterator: An #ETransliterator
 * @str: The string to transliterate
 *
 * Transliterates @str according to the transliteration service
 * chosen and passed to e_transliterator_new().
 *
 * Returns: (transfer full): The newly created transliteration of @str
 *
 * Since: 3.12
 */
gchar *
e_transliterator_transliterate (ETransliterator *transliterator,
				const gchar     *str)
{
	g_return_val_if_fail (transliterator != NULL, NULL);

	return _e_transliterator_cxx_transliterate (transliterator->transliterator, str);
}
