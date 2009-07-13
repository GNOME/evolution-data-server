/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/* Copyright (C) 2002-2004 Novell, Inc.
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

#include <string.h>

#include "e2k-rule-xml.h"
#include "e2k-action.h"
#include "e2k-properties.h"
#include "e2k-propnames.h"
#include "e2k-proptags.h"
#include "e2k-utils.h"
#include "mapi.h"

static const gchar *contains_types[] = { NULL, "contains", NULL, NULL, NULL, "not contains", NULL, NULL };
static const gchar *subject_types[] = { "is", "contains", "starts with", NULL, "is not", "not contains", "not starts with", NULL };
#define E2K_FL_NEGATE 4
#define E2K_FL_MAX 8

#if 0
static gboolean
fuzzy_level_from_name (const gchar *name, const gchar *map[],
		       gint *fuzzy_level, gboolean *negated)
{
	gint i;

	for (i = 0; i < E2K_FL_MAX; i++) {
		if (map[i] && !strcmp (name, map[i])) {
			*fuzzy_level = i & ~E2K_FL_NEGATE;
			*negated = (*fuzzy_level != i);
			return TRUE;
		}
	}

	return FALSE;
}
#endif

static inline const gchar *
fuzzy_level_to_name (gint fuzzy_level, gboolean negated, const gchar *map[])
{
	fuzzy_level = E2K_FL_MATCH_TYPE (fuzzy_level);
	if (negated)
		fuzzy_level |= E2K_FL_NEGATE;

	return map[fuzzy_level];
}

static const gchar *is_types[] = { NULL, NULL, NULL, NULL, "is", "is not" };
static const gchar *date_types[] = { "before", "before", "after", "after", NULL, NULL };
static const gchar *gsizeypes[] = { "less than", "less than", "greater than", "greater than", NULL, NULL };

#if 0
static gboolean
relop_from_name (const gchar *name, const gchar *map[],
		 E2kRestrictionRelop *relop)
{
	gint i;

	for (i = 0; i < E2K_RELOP_RE; i++) {
		if (map[i] && !strcmp (name, map[i])) {
			*relop = i;
			return TRUE;
		}
	}

	return FALSE;
}
#endif

static inline const gchar *
relop_to_name (E2kRestrictionRelop relop, gboolean negated, const gchar *map[])
{
	static const gint negate_map[] = {
		E2K_RELOP_GE, E2K_RELOP_GT, E2K_RELOP_LE, E2K_RELOP_LT,
		E2K_RELOP_NE, E2K_RELOP_EQ
	};

	if (negated)
		relop = negate_map[relop];

	return map[relop];
}

/* Check if @rn encodes Outlook's "Message was sent only to me" rule */
static gboolean
restriction_is_only_to_me (E2kRestriction *rn)
{
	E2kRestriction *sub;

	if (rn->type != E2K_RESTRICTION_AND || rn->res.and.nrns != 3)
		return FALSE;

	sub = rn->res.and.rns[0];
	if (sub->type != E2K_RESTRICTION_PROPERTY ||
	    sub->res.property.relop != E2K_RELOP_EQ ||
	    sub->res.property.pv.prop.proptag != E2K_PROPTAG_PR_MESSAGE_TO_ME ||
	    sub->res.property.pv.value == NULL)
		return FALSE;

	sub = rn->res.and.rns[1];
	if (sub->type != E2K_RESTRICTION_NOT)
		return FALSE;
	sub = sub->res.not.rn;
	if (sub->type != E2K_RESTRICTION_CONTENT ||
	    !(sub->res.content.fuzzy_level & E2K_FL_SUBSTRING) ||
	    sub->res.content.pv.prop.proptag != E2K_PROPTAG_PR_DISPLAY_TO ||
	    strcmp (sub->res.content.pv.value, ";") != 0)
		return FALSE;

	sub = rn->res.and.rns[2];
	if (sub->type != E2K_RESTRICTION_PROPERTY ||
	    sub->res.content.pv.prop.proptag != E2K_PROPTAG_PR_DISPLAY_CC ||
	    strcmp (sub->res.content.pv.value, "") != 0)
		return FALSE;

	return TRUE;
}

