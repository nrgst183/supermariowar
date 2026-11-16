#pragma once

#include <filesystem>
#include <string>
#include <string_view>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

/* Call Initialize_Paths() when your application launches */

bool FileExists(const std::string& path);

/* All filenames must go through this door */
// TODO: Return fs::path
std::string convertPath(const std::string& source);
std::string convertPath(std::string_view relpath, const std::filesystem::path& packdir);

constexpr char dirSeparator() { return '/'; }

std::string GetHomeDirectory();
std::string GetRootDirectory();

#ifdef __APPLE__
    void Initialize_Paths();
#else
#  define Initialize_Paths();
#endif

std::string GetNameFromFileName(const std::string& path, bool stripAuthor = false);

// Where a map's thumbnail (for the map browser) is cached. On the Xbox it is
// on the cache drive (Z:), as the data on the disc can't be written; its
// folder is made when needed.
std::string mapThumbnailPath(const std::string& mapFile);
std::string stripCreatorAndExt(const std::string& filename);
std::string stripPathAndExtension(const std::string& path);

// Defined in global.cpp
extern std::string RootDataDirectory;
