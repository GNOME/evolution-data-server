/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* Copyright (C) 2001-2004 Novell, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "e2k-security-descriptor.h"
#include "e2k-sid.h"

#include <stdlib.h>
#include <string.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/xmlmemory.h>

struct _E2kSecurityDescriptorPrivate {
	GByteArray *header;
	guint16 control_flags;
	GArray *aces;

	E2kSid *default_sid, *owner, *group;
	GHashTable *sids, *sid_order;
};

typedef struct {
	guint8  Revision;
	guint8  Sbz1;
	guint16 Control;
	guint32 Owner;
	guint32 Group;
	guint32 Sacl;
	guint32 Dacl;
} E2k_SECURITY_DESCRIPTOR_RELATIVE;

#define E2K_SECURITY_DESCRIPTOR_REVISION 1
#define E2K_SE_DACL_PRESENT              GUINT16_TO_LE(0x0004)
#define E2K_SE_SACL_PRESENT              GUINT16_TO_LE(0x0010)
#define E2K_SE_DACL_PROTECTED            GUINT16_TO_LE(0x1000)

typedef struct {
	guint8  AclRevision;
	guint8  Sbz1;
	guint16 AclSize;
	guint16 AceCount;
	guint16 Sbz2;
} E2k_ACL;

#define E2K_ACL_REVISION 2

typedef struct {
	guint8  AceType;
	guint8  AceFlags;
	guint16 AceSize;
} E2k_ACE_HEADER;

#define E2K_ACCESS_ALLOWED_ACE_TYPE (0x00)
#define E2K_ACCESS_DENIED_ACE_TYPE  (0x01)

#define E2K_OBJECT_INHERIT_ACE      (0x01)
#define E2K_CONTAINER_INHERIT_ACE   (0x02)
#define E2K_INHERIT_ONLY_ACE        (0x08)

typedef struct {
	E2k_ACE_HEADER  Header;
	guint32         Mask;
	E2kSid         *Sid;
} E2k_ACE;

typedef struct {
	guint32 mapi_permission;
	guint32 container_allowed, container_not_denied;
	guint32 object_allowed, object_not_denied;
} E2kPermissionsMap;

/* The magic numbers are from the WSS SDK, except modified to match
 * Outlook a bit.
 */
#define LE(x) (GUINT32_TO_LE (x))
static E2kPermissionsMap permissions_map[] = {
	{ E2K_PERMISSION_READ_ANY,
	  LE(0x000000), LE(0x000000), LE(0x1208a9), LE(0x0008a9) },
	{ E2K_PERMISSION_CREATE,
	  LE(0x000002), LE(0x000002), LE(0x000000), LE(0x000000) },
	{ E2K_PERMISSION_CREATE_SUBFOLDER,
	  LE(0x000004), LE(0x000004), LE(0x000000), LE(0x000000) },
	{ E2K_PERMISSION_EDIT_OWNED,
	  LE(0x000000), LE(0x000000), LE(0x000200), LE(0x000000) },
	{ E2K_PERMISSION_DELETE_OWNED,
	  LE(0x000000), LE(0x000000), LE(0x000400), LE(0x000000) },
	{ E2K_PERMISSION_EDIT_ANY,
	  LE(0x000000), LE(0x000000), LE(0x0c0116), LE(0x1e0316) },
	{ E2K_PERMISSION_DELETE_ANY,
	  LE(0x000000), LE(0x000000), LE(0x010000), LE(0x010400) },
	{ E2K_PERMISSION_OWNER,
	  LE(0x0d4110), LE(0x0d4110), LE(0x000000), LE(0x000000) },
	{ E2K_PERMISSION_CONTACT,
	  LE(0x008000), LE(0x008000), LE(0x000000), LE(0x000000) },
	{ E2K_PERMISSION_FOLDER_VISIBLE,
	  LE(0x1208a9), LE(0x1200a9), LE(0x000000), LE(0x000000) }
};
static const gint permissions_map_size =
	sizeof (permissions_map) / sizeof (permissions_map[0]);

static const guint32 container_permissions_all = LE(0x1fc9bf);
static const guint32 object_permissions_all    = LE(0x1f0fbf);
#undef LE

#define PARENT_TYPE G_TYPE_OBJECT
static GObjectClass *parent_class = NULL;

static void dispose (GObject *object);

static void
class_init (GObjectClass *object_class)
{
	parent_class = g_type_class_ref (PARENT_TYPE);

	object_class->dispose = dispose;
}

