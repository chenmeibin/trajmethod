#pragma once

#ifdef _WIN32
#include_next <direct.h>
#else
#include <sys/stat.h>

inline int _mkdir(const char* path) {
  return mkdir(path, 0777);
}
#endif