/* Check if @rn encodes Outlook's "is a delegatable meeting request" rule */
static gboolean
restriction_is_delegation (E2kRestriction *rn)
{
	E2kRestriction *sub;

	if (rn->type != E2K_RESTRICTION_AND || rn->res.and.nrns != 3)
		return FALSE;

	sub = rn->res.and.rns[0];
	if (sub->type != E2K_RESTRICTION_CONTENT ||
	    E2K_FL_MATCH_TYPE (sub->res.content.fuzzy_level) != E2K_FL_PREFIX ||
	    sub->res.content.pv.prop.proptag != E2K_PROPTAG_PR_MESSAGE_CLASS ||
	    strcmp (sub->res.content.pv.value, "IPM.Schedule.Meeting") != 0)
		return FALSE;

	sub = rn->res.and.rns[1];
	if (sub->type != E2K_RESTRICTION_NOT ||
	    sub->res.not.rn->type != E2K_RESTRICTION_EXIST ||
	    sub->res.not.rn->res.exist.prop.proptag != E2K_PROPTAG_PR_DELEGATED_BY_RULE)
		return FALSE;

	sub = rn->res.and.rns[2];
	if (sub->type != E2K_RESTRICTION_OR || sub->res.or.nrns != 2)
		return FALSE;

	sub = rn->res.and.rns[2]->res.or.rns[0];
	if (sub->type != E2K_RESTRICTION_NOT ||
	    sub->res.not.rn->type != E2K_RESTRICTION_EXIST ||
	    sub->res.not.rn->res.exist.prop.proptag != E2K_PROPTAG_PR_SENSITIVITY)
		return FALSE;

	sub = rn->res.and.rns[2]->res.or.rns[1];
	if (sub->type != E2K_RESTRICTION_PROPERTY ||
	    sub->res.property.relop != E2K_RELOP_NE ||
	    sub->res.property.pv.prop.proptag != E2K_PROPTAG_PR_SENSITIVITY ||
	    GPOINTER_TO_INT (sub->res.property.pv.value) != MAPI_SENSITIVITY_PRIVATE)
		return FALSE;

	return TRUE;
}

static xmlNode *
new_value (xmlNode *part,
           const xmlChar *name,
           const xmlChar *type,
           const xmlChar *value)
{
	xmlNode *node;

	node = xmlNewChild (part, NULL, (xmlChar *) "value", NULL);
	xmlSetProp (node, (xmlChar *) "name", name);
	xmlSetProp (node, (xmlChar *) "type", type);
	if (value)
		xmlSetProp (node, (xmlChar *) "value", value);

	return node;
}

static xmlNode *
new_value_int (xmlNode *part,
               const xmlChar *name,
               const xmlChar *type,
               const xmlChar *value_name,
               glong value)
{
	xmlNode *node;
	gchar *str;

	node = xmlNewChild (part, NULL, (xmlChar *) "value", NULL);
	xmlSetProp (node, (xmlChar *) "name", name);
	xmlSetProp (node, (xmlChar *) "type", type);

	str = g_strdup_printf ("%ld", value);
	xmlSetProp (node, (xmlChar *) value_name, (xmlChar *) str);
	g_free (str);

	return node;
}

static xmlNode *
new_part (const xmlChar *part_name)
{
	xmlNode *part;

	part = xmlNewNode (NULL, (xmlChar *) "part");
	xmlSetProp (part, (xmlChar *) "name", part_name);
	return part;
}

static xmlNode *
match (const xmlChar *part_name,
       const xmlChar *value_name,
       const xmlChar *value_value,
       const xmlChar *string_name,
       const xmlChar *string_value)
{
	xmlNode *part, *value;

	part = new_part (part_name);
	value = new_value (
		part, value_name, (xmlChar *) "option", value_value);
	value = new_value (part, string_name, (xmlChar *) "string", NULL);
	xmlNewTextChild (value, NULL, (xmlChar *) "string", string_value);

	return part;
}

