/*
 * SPDX-FileCopyrightText: (C) 2017 Red Hat, Inc. (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#if !defined (__CAMEL_H_INSIDE__) && !defined (CAMEL_COMPILATION)
#error "Only <camel/camel.h> can be included directly."
#endif

#ifndef CAMEL_WEAK_REF_GROUP_H
#define CAMEL_WEAK_REF_GROUP_H

#include <glib-object.h>

#define CAMEL_TYPE_WEAK_REF_GROUP (camel_weak_ref_group_get_type ())

G_BEGIN_DECLS

typedef struct _CamelWeakRefGroup CamelWeakRefGroup;

GType		camel_weak_ref_group_get_type	(void) G_GNUC_CONST;
CamelWeakRefGroup *
		camel_weak_ref_group_new	(void);
CamelWeakRefGroup *
		camel_weak_ref_group_ref	(CamelWeakRefGroup *group);
void		camel_weak_ref_group_unref	(CamelWeakRefGroup *group);

void		camel_weak_ref_group_set	(CamelWeakRefGroup *group,
						 gpointer object);
gpointer	camel_weak_ref_group_get	(CamelWeakRefGroup *group);

G_END_DECLS

#endif /* CAMEL_WEAK_REF_GROUP_H */