static void
init (E2kSecurityDescriptor *sd)
{
	sd->priv = g_new0 (E2kSecurityDescriptorPrivate, 1);

	sd->priv->sids = g_hash_table_new (e2k_sid_binary_sid_hash,
					   e2k_sid_binary_sid_equal);
	sd->priv->sid_order = g_hash_table_new (NULL, NULL);
	sd->priv->aces = g_array_new (FALSE, TRUE, sizeof (E2k_ACE));
}

static void
free_sid (gpointer key, gpointer sid, gpointer data)
{
	g_object_unref (sid);
}

static void
dispose (GObject *object)
{
	E2kSecurityDescriptor *sd = (E2kSecurityDescriptor *) object;

	if (sd->priv) {
		g_hash_table_foreach (sd->priv->sids, free_sid, NULL);
		g_hash_table_destroy (sd->priv->sids);
		g_hash_table_destroy (sd->priv->sid_order);

		g_array_free (sd->priv->aces, TRUE);

		if (sd->priv->header)
			g_byte_array_free (sd->priv->header, TRUE);

		g_free (sd->priv);
		sd->priv = NULL;
	}

	G_OBJECT_CLASS (parent_class)->dispose (object);
}

E2K_MAKE_TYPE (e2k_security_descriptor, E2kSecurityDescriptor, class_init, init, PARENT_TYPE)

/* This determines the relative ordering of any two ACEs in a SID.
 * See docs/security for details.
 */
static gint
ace_compar (E2k_ACE *ace1, E2k_ACE *ace2, E2kSecurityDescriptor *sd)
{
	E2kSidType t1;
	E2kSidType t2;
	gint order1, order2;

	if (ace1 == ace2)
		return 0;

	/* Figure out which overall section the SID will go in and
	 * what its order within that group is.
	 */
	if (ace1->Sid == sd->priv->default_sid)
		t1 = E2K_SID_TYPE_GROUP;
	else
		t1 = e2k_sid_get_sid_type (ace1->Sid);
	order1 = GPOINTER_TO_INT (g_hash_table_lookup (sd->priv->sid_order,
						       ace1->Sid));

	if (ace2->Sid == sd->priv->default_sid)
		t2 = E2K_SID_TYPE_GROUP;
	else
		t2 = e2k_sid_get_sid_type (ace2->Sid);
	order2 = GPOINTER_TO_INT (g_hash_table_lookup (sd->priv->sid_order,
						       ace2->Sid));

	if (t1 != t2) {
		if (t1 == E2K_SID_TYPE_USER)
			return -1;
		else if (t2 == E2K_SID_TYPE_USER)
			return 1;
		else if (t1 == E2K_SID_TYPE_GROUP)
			return 1;
		else /* (t2 == E2K_SID_TYPE_GROUP) */
			return -1;
	}

	if (t1 != E2K_SID_TYPE_GROUP) {
		/* Object-level ACEs go before Container-level ACEs */
		if ((ace1->Header.AceFlags & E2K_OBJECT_INHERIT_ACE) &&
		    !(ace2->Header.AceFlags & E2K_OBJECT_INHERIT_ACE))
			return -1;
		else if ((ace2->Header.AceFlags & E2K_OBJECT_INHERIT_ACE) &&
			 !(ace1->Header.AceFlags & E2K_OBJECT_INHERIT_ACE))
			return 1;

		/* Compare SID order */
		if (order1 < order2)
			return -1;
		else if (order1 > order2)
			return 1;

		/* Allowed ACEs for a given SID go before Denied ACEs */
		if (ace1->Header.AceType == ace2->Header.AceType)
			return 0;
		else if (ace1->Header.AceType == E2K_ACCESS_ALLOWED_ACE_TYPE)
			return -1;
		else
			return 1;
	} else {
		/* For groups, object-level ACEs go after Container-level */
		if ((ace1->Header.AceFlags & E2K_OBJECT_INHERIT_ACE) &&
		    !(ace2->Header.AceFlags & E2K_OBJECT_INHERIT_ACE))
			return 1;
		else if ((ace2->Header.AceFlags & E2K_OBJECT_INHERIT_ACE) &&
			 !(ace1->Header.AceFlags & E2K_OBJECT_INHERIT_ACE))
			return -1;

		/* Default comes after groups in each section */
		if (ace1->Sid != ace2->Sid) {
			if (ace1->Sid == sd->priv->default_sid)
				return 1;
			else if (ace2->Sid == sd->priv->default_sid)
				return -1;
		}

		/* All Allowed ACEs go before all Denied ACEs */
		if (ace1->Header.AceType == E2K_ACCESS_ALLOWED_ACE_TYPE &&
		    ace2->Header.AceType == E2K_ACCESS_DENIED_ACE_TYPE)
			return -1;
		else if (ace1->Header.AceType == E2K_ACCESS_DENIED_ACE_TYPE &&
			 ace2->Header.AceType == E2K_ACCESS_ALLOWED_ACE_TYPE)
			return 1;

		/* Compare SID order */
		if (order1 < order2)
			return -1;
		else if (order1 > order2)
			return 1;
		else
			return 0;
	}
}