static xmlNode *
message_is (const xmlChar *name,
            const xmlChar *type_name,
            const xmlChar *kind,
            gboolean negated)
{
	xmlNode *part;

	part = new_part (name);
	new_value (
		part, type_name, (xmlChar *) "option",
		negated ? (xmlChar *) "is not" : (xmlChar *) "is");
	new_value (part, (xmlChar *) "kind", (xmlChar *) "option", kind);

	return part;
}

static xmlNode *
address_is (E2kRestriction *comment_rn, gboolean recipients, gboolean negated)
{
	xmlNode *part;
	E2kRestriction *rn;
	E2kPropValue *pv;
	const gchar *relation, *display_name, *p;
	gchar *addr, *full_addr;
	GByteArray *ba;
	gint i;

	rn = comment_rn->res.comment.rn;
	if (rn->type != E2K_RESTRICTION_PROPERTY ||
	    rn->res.property.relop != E2K_RELOP_EQ)
		return NULL;
	pv = &rn->res.property.pv;

	if ((recipients && pv->prop.proptag != E2K_PROPTAG_PR_SEARCH_KEY) ||
	    (!recipients && pv->prop.proptag != E2K_PROPTAG_PR_SENDER_SEARCH_KEY))
		return NULL;

	relation = relop_to_name (rn->res.property.relop, negated, is_types);
	if (!relation)
		return NULL;

	/* Extract the address part */
	ba = pv->value;
	p = strchr ((gchar *)ba->data, ':');
	if (p)
		addr = g_ascii_strdown (p + 1, -1);
	else
		addr = g_ascii_strdown ((gchar *)ba->data, -1);

	/* Find the display name in the comment */
	display_name = NULL;
	for (i = 0; i < comment_rn->res.comment.nprops; i++) {
		pv = &comment_rn->res.comment.props[i];
		if (E2K_PROPTAG_TYPE (pv->prop.proptag) == E2K_PT_UNICODE) {
			display_name = pv->value;
			break;
		}
	}

	if (display_name)
		full_addr = g_strdup_printf ("%s <%s>", display_name, addr);
	else
		full_addr = g_strdup_printf ("<%s>", addr);

	if (recipients) {
		part = match (
			(xmlChar *) "recipient",
			(xmlChar *) "recipient-type",
			(xmlChar *) relation,
			(xmlChar *) "recipient",
			(xmlChar *) full_addr);
	} else {
		part = match (
			(xmlChar *) "sender",
			(xmlChar *) "sender-type",
			(xmlChar *) relation,
			(xmlChar *) "sender",
			(xmlChar *) full_addr);
	}

	g_free (full_addr);
	g_free (addr);
	return part;
}

