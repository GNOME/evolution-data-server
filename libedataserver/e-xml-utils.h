/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* 
 * Copyright (C) 2005 Novell, Inc.
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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifndef __E_XML_UTILS_H__
#define __E_XML_UTILS_H__

#include <glib.h>

#include <libxml/parser.h>

G_BEGIN_DECLS

xmlDocPtr   e_xml_parse_file (const char *filename);
int         e_xml_save_file  (const char *filename,
			      xmlDocPtr   doc);
xmlNode    *e_xml_get_child_by_name (const xmlNode *parent,
				     const xmlChar *child_name);

G_END_DECLS

#endif /* __E_XML_UTILS_H__ */