static xmlNode *
find_child (xmlNode *node, const xmlChar *name)
{
	for (node = node->xmlChildrenNode; node; node = node->next) {
		if (node->name && !xmlStrcmp (node->name, name))
			return node;
	}
	return NULL;
}

static void
extract_sids (E2kSecurityDescriptor *sd, xmlNodePtr node)
{
	xmlNodePtr string_sid_node, type_node, display_name_node;
	xmlChar *string_sid, *content, *display_name;
	const guint8 *bsid;
	E2kSid *sid;
	E2kSidType type;

	for (; node; node = node->next) {
		if (xmlStrcmp (node->name, (xmlChar *) "sid") != 0) {
			if (node->xmlChildrenNode)
				extract_sids (sd, node->xmlChildrenNode);
			continue;
		}

		string_sid_node = find_child (node, (xmlChar *) "string_sid");
		type_node = find_child (node, (xmlChar *) "type");
		display_name_node = find_child (node, (xmlChar *) "display_name");
		if (!string_sid_node || !type_node)
			continue;

		string_sid = xmlNodeGetContent (string_sid_node);

		content = xmlNodeGetContent (type_node);
		if (!content || !xmlStrcmp (content, (xmlChar *) "user"))
			type = E2K_SID_TYPE_USER;
		else if (!xmlStrcmp (content, (xmlChar *) "group"))
			type = E2K_SID_TYPE_GROUP;
		else if (!xmlStrcmp (content, (xmlChar *) "well_known_group"))
			type = E2K_SID_TYPE_WELL_KNOWN_GROUP;
		else if (!xmlStrcmp (content, (xmlChar *) "alias"))
			type = E2K_SID_TYPE_ALIAS;
		else
			type = E2K_SID_TYPE_INVALID;
		xmlFree (content);

		if (display_name_node)
			display_name = xmlNodeGetContent (display_name_node);
		else
			display_name = NULL;

		sid = e2k_sid_new_from_string_sid (type, (gchar *) string_sid,
						   (gchar *) display_name);
		xmlFree (string_sid);
		if (display_name)
			xmlFree (display_name);

		bsid = e2k_sid_get_binary_sid (sid);
		if (g_hash_table_lookup (sd->priv->sids, bsid)) {
			g_object_unref (sid);
			continue;
		}

		g_hash_table_insert (sd->priv->sids, (gchar *)bsid, sid);
	}
}

static gboolean
parse_sid (E2kSecurityDescriptor *sd, GByteArray *binsd, guint16 *off,
	   E2kSid **sid)
{
	gint sid_len;

	if (binsd->len - *off < E2K_SID_BINARY_SID_MIN_LEN)
		return FALSE;
	sid_len = E2K_SID_BINARY_SID_LEN (binsd->data + *off);
	if (binsd->len - *off < sid_len)
		return FALSE;

	*sid = g_hash_table_lookup (sd->priv->sids, binsd->data + *off);
	*off += sid_len;

	return *sid != NULL;
}