static gboolean
restriction_to_xml (E2kRestriction *rn, xmlNode *partset,
		    E2kRestrictionType wrap_type, gboolean negated)
{
	xmlNode *part, *value, *node;
	E2kPropValue *pv;
	const gchar *match_type;
	gint i;

	switch (rn->type) {
	case E2K_RESTRICTION_AND:
	case E2K_RESTRICTION_OR:
		/* Check for special rules */
		if (restriction_is_only_to_me (rn)) {
			part = message_is (
				(xmlChar *) "message-to-me",
				(xmlChar *) "message-to-me-type",
				(xmlChar *) "only", negated);
			break;
		} else if (restriction_is_delegation (rn)) {
			part = message_is (
				(xmlChar *) "special-message",
				(xmlChar *) "special-message-type",
				(xmlChar *) "delegated-meeting-request",
				negated);
			break;
		}

		/* If we are inside an "and" and hit another "and",
		 * we can just remove the extra level:
		 *    (and foo (and bar baz) quux) =>
		 *    (and foo bar baz quux)
		 * Likewise for "or"s.
		 *
		 * If we are inside an "and" and hit a "(not (or" (or
		 * vice versa), we can use DeMorgan's Law and then
		 * apply the above rule:
		 *    (and foo (not (or bar baz)) quux) =>
		 *    (and foo (and (not bar) (not baz)) quux) =>
		 *    (and foo (not bar) (not baz) quux)
		 *
		 * This handles both cases.
		 */
		if ((rn->type == wrap_type && !negated) ||
		    (rn->type != wrap_type && negated)) {
			for (i = 0; i < rn->res.and.nrns; i++) {
				if (!restriction_to_xml (rn->res.and.rns[i],
							 partset, wrap_type,
							 negated))
					return FALSE;
			}
			return TRUE;
		}

		/* Otherwise, we have a rule that can't be expressed
		 * as "match all" or "match any".
		 */
		return FALSE;

	case E2K_RESTRICTION_NOT:
		return restriction_to_xml (rn->res.not.rn, partset,
					   wrap_type, !negated);

	case E2K_RESTRICTION_CONTENT:
	{
		gint fuzzy_level = E2K_FL_MATCH_TYPE (rn->res.content.fuzzy_level);

		pv = &rn->res.content.pv;

		switch (pv->prop.proptag) {
		case E2K_PROPTAG_PR_BODY:
			match_type = fuzzy_level_to_name (fuzzy_level, negated,
							  contains_types);
			if (!match_type)
				return FALSE;

			part = match (
				(xmlChar *) "body",
				(xmlChar *) "body-type",
				(xmlChar *) match_type,
				(xmlChar *) "word",
				(xmlChar *) pv->value);
			break;

		case E2K_PROPTAG_PR_SUBJECT:
			match_type = fuzzy_level_to_name (fuzzy_level, negated,
							  subject_types);
			if (!match_type)
				return FALSE;

			part = match (
				(xmlChar *) "subject",
				(xmlChar *) "subject-type",
				(xmlChar *) match_type,
				(xmlChar *) "subject",
				(xmlChar *) pv->value);
			break;

		case E2K_PROPTAG_PR_TRANSPORT_MESSAGE_HEADERS:
			match_type = fuzzy_level_to_name (fuzzy_level, negated,
							  contains_types);
			if (!match_type)
				return FALSE;

			part = match (
				(xmlChar *) "full-headers",
				(xmlChar *) "full-headers-type",
				(xmlChar *) match_type,
				(xmlChar *) "word",
				(xmlChar *) pv->value);
			break;

		case E2K_PROPTAG_PR_MESSAGE_CLASS:
			if ((fuzzy_level == E2K_FL_FULLSTRING) &&
			    !strcmp (pv->value, "IPM.Note.Rules.OofTemplate.Microsoft")) {
				part = message_is (
					(xmlChar *) "special-message",
					(xmlChar *) "special-message-type",
					(xmlChar *) "oof", negated);
			} else if ((fuzzy_level == E2K_FL_PREFIX) &&
				   !strcmp (pv->value, "IPM.Schedule.Meeting")) {
				part = message_is (
					(xmlChar *) "special-message",
					(xmlChar *) "special-message-type",
					(xmlChar *) "meeting-request", negated);
			} else
				return FALSE;

			break;

		default:
			return FALSE;
		}
		break;
	}

	case E2K_RESTRICTION_PROPERTY:
	{
		E2kRestrictionRelop relop;
		const gchar *relation;

		relop = rn->res.property.relop;
		if (relop >= E2K_RELOP_RE)
			return FALSE;

		pv = &rn->res.property.pv;

		switch (pv->prop.proptag) {
		case E2K_PROPTAG_PR_MESSAGE_TO_ME:
			if ((relop == E2K_RELOP_EQ && !pv->value) ||
			    (relop == E2K_RELOP_NE && pv->value))
				negated = !negated;

			part = message_is (
				(xmlChar *) "message-to-me",
				(xmlChar *) "message-to-me-type",
				(xmlChar *) "to", negated);
			break;

		case E2K_PROPTAG_PR_MESSAGE_CC_ME:
			if ((relop == E2K_RELOP_EQ && !pv->value) ||
			    (relop == E2K_RELOP_NE && pv->value))
				negated = !negated;

			part = message_is (
				(xmlChar *) "message-to-me",
				(xmlChar *) "message-to-me-type",
				(xmlChar *) "cc", negated);
			break;

		case E2K_PROPTAG_PR_MESSAGE_DELIVERY_TIME:
		case E2K_PROPTAG_PR_CLIENT_SUBMIT_TIME:
		{
			gchar *timestamp;

			relation = relop_to_name (relop, negated, date_types);
			if (!relation)
				return FALSE;

			if (pv->prop.proptag == E2K_PROPTAG_PR_MESSAGE_DELIVERY_TIME)
				part = new_part ((xmlChar *) "received-date");
			else
				part = new_part ((xmlChar *) "sent-date");

			value = new_value (
				part,
				(xmlChar *) "date-spec-type",
				(xmlChar *) "option",
				(xmlChar *) relation);
			value = new_value (
				part,
				(xmlChar *) "versus",
				(xmlChar *) "datespec", NULL);

			node = xmlNewChild (
				value, NULL, (xmlChar *) "datespec", NULL);
			xmlSetProp (
				node,
				(xmlChar *) "type",
				(xmlChar *) "1");

			timestamp = g_strdup_printf ("%lu", (gulong)e2k_parse_timestamp (pv->value));
			xmlSetProp (
				node,
				(xmlChar *) "value",
				(xmlChar *) timestamp);
			g_free (timestamp);
			break;
		}

		case E2K_PROPTAG_PR_MESSAGE_SIZE:
			relation = relop_to_name (relop, negated, gsizeypes);
			if (!relation)
				return FALSE;

			part = new_part ((xmlChar *) "size");
			new_value (
				part,
				(xmlChar *) "size-type",
				(xmlChar *) "option",
				(xmlChar *) relation);
			new_value_int (
				part,
				(xmlChar *) "versus",
				(xmlChar *) "integer",
				(xmlChar *) "integer",
				GPOINTER_TO_INT (pv->value) / 1024);
			break;

		case E2K_PROPTAG_PR_IMPORTANCE:
			relation = relop_to_name (relop, negated, is_types);
			if (!relation)
				return FALSE;

			part = new_part ((xmlChar *) "importance");
			new_value (
				part,
				(xmlChar *) "importance-type",
				(xmlChar *) "option",
				(xmlChar *) relation);
			new_value_int (
				part,
				(xmlChar *) "importance",
				(xmlChar *) "option",
				(xmlChar *) "value",
				GPOINTER_TO_INT (pv->value));
			break;

		case E2K_PROPTAG_PR_SENSITIVITY:
			relation = relop_to_name (relop, negated, is_types);
			if (!relation)
				return FALSE;

			part = new_part ((xmlChar *) "sensitivity");
			xmlSetProp (
				part,
				(xmlChar *) "name",
				(xmlChar *) "sensitivity");
			new_value (
				part,
				(xmlChar *) "sensitivity-type",
				(xmlChar *) "option",
				(xmlChar *) relation);
			new_value_int (
				part,
				(xmlChar *) "sensitivity",
				(xmlChar *) "option",
				(xmlChar *) "value",
				GPOINTER_TO_INT (pv->value));
			break;

		default:
			return FALSE;
		}
		break;
	}

	case E2K_RESTRICTION_COMMENT:
		part = address_is (rn, FALSE, negated);
		if (!part)
			return FALSE;
		break;

	case E2K_RESTRICTION_BITMASK:
		if (rn->res.bitmask.prop.proptag != E2K_PROPTAG_PR_MESSAGE_FLAGS ||
		    rn->res.bitmask.mask != MAPI_MSGFLAG_HASATTACH)
			return FALSE;

		part = new_part ((xmlChar *) "attachments");
		if (rn->res.bitmask.bitop == E2K_BMR_NEZ) {
			new_value (
				part,
				(xmlChar *) "match-type",
				(xmlChar *) "option",
				negated ?
				(xmlChar *) "not exist" :
				(xmlChar *) "exist");
		} else {
			new_value (
				part,
				(xmlChar *) "match-type",
				(xmlChar *) "option",
				negated ?
				(xmlChar *) "exist" :
				(xmlChar *) "not exist");
		}
		break;

	case E2K_RESTRICTION_SUBRESTRICTION:
		if (rn->res.sub.subtable.proptag != E2K_PROPTAG_PR_MESSAGE_RECIPIENTS)
			return FALSE;
		if (rn->res.sub.rn->type != E2K_RESTRICTION_COMMENT)
			return FALSE;

		part = address_is (rn->res.sub.rn, TRUE, negated);
		if (!part)
			return FALSE;
		break;

	default:
		return FALSE;
	}

	xmlAddChild (partset, part);
	return TRUE;
}

