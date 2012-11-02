/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * The Evolution addressbook client object.
 *
 * Author:
 *   Nat Friedman (nat@ximian.com)
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 */

#if !defined (__LIBEBOOK_H_INSIDE__) && !defined (LIBEBOOK_COMPILATION)
#error "Only <libebook/libebook.h> should be included directly."
#endif

#ifndef E_BOOK_DISABLE_DEPRECATED

#ifndef E_BOOK_VIEW_H
#define E_BOOK_VIEW_H

#include <glib-object.h>
#include "e-book-types.h"

/* Standard GObject macros */
#define E_TYPE_BOOK_VIEW \
	(e_book_view_get_type ())
#define E_BOOK_VIEW(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_BOOK_VIEW, EBookView))
#define E_BOOK_VIEW_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), E_TYPE_BOOK_VIEW, EBookViewClass))
#define E_IS_BOOK_VIEW(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_BOOK_VIEW))
#define E_IS_BOOK_VIEW_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), E_TYPE_BOOK_VIEW))
#define E_BOOK_VIEW_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), E_TYPE_BOOK_VIEW, EBookViewClass))

G_BEGIN_DECLS

typedef struct _EBookView EBookView;
typedef struct _EBookViewClass EBookViewClass;
typedef struct _EBookViewPrivate EBookViewPrivate;

struct _EBook;

struct _EBookView {
	GObject parent;
	EBookViewPrivate *priv;
};

struct _EBookViewClass {
	GObjectClass parent_class;

	/* Signals */
	void		(*contacts_changed)	(EBookView *book_view,
						 const GList *contacts);
	void		(*contacts_removed)	(EBookView *book_view,
						 const GList *ids);
	void		(*contacts_added)	(EBookView *book_view,
						 const GList *contacts);
	void		(*sequence_complete)	(EBookView *book_view,
						 EBookViewStatus status);
	void		(*view_complete)	(EBookView *book_view,
						 EBookViewStatus status,
						 const gchar *error_msg);
	void		(*status_message)	(EBookView *book_view,
						 const gchar *message);

	/* Padding for future expansion */
	void		(*_ebook_reserved0)	(void);
	void		(*_ebook_reserved1)	(void);
	void		(*_ebook_reserved2)	(void);
	void		(*_ebook_reserved3)	(void);
	void		(*_ebook_reserved4)	(void);
};

GType		e_book_view_get_type		(void);
struct _EBook *	e_book_view_get_book		(EBookView *book_view);
void		e_book_view_start		(EBookView *book_view);
void		e_book_view_stop		(EBookView *book_view);

G_END_DECLS

#endif /* E_BOOK_VIEW_H */

#endif /* E_BOOK_DISABLE_DEPRECATED */