static gboolean
parse_acl (E2kSecurityDescriptor *sd, GByteArray *binsd, guint16 *off)
{
	E2k_ACL aclbuf;
	E2k_ACE acebuf;
	gint ace_count, i;

	if (binsd->len - *off < sizeof (E2k_ACL))
		return FALSE;

	memcpy (&aclbuf, binsd->data + *off, sizeof (aclbuf));
	if (*off + GUINT16_FROM_LE (aclbuf.AclSize) > binsd->len)
		return FALSE;
	if (aclbuf.AclRevision != E2K_ACL_REVISION)
		return FALSE;

	ace_count = GUINT16_FROM_LE (aclbuf.AceCount);

	*off += sizeof (aclbuf);
	for (i = 0; i < ace_count; i++) {
		if (binsd->len - *off < sizeof (E2k_ACE))
			return FALSE;

		memcpy (&acebuf, binsd->data + *off,
			sizeof (acebuf.Header) + sizeof (acebuf.Mask));
		*off += sizeof (acebuf.Header) + sizeof (acebuf.Mask);

		/* If either of OBJECT_INHERIT_ACE or INHERIT_ONLY_ACE
		 * is set, both must be.
		 */
		if (acebuf.Header.AceFlags & E2K_OBJECT_INHERIT_ACE) {
			if (!(acebuf.Header.AceFlags & E2K_INHERIT_ONLY_ACE))
				return FALSE;
		} else {
			if (acebuf.Header.AceFlags & E2K_INHERIT_ONLY_ACE)
				return FALSE;
		}

		if (!parse_sid (sd, binsd, off, &acebuf.Sid))
			return FALSE;

		if (!g_hash_table_lookup (sd->priv->sid_order, acebuf.Sid)) {
			gint size = g_hash_table_size (sd->priv->sid_order);

			g_hash_table_insert (sd->priv->sid_order, acebuf.Sid,
					     GUINT_TO_POINTER (size + 1));
		}

		g_array_append_val (sd->priv->aces, acebuf);
	}

	return TRUE;
}

/**
 * e2k_security_descriptor_new:
 * @xml_form: the XML form of the folder's security descriptor
 * (The "http://schemas.microsoft.com/exchange/security/descriptor"
 * property, aka %E2K_PR_EXCHANGE_SD_XML)
 * @binary_form: the binary form of the folder's security descriptor
 * (The "http://schemas.microsoft.com/exchange/ntsecuritydescriptor"
 * property, aka %E2K_PR_EXCHANGE_SD_BINARY)
 *
 * Constructs an #E2kSecurityDescriptor from the data in @xml_form and
 * @binary_form.
 *
 * Return value: the security descriptor, or %NULL if the data could
 * not be parsed.
 **/
E2kSecurityDescriptor *
e2k_security_descriptor_new (xmlNodePtr xml_form, GByteArray *binary_form)
{
	E2kSecurityDescriptor *sd;
	E2k_SECURITY_DESCRIPTOR_RELATIVE sdbuf;
	guint16 off, header_len;

	g_return_val_if_fail (xml_form != NULL, NULL);
	g_return_val_if_fail (binary_form != NULL, NULL);

	if (binary_form->len < 2)
		return NULL;

	memcpy (&header_len, binary_form->data, 2);
	header_len = GUINT16_FROM_LE (header_len);
	if (header_len + sizeof (sdbuf) > binary_form->len)
		return NULL;

	memcpy (&sdbuf, binary_form->data + header_len, sizeof (sdbuf));
	if (sdbuf.Revision != E2K_SECURITY_DESCRIPTOR_REVISION)
		return NULL;
	if ((sdbuf.Control & (E2K_SE_DACL_PRESENT | E2K_SE_SACL_PRESENT)) !=
	    E2K_SE_DACL_PRESENT)
		return NULL;

	sd = g_object_new (E2K_TYPE_SECURITY_DESCRIPTOR, NULL);
	sd->priv->header = g_byte_array_new ();
	g_byte_array_append (sd->priv->header, binary_form->data, header_len);
	sd->priv->control_flags = sdbuf.Control;

	/* Create a SID for "Default" then extract remaining SIDs from
	 * the XML form since they have display names associated with
	 * them.
	 */
	sd->priv->default_sid =
		e2k_sid_new_from_string_sid (E2K_SID_TYPE_WELL_KNOWN_GROUP,
					     E2K_SID_WKS_EVERYONE, NULL);
	g_hash_table_insert (sd->priv->sids,
			     (gchar *)e2k_sid_get_binary_sid (sd->priv->default_sid),
			     sd->priv->default_sid);
	extract_sids (sd, xml_form);

	off = GUINT32_FROM_LE (sdbuf.Owner) + sd->priv->header->len;
	if (!parse_sid (sd, binary_form, &off, &sd->priv->owner))
		goto lose;
	off = GUINT32_FROM_LE (sdbuf.Group) + sd->priv->header->len;
	if (!parse_sid (sd, binary_form, &off, &sd->priv->group))
		goto lose;

	off = GUINT32_FROM_LE (sdbuf.Dacl) + sd->priv->header->len;
	if (!parse_acl (sd, binary_form, &off))
		goto lose;

	return sd;

 lose:
	g_object_unref (sd);
	return NULL;
}

/**
 * e2k_security_descriptor_to_binary:
 * @sd: an #E2kSecurityDescriptor
 *
 * Converts @sd back to binary (#E2K_PR_EXCHANGE_SD_BINARY) form
 * so it can be PROPPATCHed back to the server.
 *
 * Return value: the binary form of @sd.
 **/