static gchar *
stringify_entryid (guint8 *data, gint len)
{
	GString *string;
	gchar *ret;
	gint i;

	string = g_string_new (NULL);

	for (i = 0; i < len && i < 22; i++)
		g_string_append_printf (string, "%02x", data[i]);
	if (i < len && data[i]) {
		for (; i < len; i++)
			g_string_append_printf (string, "%02x", data[i]);
	}

	ret = string->str;
	g_string_free (string, FALSE);
	return ret;
}

static gboolean
action_to_xml (E2kAction *act, xmlNode *actionset)
{
	xmlNode *part, *value;
	gchar *entryid;

	switch (act->type) {
	case E2K_ACTION_MOVE:
	case E2K_ACTION_COPY:
		part = new_part (
			act->type == E2K_ACTION_MOVE ?
			(xmlChar *) "move-to-folder" :
			(xmlChar *) "copy-to-folder");
		value = new_value (
			part,
			(xmlChar *) "folder",
			(xmlChar *) "folder-source-key", NULL);
		entryid = stringify_entryid (
			act->act.xfer.folder_source_key->data + 1,
			act->act.xfer.folder_source_key->len - 1);
		xmlNewTextChild (
			value, NULL,
			(xmlChar *) "entryid",
			(xmlChar *) entryid);
		g_free (entryid);
		break;

	case E2K_ACTION_REPLY:
	case E2K_ACTION_OOF_REPLY:
		part = new_part (
			act->type == E2K_ACTION_REPLY ?
			(xmlChar *) "reply" :
			(xmlChar *) "oof-reply");
		value = new_value (
			part,
			(xmlChar *) "template",
			(xmlChar *) "message-entryid", NULL);
		entryid = stringify_entryid (
			act->act.reply.entryid->data,
			act->act.reply.entryid->len);
		xmlNewTextChild (
			value, NULL,
			(xmlChar *) "entryid",
			(xmlChar *) entryid);
		g_free (entryid);
		break;

	case E2K_ACTION_DEFER:
		part = new_part ((xmlChar *) "defer");
		break;

	case E2K_ACTION_BOUNCE:
		part = new_part ((xmlChar *) "bounce");
		switch (act->act.bounce_code) {
		case E2K_ACTION_BOUNCE_CODE_TOO_LARGE:
			new_value (
				part,
				(xmlChar *) "bounce_code",
				(xmlChar *) "option",
				(xmlChar *) "size");
			break;
		case E2K_ACTION_BOUNCE_CODE_FORM_MISMATCH:
			new_value (
				part,
				(xmlChar *) "bounce_code",
				(xmlChar *) "option",
				(xmlChar *) "form-mismatch");
			break;
		case E2K_ACTION_BOUNCE_CODE_ACCESS_DENIED:
			new_value (
				part,
				(xmlChar *) "bounce_code",
				(xmlChar *) "option",
				(xmlChar *) "permission");
			break;
		}
		break;

	case E2K_ACTION_FORWARD:
	case E2K_ACTION_DELEGATE:
	{
		gint i, j;
		E2kAddrList *list;
		E2kAddrEntry *entry;
		E2kPropValue *pv;
		const gchar *display_name, *email;
		gchar *full_addr;

		list = act->act.addr_list;
		for (i = 0; i < list->nentries; i++) {
			entry = &list->entry[i];
			display_name = email = NULL;
			for (j = 0; j < entry->nvalues; j++) {
				pv = &entry->propval[j];
				if (pv->prop.proptag == E2K_PROPTAG_PR_TRANSMITTABLE_DISPLAY_NAME)
					display_name = pv->value;
				else if (pv->prop.proptag == E2K_PROPTAG_PR_EMAIL_ADDRESS)
					email = pv->value;
			}

			if (!email)
				continue;
			if (display_name)
				full_addr = g_strdup_printf ("%s <%s>", display_name, email);
			else
				full_addr = g_strdup_printf ("<%s>", email);

			part = new_part (
				act->type == E2K_ACTION_FORWARD ?
				(xmlChar *) "forward" :
				(xmlChar *) "delegate");
			value = new_value (
				part,
				(xmlChar *) "recipient",
				(xmlChar *) "recipient", NULL);
			xmlNewTextChild (
				value, NULL,
				(xmlChar *) "recipient",
				(xmlChar *) full_addr);
			g_free (full_addr);

			xmlAddChild (actionset, part);
		}
		return TRUE;
	}

	case E2K_ACTION_TAG:
		if (act->act.proptag.prop.proptag != E2K_PROPTAG_PR_IMPORTANCE)
			return FALSE;

		part = new_part ((xmlChar *) "set-importance");
		new_value_int (
			part,
			(xmlChar *) "importance",
			(xmlChar *) "option",
			(xmlChar *) "value",
			GPOINTER_TO_INT (act->act.proptag.value));
		break;

	case E2K_ACTION_DELETE:
		part = new_part ((xmlChar *) "delete");
		break;

	case E2K_ACTION_MARK_AS_READ:
		part = new_part ((xmlChar *) "mark-read");
		break;

	default:
		return FALSE;
	}

	xmlAddChild (actionset, part);
	return TRUE;
}

