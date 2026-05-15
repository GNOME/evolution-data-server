/*
 * SPDX-FileCopyrightText: (C) 2006 OpenedHand Ltd
 * SPDX-FileCopyrightText: (C) 2009 Intel Corporation
 * SPDX-License-Identifier: LGPL-2.0-or-later
 * SPDX-FileContributor: Ross Burton <ross@linux.intel.com>
 */

#ifndef EDS_DISABLE_DEPRECATED

/* Do not generate bindings. */
#ifndef __GI_SCANNER__

#ifndef E_BOOK_VIEW_PRIVATE_H
#define E_BOOK_VIEW_PRIVATE_H

#include "e-book.h"
#include "e-book-client.h"
#include "e-book-client-view.h"
#include "e-book-view.h"

G_BEGIN_DECLS

EBookView *	_e_book_view_new		(EBook *book,
						 EBookClientView *client_view);

G_END_DECLS

#endif /* E_BOOK_VIEW_PRIVATE_H */

#endif /* __GI_SCANNER__ */

#endif /* EDS_DISABLE_DEPRECATED */