GByteArray *
e2k_security_descriptor_to_binary (E2kSecurityDescriptor *sd)
{
	GByteArray *binsd;
	E2k_SECURITY_DESCRIPTOR_RELATIVE sdbuf;
	E2k_ACL aclbuf;
	E2k_ACE *aces;
	gint off, ace, last_ace = -1, acl_size, ace_count;
	const guint8 *bsid;

	g_return_val_if_fail (E2K_IS_SECURITY_DESCRIPTOR (sd), NULL);

	aces = (E2k_ACE *)sd->priv->aces->data;

	/* Compute the length of the ACL first */
	acl_size = sizeof (E2k_ACL);
	for (ace = ace_count = 0; ace < sd->priv->aces->len; ace++) {
		if (aces[ace].Mask) {
			ace_count++;
			acl_size += GUINT16_FROM_LE (aces[ace].Header.AceSize);
		}
	}

	binsd = g_byte_array_new ();

	/* Exchange-specific header */
	g_byte_array_append (binsd, sd->priv->header->data,
			     sd->priv->header->len);

	/* SECURITY_DESCRIPTOR header */
	memset (&sdbuf, 0, sizeof (sdbuf));
	sdbuf.Revision = E2K_SECURITY_DESCRIPTOR_REVISION;
	sdbuf.Control = sd->priv->control_flags;
	off = sizeof (sdbuf);
	sdbuf.Dacl = GUINT32_TO_LE (off);
	off += acl_size;
	sdbuf.Owner = GUINT32_TO_LE (off);
	bsid = e2k_sid_get_binary_sid (sd->priv->owner);
	off += E2K_SID_BINARY_SID_LEN (bsid);
	sdbuf.Group = GUINT32_TO_LE (off);
	g_byte_array_append (binsd, (gpointer)&sdbuf, sizeof (sdbuf));

	/* ACL header */
	aclbuf.AclRevision = E2K_ACL_REVISION;
	aclbuf.Sbz1        = 0;
	aclbuf.AclSize     = GUINT16_TO_LE (acl_size);
	aclbuf.AceCount    = GUINT16_TO_LE (ace_count);
	aclbuf.Sbz2        = 0;
	g_byte_array_append (binsd, (gpointer)&aclbuf, sizeof (aclbuf));

	/* ACEs */
	for (ace = 0; ace < sd->priv->aces->len; ace++) {
		if (!aces[ace].Mask)
			continue;

		if (last_ace != -1) {
			if (ace_compar (&aces[last_ace], &aces[ace], sd) != -1) {
				g_warning ("ACE order mismatch at %d\n", ace);
				g_byte_array_free (binsd, TRUE);
				return NULL;
			}
		}

		g_byte_array_append (binsd, (gpointer)&aces[ace],
				     sizeof (aces[ace].Header) +
				     sizeof (aces[ace].Mask));
		bsid = e2k_sid_get_binary_sid (aces[ace].Sid);
		g_byte_array_append (binsd, bsid,
				     E2K_SID_BINARY_SID_LEN (bsid));
		last_ace = ace;
	}

	/* Owner and Group */
	bsid = e2k_sid_get_binary_sid (sd->priv->owner);
	g_byte_array_append (binsd, bsid, E2K_SID_BINARY_SID_LEN (bsid));
	bsid = e2k_sid_get_binary_sid (sd->priv->group);
	g_byte_array_append (binsd, bsid, E2K_SID_BINARY_SID_LEN (bsid));

	return binsd;
}

/**
 * e2k_security_descriptor_get_default:
 * @sd: a security descriptor
 *
 * Returns an #E2kSid corresponding to the default permissions
 * associated with @sd. You can pass this to
 * e2k_security_descriptor_get_permissions() and
 * e2k_security_descriptor_set_permissions().
 *
 * Return value: the "Default" SID
 **/
E2kSid *
e2k_security_descriptor_get_default (E2kSecurityDescriptor *sd)
{
	return sd->priv->default_sid;
}

/**
 * e2k_security_descriptor_get_sids:
 * @sd: a security descriptor
 *
 * Returns a #GList containing the SIDs of each user or group
 * represented in @sd. You can pass these SIDs to
 * e2k_security_descriptor_get_permissions(),
 * e2k_security_descriptor_set_permissions(), and
 * e2k_security_descriptor_remove_sid().
 *
 * Return value: a list of SIDs. The caller must free the list
 * with g_list_free(), but should not free the contents.
 **/
