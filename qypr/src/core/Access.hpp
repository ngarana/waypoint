// Access.hpp - Test-visibility access macros.
//
// The codebase writes `PRIVATE :` / `PROTECTED :` instead of the bare `private:`
// / `protected:` access specifiers so the unit-test build can reach internals
// without `friend` clutter. Under TESTING they open up to `public`; every other
// build keeps normal encapsulation. This header is force-included into every
// translation unit by CMake (add_compile_options(-include .../Access.hpp)), so
// the macros are defined everywhere without each file having to include it.
#pragma once

#ifdef TESTING
#    define PRIVATE public
#    define PROTECTED public
#else
#    define PRIVATE private
#    define PROTECTED protected
#endif
