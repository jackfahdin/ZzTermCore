#pragma once

/**
 * @file ZzPtyExport.h
 * @brief ZzTermPty 动态库导出宏定义。
 *
 * 与 include/ZzTerm/Export.h 同一约定：
 * - 静态构建（BUILD_SHARED_LIBS=OFF）定义 ZZTERM_PTY_STATIC，宏为空；
 * - 动态构建编译库本体定义 ZZTERM_PTY_BUILDING_LIBRARY（导出），使用者不定义（导入）。
 */

#if defined(ZZTERM_PTY_STATIC)
#  define ZZTERM_PTY_API
#elif defined(_WIN32) || defined(__CYGWIN__)
#  if defined(ZZTERM_PTY_BUILDING_LIBRARY)
#    define ZZTERM_PTY_API __declspec(dllexport)
#  else
#    define ZZTERM_PTY_API __declspec(dllimport)
#  endif
#else
#  if defined(ZZTERM_PTY_BUILDING_LIBRARY)
#    define ZZTERM_PTY_API __attribute__((visibility("default")))
#  else
#    define ZZTERM_PTY_API
#  endif
#endif