GList *
e2k_security_descriptor_get_sids (E2kSecurityDescriptor *sd)
{
	GList *sids = NULL;
	GHashTable *added_sids;
	E2k_ACE *aces;
	gint ace;

	g_return_val_if_fail (E2K_IS_SECURITY_DESCRIPTOR (sd), NULL);

	added_sids = g_hash_table_new (NULL, NULL);
	aces = (E2k_ACE *)sd->priv->aces->data;
	for (ace = 0; ace < sd->priv->aces->len; ace++) {
		if (!g_hash_table_lookup (added_sids, aces[ace].Sid)) {
			g_hash_table_insert (added_sids, aces[ace].Sid,
					     aces[ace].Sid);
			sids = g_list_prepend (sids, aces[ace].Sid);
		}
	}
	g_hash_table_destroy (added_sids);

	return sids;
}

/**
 * e2k_security_descriptor_remove_sid:
 * @sd: a security descriptor
 * @sid: a SID
 *
 * Removes @sid from @sd. If @sid is a user, this means s/he will now
 * have only the default permissions on @sd (unless s/he is a member
 * of a group that is also present in @sd.)
 **/
void
e2k_security_descriptor_remove_sid (E2kSecurityDescriptor *sd,
				    E2kSid *sid)
{
	E2k_ACE *aces;
	gint ace;

	g_return_if_fail (E2K_IS_SECURITY_DESCRIPTOR (sd));
	g_return_if_fail (E2K_IS_SID (sid));

	/* Canonicalize the SID */
	sid = g_hash_table_lookup (sd->priv->sids,
				   e2k_sid_get_binary_sid (sid));
	if (!sid)
		return;

	/* We can't actually remove all trace of the user, because if
	 * he is removed and then re-added without saving in between,
	 * then we need to keep the original AceFlags. So we just
	 * clear out all of the masks, which (assuming the user is
	 * not re-added) will result in him not being written out
	 * when sd is saved.
	 */

	aces = (E2k_ACE *)sd->priv->aces->data;
	for (ace = 0; ace < sd->priv->aces->len; ace++) {
		if (aces[ace].Sid == sid)
			aces[ace].Mask = 0;
	}
}

/**
 * e2k_security_descriptor_get_permissions:
 * @sd: a security descriptor
 * @sid: a SID
 *
 * Computes the MAPI permissions associated with @sid. (Only the
 * permissions *directly* associated with @sid, not any acquired via
 * group memberships or the Default SID.)
 *
 * Return value: the MAPI permissions
 **/
guint32
e2k_security_descriptor_get_permissions (E2kSecurityDescriptor *sd,
					 E2kSid *sid)
{
	E2k_ACE *aces;
	guint32 mapi_perms, checkperm;
	gint ace, map;

	g_return_val_if_fail (E2K_IS_SECURITY_DESCRIPTOR (sd), 0);
	g_return_val_if_fail (E2K_IS_SID (sid), 0);

	/* Canonicalize the SID */
	sid = g_hash_table_lookup (sd->priv->sids,
				   e2k_sid_get_binary_sid (sid));
	if (!sid)
		return 0;

	mapi_perms = 0;
	aces = (E2k_ACE *)sd->priv->aces->data;
	for (ace = 0; ace < sd->priv->aces->len; ace++) {
		if (aces[ace].Sid != sid)
			continue;
		if (aces[ace].Header.AceType == E2K_ACCESS_DENIED_ACE_TYPE)
			continue;

		for (map = 0; map < permissions_map_size; map++) {
			if (aces[ace].Header.AceFlags & E2K_OBJECT_INHERIT_ACE)
				checkperm = permissions_map[map].object_allowed;
			else
				checkperm = permissions_map[map].container_allowed;
			if (!checkperm)
				continue;

			if ((aces[ace].Mask & checkperm) == checkperm)
				mapi_perms |= permissions_map[map].mapi_permission;
		}
	}

	return mapi_perms;
}

/* Put @ace into @sd. If no ACE corresponding to @ace currently exists,
 * it will be added in the right place. If it does already exist, its
 * flags (in particular INHERITED_ACE) will be preserved and only the
 * mask will be changed.
 */
