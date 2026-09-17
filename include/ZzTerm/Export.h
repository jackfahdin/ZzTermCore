#pragma once

/**
 * @file Export.h
 * @brief ZzTermCore 动态库导出宏定义。
 *
 * 构建约定：
 * - 静态库构建（BUILD_SHARED_LIBS=OFF）时定义 ZZTERM_STATIC，宏为空；
 * - 动态库构建时，编译库本体定义 ZZTERM_BUILDING_LIBRARY（导出），
 *   使用者不定义（导入）。
 * 静态/动态两种构建下公开 API 语义完全一致。
 */

#if defined(ZZTERM_STATIC)
#  define ZZTERM_API
#elif defined(_WIN32) || defined(__CYGWIN__)
#  if defined(ZZTERM_BUILDING_LIBRARY)
#    define ZZTERM_API __declspec(dllexport)
#  else
#    define ZZTERM_API __declspec(dllimport)
#  endif
#else
#  if defined(ZZTERM_BUILDING_LIBRARY)
#    define ZZTERM_API __attribute__((visibility("default")))
#  else
#    define ZZTERM_API
#  endif
#endif
