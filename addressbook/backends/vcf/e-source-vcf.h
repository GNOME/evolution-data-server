/*
 * e-source-vcf.h
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with the program; if not, see <http://www.gnu.org/licenses/>
 *
 */

#ifndef E_SOURCE_VCF_H
#define E_SOURCE_VCF_H

#include <libedataserver/libedataserver.h>

/* Standard GObject macros */
#define E_TYPE_SOURCE_VCF \
	(e_source_vcf_get_type ())
#define E_SOURCE_VCF(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_SOURCE_VCF, ESourceVCF))
#define E_SOURCE_VCF_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), E_TYPE_SOURCE_VCF, ESourceVCFClass))
#define E_IS_SOURCE_VCF(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_SOURCE_VCF))
#define E_IS_SOURCE_VCF_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), E_TYPE_SOURCE_VCF))
#define E_SOURCE_VCF_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), E_TYPE_SOURCE_VCF, ESourceVCFClass))

#define E_SOURCE_EXTENSION_VCF_BACKEND "VCF Backend"

G_BEGIN_DECLS

typedef struct _ESourceVCF ESourceVCF;
typedef struct _ESourceVCFClass ESourceVCFClass;
typedef struct _ESourceVCFPrivate ESourceVCFPrivate;

struct _ESourceVCF {
	ESourceExtension parent;
	ESourceVCFPrivate *priv;
};

struct _ESourceVCFClass {
	ESourceExtensionClass parent_class;
};

GType		e_source_vcf_get_type		(void);
void		e_source_vcf_type_register	(GTypeModule *type_module);
const gchar *	e_source_vcf_get_path		(ESourceVCF *extension);
gchar *		e_source_vcf_dup_path		(ESourceVCF *extension);
void		e_source_vcf_set_path		(ESourceVCF *extension,
						 const gchar *path);

G_END_DECLS

#endif /* E_SOURCE_VCF_H */
