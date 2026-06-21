#pragma once
// detex (https://github.com/hglm/detex) marks its inline helpers with
// __attribute__((always_inline)), a GCC/Clang extension MSVC can't parse. Neutralize
// the attribute on MSVC (non-Clang) so detex.h and the ETC2/EAC decoder sources
// compile; the `inline` keyword that follows is enough. This header is force-included
// ahead of the detex .c files (see CMakeLists) and pulled in before detex.h in image.cpp.
#if defined(_MSC_VER) && !defined(__clang__)
#define __attribute__(x)
#endif
