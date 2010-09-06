/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*-

   e-debug-log.h: Ring buffer for logging debug messages
 
   Copyright (C) 2006, 2007 Novell, Inc.
  
   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.
  
   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.
  
   You should have received a copy of the GNU General Public
   License along with this program; if not, write to the
   Free Software Foundation, Inc., 59 Temple Place - Suite 330,
   Boston, MA 02111-1307, USA.
  
   Author: Federico Mena-Quintero <federico@novell.com>
*/

#ifndef E_DEBUG_LOG_H
#define E_DEBUG_LOG_H

#include <glib.h>

#define E_DEBUG_LOG_DOMAIN_USER		"USER"		/* always enabled */
#define E_DEBUG_LOG_DOMAIN_GLOG		"GLog"		/* used for GLog messages; don't use it yourself */
#define E_DEBUG_LOG_DOMAIN_CAL_QUERIES  "CalQueries"    /* used for calendar queries analysis */

void e_debug_log (gboolean is_milestone, const char *domain, const char *format, ...);

void e_debug_logv (gboolean is_milestone, const char *domain, const char *format, va_list args);

gboolean e_debug_log_load_configuration (const char *filename, GError **error);

void e_debug_log_enable_domains (const char **domains, int n_domains);
void e_debug_log_disable_domains (const char **domains, int n_domains);

gboolean e_debug_log_is_domain_enabled (const char *domain);

gboolean e_debug_log_dump (const char *filename, GError **error);

gboolean e_debug_log_dump_to_dated_file (GError **error);

void e_debug_log_set_max_lines (int num_lines);
int e_debug_log_get_max_lines (void);

/* For testing only */
void e_debug_log_clear (void);

#endif /* E_DEBUG_LOG_H */

