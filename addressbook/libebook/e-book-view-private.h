/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * The Evolution addressbook client object.
 *
 * Author:
 *   Nat Friedman (nat@ximian.com)
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 */

#ifndef __E_BOOK_VIEW_PRIVATE_H__
#define __E_BOOK_VIEW_PRIVATE_H__

#include <glib.h>
#include <glib-object.h>

#include "Evolution-DataServer-Addressbook.h"
#include "e-book-view-listener.h"

/* Creating a new addressbook. */
EBookView *e_book_view_new (GNOME_Evolution_Addressbook_BookView corba_book_view, EBookViewListener *listener);

G_END_DECLS

#endif /* ! __E_BOOK_VIEW_PRIVATE_H__ */
