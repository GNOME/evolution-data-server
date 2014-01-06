/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2013 Intel Corporation
 *
 * This library is free software you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library; if not, see <http://www.gnu.org/licenses/>.
 *
 * Author: Tristan Van Berkom <tristanvb@openismus.com>
 */

#ifndef CURSOR_SLOT_H
#define CURSOR_SLOT_H

#include <gtk/gtk.h>
#include <libebook/libebook.h>

G_BEGIN_DECLS

#define CURSOR_TYPE_SLOT            (cursor_slot_get_type())
#define CURSOR_SLOT(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), CURSOR_TYPE_SLOT, CursorSlot))
#define CURSOR_SLOT_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), CURSOR_TYPE_SLOT, CursorSlotClass))
#define CURSOR_IS_SLOT(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), CURSOR_TYPE_SLOT))
#define CURSOR_IS_SLOT_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), CURSOR_TYPE_SLOT))
#define CURSOR_SLOT_GET_CLASS(o)    (G_TYPE_INSTANCE_GET_CLASS ((o), CURSOR_SLOT, CursorSlotClass))

typedef struct _CursorSlot         CursorSlot;
typedef struct _CursorSlotPrivate  CursorSlotPrivate;
typedef struct _CursorSlotClass    CursorSlotClass;

struct _CursorSlot
{
	GtkGrid parent_instance;

	CursorSlotPrivate *priv;
};

struct _CursorSlotClass
{
	GtkGridClass parent_class;
};

GType       cursor_slot_get_type         (void) G_GNUC_CONST;
GtkWidget  *cursor_slot_new              (EContact *contact);
void        cursor_slot_set_from_contact (CursorSlot *slot,
					  EContact   *contact);

G_END_DECLS

#endif /* CURSOR_SLOT_H */
