/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* Copyright (C) 2001-2004 Novell, Inc. */

#ifndef __E2K_SECURITY_DESCRIPTOR_H__
#define __E2K_SECURITY_DESCRIPTOR_H__

#include "e2k-types.h"
#include <glib-object.h>
#include <libxml/tree.h>

#define E2K_TYPE_SECURITY_DESCRIPTOR            (e2k_security_descriptor_get_type ())
#define E2K_SECURITY_DESCRIPTOR(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), E2K_TYPE_SECURITY_DESCRIPTOR, E2kSecurityDescriptor))
#define E2K_SECURITY_DESCRIPTOR_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), E2K_TYPE_SECURITY_DESCRIPTOR, E2kSecurityDescriptorClass))
#define E2K_IS_SECURITY_DESCRIPTOR(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), E2K_TYPE_SECURITY_DESCRIPTOR))
#define E2K_IS_SECURITY_DESCRIPTOR_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((obj), E2K_TYPE_SECURITY_DESCRIPTOR))

struct _E2kSecurityDescriptor {
	GObject parent;

	E2kSecurityDescriptorPrivate *priv;
};

struct _E2kSecurityDescriptorClass {
	GObjectClass parent_class;
};

GType                  e2k_security_descriptor_get_type      (void);

E2kSecurityDescriptor *e2k_security_descriptor_new           (xmlNodePtr             xml_form,
							      GByteArray            *binary_form);
GByteArray            *e2k_security_descriptor_to_binary     (E2kSecurityDescriptor *sd);

GList                 *e2k_security_descriptor_get_sids      (E2kSecurityDescriptor *sd);
E2kSid                *e2k_security_descriptor_get_default   (E2kSecurityDescriptor *sd);
void                   e2k_security_descriptor_remove_sid    (E2kSecurityDescriptor *sd,
							      E2kSid                *sid);

/* MAPI folder permissions */
#define E2K_PERMISSION_READ_ANY         0x001
#define E2K_PERMISSION_CREATE           0x002
#define E2K_PERMISSION_EDIT_OWNED       0x008
#define E2K_PERMISSION_DELETE_OWNED     0x010
#define E2K_PERMISSION_EDIT_ANY         0x020
#define E2K_PERMISSION_DELETE_ANY       0x040
#define E2K_PERMISSION_CREATE_SUBFOLDER 0x080
#define E2K_PERMISSION_OWNER            0x100
#define E2K_PERMISSION_CONTACT          0x200
#define E2K_PERMISSION_FOLDER_VISIBLE   0x400

#define E2K_PERMISSION_EDIT_MASK	(E2K_PERMISSION_EDIT_ANY | E2K_PERMISSION_EDIT_OWNED)
#define E2K_PERMISSION_DELETE_MASK	(E2K_PERMISSION_DELETE_ANY | E2K_PERMISSION_DELETE_OWNED)

guint32 e2k_security_descriptor_get_permissions (E2kSecurityDescriptor *sd,
						 E2kSid *sid);
void    e2k_security_descriptor_set_permissions (E2kSecurityDescriptor *sd,
						 E2kSid *sid,
						 guint32 perms);

/* Outlook-defined roles */
typedef enum {
	E2K_PERMISSIONS_ROLE_OWNER,
	E2K_PERMISSIONS_ROLE_PUBLISHING_EDITOR,
	E2K_PERMISSIONS_ROLE_EDITOR,
	E2K_PERMISSIONS_ROLE_PUBLISHING_AUTHOR,
	E2K_PERMISSIONS_ROLE_AUTHOR,
	E2K_PERMISSIONS_ROLE_NON_EDITING_AUTHOR,
	E2K_PERMISSIONS_ROLE_REVIEWER,
	E2K_PERMISSIONS_ROLE_CONTRIBUTOR,
	E2K_PERMISSIONS_ROLE_NONE,

	E2K_PERMISSIONS_ROLE_NUM_ROLES,
	E2K_PERMISSIONS_ROLE_CUSTOM = -1
} E2kPermissionsRole;

const gchar        *e2k_permissions_role_get_name  (E2kPermissionsRole role);
guint32            e2k_permissions_role_get_perms (E2kPermissionsRole role);

E2kPermissionsRole e2k_permissions_role_find      (guint perms);

#endif