static void
set_ace (E2kSecurityDescriptor *sd, E2k_ACE *ace)
{
	E2k_ACE *aces = (E2k_ACE *)sd->priv->aces->data;
	gint low, mid = 0, high, cmp = -1;

	low = 0;
	high = sd->priv->aces->len - 1;
	while (low <= high) {
		mid = (low + high) / 2;
		cmp = ace_compar (ace, &aces[mid], sd);
		if (cmp == 0) {
			if (ace->Mask)
				aces[mid].Mask = ace->Mask;
			else
				g_array_remove_index (sd->priv->aces, mid);
			return;
		} else if (cmp < 0)
			high = mid - 1;
		else
			low = mid + 1;
	}

	if (ace->Mask)
		g_array_insert_vals (sd->priv->aces, cmp < 0 ? mid : mid + 1, ace, 1);
}

/**
 * e2k_security_descriptor_set_permissions:
 * @sd: a security descriptor
 * @sid: a SID
 * @perms: the MAPI permissions
 *
 * Updates or sets @sid's permissions on @sd.
 **/
void
e2k_security_descriptor_set_permissions (E2kSecurityDescriptor *sd,
					 E2kSid *sid, guint32 perms)
{
	E2k_ACE ace;
	guint32 object_allowed, object_denied;
	guint32 container_allowed, container_denied;
	const guint8 *bsid;
	E2kSid *sid2;
	gint map;

	g_return_if_fail (E2K_IS_SECURITY_DESCRIPTOR (sd));
	g_return_if_fail (E2K_IS_SID (sid));

	bsid = e2k_sid_get_binary_sid (sid);
	sid2 = g_hash_table_lookup (sd->priv->sids, bsid);
	if (!sid2) {
		gint size = g_hash_table_size (sd->priv->sid_order);

		g_hash_table_insert (sd->priv->sids, (gchar *)bsid, sid);
		g_object_ref (sid);

		g_hash_table_insert (sd->priv->sid_order, sid,
				     GUINT_TO_POINTER (size + 1));
	} else
		sid = sid2;

	object_allowed    = 0;
	object_denied     = object_permissions_all;
	container_allowed = 0;
	container_denied  = container_permissions_all;

	for (map = 0; map < permissions_map_size; map++) {
		if (!(permissions_map[map].mapi_permission & perms))
			continue;

		object_allowed    |=  permissions_map[map].object_allowed;
		object_denied     &= ~permissions_map[map].object_not_denied;
		container_allowed |=  permissions_map[map].container_allowed;
		container_denied  &= ~permissions_map[map].container_not_denied;
	}

	ace.Sid = sid;
	ace.Header.AceSize = GUINT16_TO_LE (sizeof (ace.Header) +
					    sizeof (ace.Mask) +
					    E2K_SID_BINARY_SID_LEN (bsid));

	ace.Header.AceType  = E2K_ACCESS_ALLOWED_ACE_TYPE;
	ace.Header.AceFlags = E2K_OBJECT_INHERIT_ACE | E2K_INHERIT_ONLY_ACE;
	ace.Mask = object_allowed;
	set_ace (sd, &ace);
	if (sid != sd->priv->default_sid) {
		ace.Header.AceType  = E2K_ACCESS_DENIED_ACE_TYPE;
		ace.Header.AceFlags = E2K_OBJECT_INHERIT_ACE | E2K_INHERIT_ONLY_ACE;
		ace.Mask = object_denied;
		set_ace (sd, &ace);
	}

	ace.Header.AceType  = E2K_ACCESS_ALLOWED_ACE_TYPE;
	ace.Header.AceFlags = E2K_CONTAINER_INHERIT_ACE;
	ace.Mask = container_allowed;
	set_ace (sd, &ace);
	if (sid != sd->priv->default_sid) {
		ace.Header.AceType  = E2K_ACCESS_DENIED_ACE_TYPE;
		ace.Header.AceFlags = E2K_CONTAINER_INHERIT_ACE;
		ace.Mask = container_denied;
		set_ace (sd, &ace);
	}
}

