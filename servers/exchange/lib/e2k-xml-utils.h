/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* Copyright (C) 2001-2004 Novell, Inc. */

#ifndef __E2K_XML_UTILS_H__
#define __E2K_XML_UTILS_H__

#include <string.h>

#include "e2k-types.h"
#include <libxml/parser.h>

#define E2K_XML_HEADER "<?xml version=\"1.0\" encoding=\"utf-8\" ?>"

xmlDoc *e2k_parse_xml  (const char *buf, int len);
xmlDoc *e2k_parse_html (const char *buf, int len);

#define E2K_IS_NODE(node, nspace, nname) (!strcmp (node->name, nname) && node->ns && !strcmp (node->ns->href, nspace))

void  e2k_g_string_append_xml_escaped (GString *string, const char *value);

xmlNode *e2k_xml_find    (xmlNode *node, const char *name);
xmlNode *e2k_xml_find_in (xmlNode *node, xmlNode *top, const char *name);

#endif
