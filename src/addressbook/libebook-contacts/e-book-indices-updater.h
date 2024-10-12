/*
 * SPDX-FileCopyrightText: (C) 2023 Red Hat (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#if !defined (__LIBEBOOK_CONTACTS_H_INSIDE__) && !defined (LIBEBOOK_CONTACTS_COMPILATION)
#error "Only <libebook-contacts/libebook-contacts.h> should be included directly."
#endif

#ifndef E_BOOK_INDICES_UPDATER_H
#define E_BOOK_INDICES_UPDATER_H

#include <libebook-contacts/e-book-contacts-utils.h>

/* Standard GObject macros */
#define E_TYPE_BOOK_INDICES_UPDATER \
	(e_book_indices_updater_get_type ())
#define E_BOOK_INDICES_UPDATER(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_BOOK_INDICES_UPDATER, EBookIndicesUpdater))
#define E_BOOK_INDICES_UPDATER_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), E_TYPE_BOOK_INDICES_UPDATER, EBookIndicesUpdaterClass))
#define E_IS_BOOK_INDICES_UPDATER(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_BOOK_INDICES_UPDATER))
#define E_IS_BOOK_INDICES_UPDATER_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), E_TYPE_BOOK_INDICES_UPDATER))
#define E_BOOK_INDICES_UPDATER_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), E_TYPE_BOOK_INDICES_UPDATER, EBookIndicesUpdaterClass))

G_BEGIN_DECLS

typedef struct _EBookIndicesUpdater EBookIndicesUpdater;
typedef struct _EBookIndicesUpdaterClass EBookIndicesUpdaterClass;
typedef struct _EBookIndicesUpdaterPrivate EBookIndicesUpdaterPrivate;

/**
 * EBookIndicesUpdater:
 *
 * An abstract object to handle EBookIndices changes.
 *
 * Since: 3.50
 */
struct _EBookIndicesUpdater {
	/*< private >*/
	GObject parent;
	EBookIndicesUpdaterPrivate *priv;
};

struct _EBookIndicesUpdaterClass {
	/*< private >*/
	GObjectClass parent_class;
};

GType		e_book_indices_updater_get_type		(void) G_GNUC_CONST;
gboolean	e_book_indices_updater_take_indices	(EBookIndicesUpdater *self,
							 EBookIndices *indices);
const EBookIndices *
		e_book_indices_updater_get_indices	(EBookIndicesUpdater *self);
void		e_book_indices_set_ascending_sort	(EBookIndicesUpdater *self,
							 gboolean ascending_sort);
gboolean	e_book_indices_get_ascending_sort	(EBookIndicesUpdater *self);
gboolean	e_book_indices_updater_add		(EBookIndicesUpdater *self,
							 const gchar *uid,
							 guint indices_index);
gboolean	e_book_indices_updater_remove		(EBookIndicesUpdater *self,
							 const gchar *uid);

G_END_DECLS

#endif /* E_BOOK_INDICES_UPDATER_H */