static struct {
	const gchar *name;
	guint32 perms;
} roles[E2K_PERMISSIONS_ROLE_NUM_ROLES] = {
	/* i18n: These are Outlook's words for the default roles in
	   the folder permissions dialog. */
	{ N_("Owner"),             (E2K_PERMISSION_FOLDER_VISIBLE |
				    E2K_PERMISSION_READ_ANY |
				    E2K_PERMISSION_CREATE |
				    E2K_PERMISSION_DELETE_OWNED |
				    E2K_PERMISSION_EDIT_OWNED |
				    E2K_PERMISSION_DELETE_ANY |
				    E2K_PERMISSION_EDIT_ANY |
				    E2K_PERMISSION_CREATE_SUBFOLDER |
				    E2K_PERMISSION_CONTACT |
				    E2K_PERMISSION_OWNER) },
	{ N_("Publishing Editor"), (E2K_PERMISSION_FOLDER_VISIBLE |
				    E2K_PERMISSION_READ_ANY |
				    E2K_PERMISSION_CREATE |
				    E2K_PERMISSION_DELETE_OWNED |
				    E2K_PERMISSION_EDIT_OWNED |
				    E2K_PERMISSION_DELETE_ANY |
				    E2K_PERMISSION_EDIT_ANY |
				    E2K_PERMISSION_CREATE_SUBFOLDER) },
	{ N_("Editor"),            (E2K_PERMISSION_FOLDER_VISIBLE |
				    E2K_PERMISSION_READ_ANY |
				    E2K_PERMISSION_CREATE |
				    E2K_PERMISSION_DELETE_OWNED |
				    E2K_PERMISSION_EDIT_OWNED |
				    E2K_PERMISSION_DELETE_ANY |
				    E2K_PERMISSION_EDIT_ANY) },
	{ N_("Publishing Author"), (E2K_PERMISSION_FOLDER_VISIBLE |
				    E2K_PERMISSION_READ_ANY |
				    E2K_PERMISSION_CREATE |
				    E2K_PERMISSION_DELETE_OWNED |
				    E2K_PERMISSION_EDIT_OWNED |
				    E2K_PERMISSION_CREATE_SUBFOLDER) },
	{ N_("Author"),            (E2K_PERMISSION_FOLDER_VISIBLE |
				    E2K_PERMISSION_READ_ANY |
				    E2K_PERMISSION_CREATE |
				    E2K_PERMISSION_DELETE_OWNED |
				    E2K_PERMISSION_EDIT_OWNED) },
	{ N_("Non-editing Author"),(E2K_PERMISSION_FOLDER_VISIBLE |
				    E2K_PERMISSION_READ_ANY |
				    E2K_PERMISSION_CREATE |
				    E2K_PERMISSION_DELETE_OWNED) },
	{ N_("Reviewer"),          (E2K_PERMISSION_FOLDER_VISIBLE |
				    E2K_PERMISSION_READ_ANY) },
	{ N_("Contributor"),       (E2K_PERMISSION_FOLDER_VISIBLE |
				    E2K_PERMISSION_CREATE) },
	{ N_("None"),              (E2K_PERMISSION_FOLDER_VISIBLE) }
};

/**
 * e2k_permissions_role_get_name:
 * @role: a permissions role
 *
 * Returns the localized name corresponding to @role
 *
 * Return value: the name
 **/
const gchar *
e2k_permissions_role_get_name (E2kPermissionsRole role)
{
	if (role == E2K_PERMISSIONS_ROLE_CUSTOM)
		return _("Custom");

	g_return_val_if_fail (role > E2K_PERMISSIONS_ROLE_CUSTOM &&
			      role < E2K_PERMISSIONS_ROLE_NUM_ROLES, NULL);
	return _(roles[role].name);
}

/**
 * e2k_permissions_role_get_perms
 * @role: a permissions role
 *
 * Returns the MAPI permissions associated with @role. @role may not
 * be %E2K_PERMISSIONS_ROLE_CUSTOM.
 *
 * Return value: the MAPI permissions
 **/
guint32
e2k_permissions_role_get_perms (E2kPermissionsRole role)
{
	g_return_val_if_fail (role >= E2K_PERMISSIONS_ROLE_CUSTOM &&
			      role < E2K_PERMISSIONS_ROLE_NUM_ROLES, 0);
	return roles[role].perms;
}

/**
 * e2k_permissions_role_find:
 * @perms: MAPI permissions
 *
 * Finds the #E2kPermissionsRole value associated with @perms. If
 * @perms don't describe any standard role, the return value will be
 * %E2K_PERMISSIONS_ROLE_CUSTOM
 *
 * Return value: the role
 **/
E2kPermissionsRole
e2k_permissions_role_find (guint perms)
{
	gint role;

	/* "Folder contact" isn't actually a permission, and is ignored
	 * for purposes of roles.
	 */
	perms &= ~E2K_PERMISSION_CONTACT;

	/* The standard "None" permission includes "Folder visible",
	 * but 0 counts as "None" too.
	 */
	if (perms == 0)
		return E2K_PERMISSIONS_ROLE_NONE;

	for (role = 0; role < E2K_PERMISSIONS_ROLE_NUM_ROLES; role++) {
		if ((roles[role].perms & ~E2K_PERMISSION_CONTACT) == perms)
			return role;
	}

	return E2K_PERMISSIONS_ROLE_CUSTOM;
}
