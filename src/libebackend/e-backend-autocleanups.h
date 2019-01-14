/*
 * e-backend-autocleanups.h
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

#if !defined (__LIBEBACKEND_H_INSIDE__) && !defined (LIBEBACKEND_COMPILATION)
#error "Only <libebackend/libebackend.h> should be included directly."
#endif

#ifndef E_BACKEND_AUTOCLEANUPS_H
#define E_BACKEND_AUTOCLEANUPS_H

#ifndef __GI_SCANNER__
#ifdef G_DEFINE_AUTOPTR_CLEANUP_FUNC

G_DEFINE_AUTOPTR_CLEANUP_FUNC(EBackendFactory, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(EBackend, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(ECacheColumnValues, e_cache_column_values_free)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(ECacheOfflineChange, e_cache_offline_change_free)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(ECacheColumnInfo, e_cache_column_info_free)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(ECache, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(ECacheReaper, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(ECollectionBackend, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(ECollectionBackendFactory, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(EDataFactory, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(EDBusServer, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(EFileCache, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(EOAuth2Support, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(EServerSideSource, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(ESourceRegistryServer, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(ESubprocessFactory, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(EUserPrompter, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(EUserPrompterServer, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(EUserPrompterServerExtension, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(EWebDAVCollectionBackend, g_object_unref)

#endif /* G_DEFINE_AUTOPTR_CLEANUP_FUNC */
#endif /* !__GI_SCANNER__ */
#endif /* E_BACKEND_AUTOCLEANUPS_H */