static gboolean
rule_to_xml (E2kRule *rule, xmlNode *ruleset)
{
	xmlNode *top, *set;
	E2kRestriction *rn;
	gint i;

	top = xmlNewChild (ruleset, NULL, (xmlChar *) "rule", NULL);

	xmlSetProp (
		top,
		(xmlChar *) "source",
		(rule->state & E2K_RULE_STATE_ONLY_WHEN_OOF) ?
		(xmlChar *) "oof" : (xmlChar *) "incoming");
	xmlSetProp (
		top,
		(xmlChar *) "enabled",
		(rule->state & E2K_RULE_STATE_ENABLED) ?
		(xmlChar *) "1" : (xmlChar *) "0");

	if (rule->name)
		xmlNewTextChild (
			top, NULL,
			(xmlChar *) "title",
			(xmlChar *) rule->name);

	set = xmlNewChild (top, NULL, (xmlChar *) "partset", NULL);
	rn = rule->condition;
	if (rn) {
		E2kRestrictionType wrap_type;

		if (rn->type == E2K_RESTRICTION_OR) {
			xmlSetProp (
				top,
				(xmlChar *) "grouping",
				(xmlChar *) "any");
			wrap_type = E2K_RESTRICTION_OR;
		} else {
			xmlSetProp (
				top,
				(xmlChar *) "grouping",
				(xmlChar *) "all");
			wrap_type = E2K_RESTRICTION_AND;
		}

		if (!restriction_to_xml (rn, set, wrap_type, FALSE)) {
			g_warning ("could not express restriction as xml");
			xmlUnlinkNode (top);
			xmlFreeNode (top);
			return FALSE;
		}
	} else
		xmlSetProp (top, (xmlChar *) "grouping", (xmlChar *) "all");

	set = xmlNewChild (top, NULL, (xmlChar *) "actionset", NULL);
	for (i = 0; i < rule->actions->len; i++) {
		if (!action_to_xml (rule->actions->pdata[i], set)) {
			g_warning ("could not express action as xml");
			xmlUnlinkNode (top);
			xmlFreeNode (top);
			return FALSE;
		}
	}

	if (rule->state & E2K_RULE_STATE_EXIT_LEVEL)
		xmlAddChild (set, new_part ((xmlChar *) "stop"));

	return TRUE;
}

/**
 * e2k_rules_to_xml:
 * @rules: an #E2kRules
 *
 * Encodes @rules into an XML format like that used by the evolution
 * filter code.
 *
 * Return value: the XML rules
 **/
xmlDoc *
e2k_rules_to_xml (E2kRules *rules)
{
	xmlDoc *doc;
	xmlNode *top, *ruleset;
	gint i;

	doc = xmlNewDoc (NULL);
	top = xmlNewNode (NULL, (xmlChar *) "filteroptions");
	xmlDocSetRootElement (doc, top);

	ruleset = xmlNewChild (top, NULL, (xmlChar *) "ruleset", NULL);

	for (i = 0; i < rules->rules->len; i++)
		rule_to_xml (rules->rules->pdata[i], ruleset);

	return doc;
}
