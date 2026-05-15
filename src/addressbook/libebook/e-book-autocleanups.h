/*
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#if !defined (__LIBEBOOK_H_INSIDE__) && !defined (LIBEBOOK_COMPILATION)
#error "Only <libebook/libebook.h> should be included directly."
#endif

#ifndef __E_BOOK_AUTOCLEANUPS_H__
#define __E_BOOK_AUTOCLEANUPS_H__

#ifndef __GI_SCANNER__
#ifdef G_DEFINE_AUTOPTR_CLEANUP_FUNC

G_DEFINE_AUTOPTR_CLEANUP_FUNC(EBookClient, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(EBookClientCursor, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(EBookClientView, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(EDestination, g_object_unref)

#endif /* G_DEFINE_AUTOPTR_CLEANUP_FUNC */
#endif /* !__GI_SCANNER__ */
#endif /* __E_BOOK_AUTOCLEANUPS_H__ */
