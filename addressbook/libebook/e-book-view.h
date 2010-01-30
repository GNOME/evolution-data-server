/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * The Evolution addressbook client object.
 *
 * Author:
 *   Nat Friedman (nat@ximian.com)
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 */

#ifndef __E_BOOK_VIEW_H__
#define __E_BOOK_VIEW_H__

#include <glib.h>
#include <glib-object.h>
#include "e-book-types.h"

#define E_TYPE_BOOK_VIEW           (e_book_view_get_type ())
#define E_BOOK_VIEW(o)             (G_TYPE_CHECK_INSTANCE_CAST ((o), E_TYPE_BOOK_VIEW, EBookView))
#define E_BOOK_VIEW_CLASS(k)       (G_TYPE_CHECK_CLASS_CAST((k), E_TYPE_BOOK_VIEW, EBookViewClass))
#define E_IS_BOOK_VIEW(o)          (G_TYPE_CHECK_INSTANCE_TYPE ((o), E_TYPE_BOOK_VIEW))
#define E_IS_BOOK_VIEW_CLASS(k)    (G_TYPE_CHECK_CLASS_TYPE ((k), E_TYPE_BOOK_VIEW))
#define E_BOOK_VIEW_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), E_TYPE_BOOK_VIEW, EBookViewClass))

G_BEGIN_DECLS

typedef struct _EBookView        EBookView;
typedef struct _EBookViewClass   EBookViewClass;
typedef struct _EBookViewPrivate EBookViewPrivate;

struct _EBook;  /* Forward reference */

struct _EBookView {
	GObject     parent;
	/*< private >*/
	EBookViewPrivate *priv;
};

struct _EBookViewClass {
	GObjectClass parent;

	/*
	 * Signals.
	 */
	void (* contacts_changed)  (EBookView *book_view, const GList *contacts);
	void (* contacts_removed)  (EBookView *book_view, const GList *ids);
	void (* contacts_added)    (EBookView *book_view, const GList *contacts);
	void (* sequence_complete) (EBookView *book_view, EBookViewStatus status);
	void (* status_message)    (EBookView *book_view, const gchar *message);

	/* Padding for future expansion */
	void (*_ebook_reserved0) (void);
	void (*_ebook_reserved1) (void);
	void (*_ebook_reserved2) (void);
	void (*_ebook_reserved3) (void);
	void (*_ebook_reserved4) (void);
};

GType              e_book_view_get_type               (void);

void               e_book_view_start                  (EBookView *book_view);
void               e_book_view_stop                   (EBookView *book_view);

struct _EBook     *e_book_view_get_book               (EBookView *book_view);

G_END_DECLS

#endif /* __E_BOOK_VIEW_H__ */
