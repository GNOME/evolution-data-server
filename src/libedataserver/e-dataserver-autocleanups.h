/*
 * e-dataserver-autocleanups.h
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

#if !defined (__LIBEDATASERVER_H_INSIDE__) && !defined (LIBEDATASERVER_COMPILATION)
#error "Only <libedataserver/libedataserver.h> should be included directly."
#endif

#ifndef __E_DATASERVER_AUTOCLEANUPS_H__
#define __E_DATASERVER_AUTOCLEANUPS_H__

#ifndef __GI_SCANNER__
#ifdef G_DEFINE_AUTOPTR_CLEANUP_FUNC

G_DEFINE_AUTOPTR_CLEANUP_FUNC(EClient, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(ECollator, e_collator_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(EAsyncClosure, e_async_closure_free)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(ENamedParameters, e_named_parameters_free)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(EExtensible, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(EExtension, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(EFlag, e_flag_free)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(EGDataOAuth2Authorizer, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(EMemChunk, e_memchunk_destroy)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(EModule, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(ENetworkMonitor, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(EOAuth2Service, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(EOAuth2ServiceBase, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(EOAuth2ServiceGoogle, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(EOAuth2ServiceOutlook, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(EOAuth2Services, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(EOperationPool, e_operation_pool_free)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(ESExp, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(ESoupAuthBearer, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(ESoupSession, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(ESource, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(ESourceAddressBook, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(ESourceAlarms, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(ESourceAuthentication, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(ESourceAutocomplete, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(ESourceAutoconfig, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(ESourceBackend, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(ESourceCalendar, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(ESourceCamel, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(ESourceCollection, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(ESourceContacts, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(ESourceCredentialsProvider, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(ESourceCredentialsProviderImpl, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(ESourceCredentialsProviderImplOAuth2, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(ESourceCredentialsProviderImplPassword, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(ESourceExtension, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(ESourceGoa, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(ESourceLDAP, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(ESourceLocal, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(ESourceMailAccount, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(ESourceMailComposition, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(ESourceMailIdentity, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(ESourceMailSignature, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(ESourceMailSubmission, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(ESourceMailTransport, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(ESourceMDN, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(ESourceMemoList, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(ESourceOffline, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(ESourceOpenPGP, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(ESourceProxy, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(ESourceRefresh, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(ESourceRegistry, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(ESourceRegistryWatcher, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(ESourceResource, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(ESourceRevisionGuards, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(ESourceSecurity, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(ESourceSelectable, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(ESourceSMIME, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(ESourceTaskList, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(ESourceUoa, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(ESourceWeather, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(ESourceWebdav, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(EWebDAVDiscoveredSource, e_webdav_discovered_source_free)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(EWebDAVSession, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(EWebDAVResource, e_webdav_resource_free)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(EWebDAVPropertyChange, e_webdav_property_change_free)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(EWebDAVPrivilege, e_webdav_privilege_free)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(EWebDAVAccessControlEntry, e_webdav_access_control_entry_free)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(EXmlDocument, g_object_unref)

#endif /* G_DEFINE_AUTOPTR_CLEANUP_FUNC */
#endif /* !__GI_SCANNER__ */
#endif /* __E_DATASERVER_AUTOCLEANUPS_H__ */
