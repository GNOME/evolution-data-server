/*
 *  Copyright (C) 2000 Ximian Inc.
 *
 *  Authors: Michael Zucchi <notzed@ximian.com>
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

#include "camel-address.h"


static void camel_address_class_init (CamelAddressClass *klass);
static void camel_address_init       (CamelAddress *obj);
static void camel_address_finalize   (CamelObject *obj);

static CamelObjectClass *camel_address_parent;

static void
camel_address_class_init (CamelAddressClass *klass)
{
	camel_address_parent = camel_type_get_global_classfuncs (camel_object_get_type ());
}

static void
camel_address_init (CamelAddress *obj)
{
	obj->addresses = g_ptr_array_new();
}

static void
camel_address_finalize (CamelObject *obj)
{
	camel_address_remove((CamelAddress *)obj, -1);
	g_ptr_array_free(((CamelAddress *)obj)->addresses, TRUE);
}

CamelType
camel_address_get_type (void)
{
	static CamelType type = CAMEL_INVALID_TYPE;
	
	if (type == CAMEL_INVALID_TYPE) {
		type = camel_type_register (camel_object_get_type (), "CamelAddress",
					    sizeof (CamelAddress),
					    sizeof (CamelAddressClass),
					    (CamelObjectClassInitFunc) camel_address_class_init,
					    NULL,
					    (CamelObjectInitFunc) camel_address_init,
					    (CamelObjectFinalizeFunc) camel_address_finalize);
	}
	
	return type;
}

/**
 * camel_address_new:
 *
 * Create a new #CamelAddress object.
 * 
 * Returns a new #CamelAddress object
 **/
CamelAddress *
camel_address_new (void)
{
	CamelAddress *new = CAMEL_ADDRESS(camel_object_new(camel_address_get_type()));
	return new;
}


/**
 * camel_address_new_clone:
 * @addr: a #CamelAddress object
 * 
 * Clone an existing address type.
 * 
 * Returns the cloned address
 **/
CamelAddress *
camel_address_new_clone (const CamelAddress *addr)
{
	CamelAddress *new = CAMEL_ADDRESS(camel_object_new(CAMEL_OBJECT_GET_TYPE(addr)));

	camel_address_cat(new, addr);
	return new;
}


/**
 * camel_address_length:
 * @addr: a #CamelAddress object
 * 
 * Get the number of addresses stored in the address @addr.
 * 
 * Returns the number of addresses contained in @addr
 **/
int
camel_address_length (CamelAddress *addr)
{
	return addr->addresses->len;
}


/**
 * camel_address_decode:
 * @addr: a #CamelAddress object
 * @raw: raw address description
 *
 * Construct a new address from a raw address field.
 *
 * Returns the number of addresses parsed or %-1 on fail
 **/
int
camel_address_decode (CamelAddress *addr, const char *raw)
{
	g_return_val_if_fail(CAMEL_IS_ADDRESS(addr), -1);

	return CAMEL_ADDRESS_CLASS (CAMEL_OBJECT_GET_CLASS (addr))->decode(addr, raw);
}


/**
 * camel_address_encode:
 * @addr: a #CamelAddress object
 * 
 * Encode an address in a format suitable for a raw header.
 * 
 * Returns the encoded address
 **/
char *
camel_address_encode (CamelAddress *addr)
{
	g_return_val_if_fail(CAMEL_IS_ADDRESS(addr), NULL);

	return CAMEL_ADDRESS_CLASS (CAMEL_OBJECT_GET_CLASS (addr))->encode(addr);
}


/**
 * camel_address_unformat:
 * @addr: a #CamelAddress object
 * @raw: raw address description
 * 
 * Attempt to convert a previously formatted and/or edited
 * address back into internal form.
 * 
 * Returns the number of addresses parsed or %-1 on fail
 **/
int
camel_address_unformat(CamelAddress *addr, const char *raw)
{
	g_return_val_if_fail(CAMEL_IS_ADDRESS(addr), -1);

	return CAMEL_ADDRESS_CLASS (CAMEL_OBJECT_GET_CLASS (addr))->unformat(addr, raw);
}


/**
 * camel_address_format:
 * @addr: a #CamelAddress object
 * 
 * Format an address in a format suitable for display.
 * 
 * Returns a newly allocated string containing the formatted addresses
 **/
char *
camel_address_format (CamelAddress *addr)
{
	g_return_val_if_fail(CAMEL_IS_ADDRESS(addr), NULL);

	return CAMEL_ADDRESS_CLASS (CAMEL_OBJECT_GET_CLASS (addr))->format(addr);
}


/**
 * camel_address_cat:
 * @dest: destination #CamelAddress object
 * @source: source #CamelAddress object
 * 
 * Concatenate one address onto another. The addresses must
 * be of the same type.
 * 
 * Returns the number of addresses concatenated
 **/
int
camel_address_cat (CamelAddress *dest, const CamelAddress *source)
{
	g_return_val_if_fail(CAMEL_IS_ADDRESS(dest), -1);
	g_return_val_if_fail(CAMEL_IS_ADDRESS(source), -1);

	return CAMEL_ADDRESS_CLASS(CAMEL_OBJECT_GET_CLASS(dest))->cat(dest, source);
}


/**
 * camel_address_copy:
 * @dest: destination #CamelAddress object
 * @source: source #CamelAddress object
 * 
 * Copy the contents of one address into another.
 * 
 * Returns the number of addresses copied
 **/
int
camel_address_copy (CamelAddress *dest, const CamelAddress *source)
{
	g_return_val_if_fail(CAMEL_IS_ADDRESS(dest), -1);
	g_return_val_if_fail(CAMEL_IS_ADDRESS(source), -1);

	camel_address_remove(dest, -1);
	return camel_address_cat(dest, source);
}


/**
 * camel_address_remove:
 * @addr: a #CamelAddress object
 * @index: The address to remove, use %-1 to remove all address.
 * 
 * Remove an address by index, or all addresses.
 **/
void
camel_address_remove (CamelAddress *addr, int index)
{
	g_return_if_fail(CAMEL_IS_ADDRESS(addr));

	if (index == -1) {
		for (index = addr->addresses->len; index>-1; index--)
			CAMEL_ADDRESS_CLASS (CAMEL_OBJECT_GET_CLASS (addr))->remove(addr, index);
	} else {
		CAMEL_ADDRESS_CLASS (CAMEL_OBJECT_GET_CLASS (addr))->remove(addr, index);
	}
}
