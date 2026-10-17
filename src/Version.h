#pragma once

// Central version definition for the Kite project.
// Bump these when you cut a build; Kite.rc carries the same numbers.

#define KITE_VERSION_MAJOR       0
#define KITE_VERSION_MINOR       1
#define KITE_VERSION_PATCH       0
#define KITE_VERSION_BUILD       0

#define KITE_NAME                "Kite"
#define KITE_NAME_WSTR           L"Kite"

#define _KITE_STR_IMPL(x)        #x
#define _KITE_STR(x)             _KITE_STR_IMPL(x)
#define _KITE_WSTR_IMPL(x)       L##x
#define _KITE_WSTR(x)            _KITE_WSTR_IMPL(x)

// "0.1.0"
#define KITE_VERSION_STR         \
	_KITE_STR(KITE_VERSION_MAJOR) "." \
	_KITE_STR(KITE_VERSION_MINOR) "." \
	_KITE_STR(KITE_VERSION_PATCH)

#define KITE_VERSION_WSTR        _KITE_WSTR(KITE_VERSION_STR)

// "0.1.0.0" - keep in sync with FILEVERSION/PRODUCTVERSION in Kite.rc
#define KITE_VERSION_FULL_STR    KITE_VERSION_STR ".0"
#define KITE_VERSION_FULL_WSTR   _KITE_WSTR(KITE_VERSION_FULL_STR)
