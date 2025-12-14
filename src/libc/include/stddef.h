/*
 * stddef.h - Standard Definitions
 */

#ifndef _STDDEF_H
#define _STDDEF_H

/* Size type */
typedef unsigned long size_t;

/* Pointer difference type */
typedef long ptrdiff_t;

/* Null pointer */
#ifndef NULL
#define NULL ((void *)0)
#endif

/* Offset of member in struct */
#define offsetof(type, member) __builtin_offsetof(type, member)

#endif /* _STDDEF_H */
