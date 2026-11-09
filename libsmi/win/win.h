/*
 * win.h --
 *
 *      Some helper functions to make libsmi compile with vc++ for win32.
 *
 * Copyright (c) 2000 E. Schoenfelder, Gaertner Datensysteme Braunschweig.
 * Copyright (c) 2000 J. Schoenwaelder, Technical University of Braunschweig.
 *
 * See the file "COPYING" for information on usage and redistribution
 * of this file, and for a DISCLAIMER OF ALL WARRANTIES.
 *
 * @(#) $Id: win.h 1657 2009-12-28 19:43:45Z schoenw $
 */

#ifndef _WIN_H
#define _WIN_H

#include <ctype.h>
#include <limits.h>
#include <io.h>

/*
 * The access() function exists in the Win32 API, but there are no
 * defines for the mode parameter. So we provided them here.
 */

#ifndef F_OK
#define F_OK	00
#endif
#ifndef W_OK
#define W_OK	02
#endif
#ifndef R_OK
#define R_OK	04
#endif

/*
 * Other function prototypes...
 */

#if ! defined(__GNUC__) && defined(__STDC__)
int __cdecl fileno(FILE *); 
#endif

/*
 * isascii() is a non __STDC__ extension needed when __STDC__ is defined in
 * Win32 environment.
 */

#if defined(__STDC__)
#ifndef isascii
#define isascii(_c)   ( (unsigned)(_c) < 0x80 )
#endif
#endif

/*
 * Older Microsoft C runtimes lacked some C99 math/conversion
 * functions. Modern MSVC (VS 2015+) provides them natively.
 */
#if !defined(__GNUC__) && (!defined(_MSC_VER) || _MSC_VER < 1900)
#define fabsf       fabs
#define strtold     strtod
#endif

/*
 * Some Windows compilers seem to lack strtof() so we fake it here.
 */

#if defined(_MSC_VER)

/*
 * Visual Studio 2015 (_MSC_VER 1900) and later provide native
 * C99 strtof() and vsnprintf().  Keep the compatibility aliases
 * only for older Microsoft compilers.
 */
#if _MSC_VER < 1900
#define strtof(f1,f2) ((float)strtod(f1,f2))
#define vsnprintf     _vsnprintf
#endif

/*
 * Windows compiler writers love to issue warnings for C functions
 * whose names were changed by C++ standards.  Since access is used as
 * the name of a structure member it has to be treated differently.
 */

#define access(f1,f2) _access(f1,f2)
#define putenv        _putenv
#define strdup        _strdup
#define strcasecmp    _stricmp

/*
 * More replacement definitions...
 */

#define isatty        _isatty
#define fileno        _fileno

#endif /* _MSC_VER */

#endif /* _WIN_H */
