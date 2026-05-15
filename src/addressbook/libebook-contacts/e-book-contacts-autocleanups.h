/*
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#if !defined (__LIBEBOOK_CONTACTS_H_INSIDE__) && !defined (LIBEBOOK_CONTACTS_COMPILATION)
#error "Only <libebook-contacts/libebook-contacts.h> should be included directly."
#endif

#ifndef __E_BOOK_CONTACTS_AUTOCLEANUPS_H__
#define __E_BOOK_CONTACTS_AUTOCLEANUPS_H__

#ifndef __GI_SCANNER__
#ifdef G_DEFINE_AUTOPTR_CLEANUP_FUNC

G_DEFINE_AUTOPTR_CLEANUP_FUNC(EAddressWestern, e_address_western_free)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(EBookQuery, e_book_query_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(EContact, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(EContactAddress, e_contact_address_free)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(EContactCert, e_contact_cert_free)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(EContactDate, e_contact_date_free)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(EContactGeo, e_contact_geo_free)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(EContactName, e_contact_name_free)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(EContactPhoto, e_contact_photo_free)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(ENameWestern, e_name_western_free)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(EPhoneNumber, e_phone_number_free)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(ESourceBackendSummarySetup, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(EVCard, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(EVCardAttribute, e_vcard_attribute_free)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(EVCardAttributeParam, e_vcard_attribute_param_free)

#endif /* G_DEFINE_AUTOPTR_CLEANUP_FUNC */
#endif /* !__GI_SCANNER__ */
#endif /* __E_BOOK_CONTACTS_AUTOCLEANUPS_H__ */
