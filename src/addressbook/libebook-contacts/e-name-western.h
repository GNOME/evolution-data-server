/*
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#ifndef __E_NAME_WESTERN_H__
#define __E_NAME_WESTERN_H__

#if !defined (__LIBEBOOK_CONTACTS_H_INSIDE__) && !defined (LIBEBOOK_CONTACTS_COMPILATION)
#error "Only <libebook-contacts/libebook-contacts.h> should be included directly."
#endif

#include <glib.h>
#include <glib-object.h>

G_BEGIN_DECLS

typedef struct {

	/* Public */
	gchar *prefix;
	gchar *first;
	gchar *middle;
	gchar *nick;
	gchar *last;
	gchar *suffix;

	/* Private */
	gchar *full;
} ENameWestern;

GType         e_name_western_get_type (void) G_GNUC_CONST;
ENameWestern *e_name_western_parse (const gchar   *full_name);
void          e_name_western_free  (ENameWestern *w);
ENameWestern *e_name_western_copy  (ENameWestern *w);

G_END_DECLS

#endif /* __E_NAME_WESTERN_H__ */
