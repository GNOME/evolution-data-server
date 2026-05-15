/*
 * SPDX-FileCopyrightText: (C) 1999-2008 Novell, Inc. (www.novell.com)
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#ifndef E_BUFFER_TAGGER_H
#define E_BUFFER_TAGGER_H

#include <gtk/gtk.h>

#if !defined (__LIBEDATASERVERUI_H_INSIDE__) && !defined (LIBEDATASERVERUI_COMPILATION)
#if GTK_CHECK_VERSION(4, 0, 0)
#error "Only <libedataserverui4/libedataserverui4.h> should be included directly."
#else
#error "Only <libedataserverui/libedataserverui.h> should be included directly."
#endif
#endif

G_BEGIN_DECLS

void e_buffer_tagger_connect     (GtkTextView *textview);
void e_buffer_tagger_disconnect  (GtkTextView *textview);
void e_buffer_tagger_update_tags (GtkTextView *textview);

G_END_DECLS

#endif /* E_BUFFER_TAGGER_H */
