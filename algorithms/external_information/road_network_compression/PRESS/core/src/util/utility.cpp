//
//  utility.cpp
//  press-v3
//
//  Created by Renchu Song on 11/21/20.
//  Copyright © 2020 Renc. All rights reserved.
//

#include "utility.hpp"

#include <cerrno>
#include <cstdio>
#include <dirent.h>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#else
#include <unistd.h>
#endif

namespace {

int createDirPortable(const char* path) {
#ifdef _WIN32
  return _mkdir(path);
#else
  return mkdir(path, 0777);
#endif
}

int removeDirPortable(const char* path) {
#ifdef _WIN32
  return _rmdir(path);
#else
  return rmdir(path);
#endif
}

bool clearDirectoryContents(const std::string& dirName) {
  DIR* dir = opendir(dirName.c_str());
  if (dir == NULL) {
    return false;
  }

  struct dirent* ent;
  while ((ent = readdir(dir)) != NULL) {
    if (ent->d_name[0] == '.') {
      continue;
    }

    std::string childPath = dirName + ent->d_name;
    struct stat statBuf;
    if (stat(childPath.c_str(), &statBuf) != 0) {
      closedir(dir);
      return false;
    }

    if ((statBuf.st_mode & S_IFDIR) != 0) {
      if (!clearDirectoryContents(childPath + "/")) {
        closedir(dir);
        return false;
      }
      if (removeDirPortable(childPath.c_str()) != 0) {
        closedir(dir);
        return false;
      }
    } else if (std::remove(childPath.c_str()) != 0) {
      closedir(dir);
      return false;
    }
  }

  closedir(dir);
  return true;
}

}

std::string readFile(const char* path) {
  std::ifstream input(path);
  std::stringstream sstr;
  while(input >> sstr.rdbuf());
  return sstr.str();
}

bool fileExists(const char* name) {
  struct stat buffer;
  return (stat (name, &buffer) == 0);
}

bool createFolder(const std::string& dirName) {
  if (createDirPortable(dirName.c_str()) == 0) {
    return true;
  }
  return errno == EEXIST;
}

bool listDirectory(const std::string& dirName, std::vector<std::string>& fileNames) {
  DIR *dir;
  struct dirent *ent;
  if ((dir = opendir(dirName.c_str())) != NULL) {
    fileNames.clear();
    while ((ent = readdir(dir)) != NULL) {
      if (ent->d_name[0] == '.') {
        continue;
      }
      fileNames.push_back(ent->d_name);
    }
    closedir(dir);
    return true;
  } else {
    return false;
  }
}

bool clearDirectory(const std::string& dirName) {
  return clearDirectoryContents(dirName);
}

std::string vecStringToJSONString(const std::vector<std::string>& data) {
  std::string result("[");
  auto len = data.size();
  if (len > 0) {
    result += toJSONString(data.at(0));
  }
  for (auto i = 1; i < len; ++i) {
    result += "," + toJSONString(data.at(i));
  }
  result += "]";
  return result;
}

std::string toJSONString(const std::string& str) {
  // TODO: escape quotes and backslash in str.
  return "\"" + str + "\"";
}
