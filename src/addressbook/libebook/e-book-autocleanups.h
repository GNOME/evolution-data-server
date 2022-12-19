/*
 * e-book-autocleanups.h
 *
 * This library is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library. If not, see <http://www.gnu.org/licenses/>.
 *
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
