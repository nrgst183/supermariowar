#include "path.h"

#include <SDL3/SDL.h>

#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <sys/stat.h>

namespace fs = std::filesystem;

std::string SMW_Root_Data_Dir;

std::string GetHomeDirectory()
{
#ifdef __APPLE__
    std::string result("/Library/Preferences/.smw/");
    char* folder = getenv("HOME");
    if (folder)
        result = std::string(folder) + result;
    return result;

#elif ANDROID
    const char* extstorage = getenv("EXTERNAL_STORAGE");
    std::string result(extstorage ? extstorage: "/mnt/sdcard");
    result += "/supermariowar/";
    return result;

#else // catch-all for Linux-based systems
    std::string result;
    if (char* sdl_path = SDL_GetPrefPath(nullptr, "supermariowar")) {
        result = sdl_path;
        SDL_free(sdl_path);
    }
    return result;
#endif
}

namespace {
// (whether files can be written in the data folder: tried once)
bool dataFolderWritable()
{
    static const bool writable = [] {
        const std::string probe = convertPath(".smw_write_test");
        FILE* file = fopen(probe.c_str(), "wb");
        if (!file)
            return false;
        fclose(file);
        std::remove(probe.c_str());
        return true;
    }();
    return writable;
}
} // namespace

std::string writablePath(const std::string& path)
{
    if (dataFolderWritable())
        return path;
    const std::string home = GetHomeDirectory();
    if (!home.empty() && path.rfind(home, 0) == 0)
        return path;    //(there already)
    //(the part in the data folder; a path elsewhere stays as it is)
    const std::string root = convertPath("");
    std::string relative;
    if (path.rfind(root, 0) == 0)
        relative = path.substr(root.size());
    else if (path.find(':') == std::string::npos && !path.starts_with('/'))
        relative = path;
    else
        return path;
    const std::string result = home + relative;
    std::error_code err;
    fs::create_directories(fs::path(result).parent_path(), err);
    return result;
}

std::string homeCopyDir(const std::string& relDir)
{
    if (dataFolderWritable())
        return {};
    return GetHomeDirectory() + relDir;
}

std::string mapThumbnailPath(const std::string& mapFile)
{
    //(in a folder of their own, named for the thumbnail images they're drawn from: the ones made
    //before those lost the pink left over from the color key, the tilesets' then the hazards', are
    //made again)
    constexpr const char* folder = "maps/cache/thumbnails3/";
    const std::string name = GetNameFromFileName(mapFile) + ".png";
    static const std::string dir = [folder] {
#ifdef SMW_XBOX
        //(on the cache drive: see SDL_XboxGetCachePath)
        std::string d = std::string(SDL_XboxGetCachePath()) + folder;
#else
        std::string d = convertPath(folder);
#endif
        std::error_code ec;
        fs::create_directories(d, ec);
        return d;
    }();
    return dir + name;
}

std::string GetRootDirectory()
{
#if !defined(_WIN32)
    // TODO: SDL_GetBasePath returns an UTF-8 string, which needs
    // some special treatment on Windows to work
    if (const char* sdl_path = SDL_GetBasePath()) {
        std::string result = sdl_path;
        return result;
    }
#endif

    return "./";
}

bool FileExists(const std::string& path)
{
    struct stat buffer {};
    int i = stat(path.c_str(), &buffer);
    return i == 0;
}

/*********************************************************************
  Mac OS X Application Bundles                              *********/

#ifdef __APPLE__
#include <iostream>
#include <CoreFoundation/CoreFoundation.h>

/* Call this when your application launches */
void Initialize_Paths()
{
    if (!SMW_Root_Data_Dir.empty())
        return;

#ifdef APPBUNDLE
    UInt8 temp[PATH_MAX];
    CFBundleRef mainBundle;
    CFURLRef dirURL;
    mainBundle = CFBundleGetMainBundle();
    if (!mainBundle) {
        printf("Not running from an .app bundle\n");
        return;
    }

    dirURL = CFBundleCopyBundleURL(mainBundle);

    if (!CFURLGetFileSystemRepresentation(dirURL, TRUE, temp,
        PATH_MAX)) {
        printf("Could not get file system representation\n");
        return;
    }

    SMW_Root_Data_Dir = getFileFromPath(std::string(reinterpret_cast<char*>(temp)));

    CFRelease(dirURL);
#else
    SMW_Root_Data_Dir = "./";
#endif
    printf("Located data folder at: %s\n", SMW_Root_Data_Dir.c_str());
}
#endif

/*********************************************************************
  All filenames should go through this                      *********/

std::string convertPath(const std::string& source)
{
    static bool are_paths_initialized = false;

    if (!are_paths_initialized) {
//		#ifdef PREFIXPATH
            SMW_Root_Data_Dir = RootDataDirectory;
//		#endif

        #ifdef __APPLE__
            Initialize_Paths();
        #endif

        size_t last_slash_pos = SMW_Root_Data_Dir.find_last_of("/");
        if (last_slash_pos != SMW_Root_Data_Dir.length() - 1)
            SMW_Root_Data_Dir += "/";

        are_paths_initialized = true;
    }

    return SMW_Root_Data_Dir + source;
}

std::string convertPath(std::string_view relpath, const std::filesystem::path& packdir)
{
    constexpr std::array<std::string_view, 2> prefixes {
        "gfx/packs/",
        "sfx/packs/",
    };
    for (std::string_view prefix : prefixes) {
        if (!relpath.starts_with(prefix))
            continue;

        relpath.remove_prefix(prefix.length());

        //If the file exists, return the path to it
        fs::path path = packdir / relpath;
        if (fs::exists(path))
            return path.generic_string();

        //If not, use the classic file
        return convertPath(std::string(prefix) + "Classic/" + std::string(relpath));  // FIXME
    }
    return convertPath(std::string(relpath));
}

// Takes a path to a file and gives you back the file name (with or without author) as a char *
std::string GetNameFromFileName(const std::string& path, bool stripAuthor)
{
    constexpr char PATH_SEPARATOR = '/';

    size_t left_pos = 0;
    size_t right_pos = path.length();

    const size_t sep_pos = path.find_last_of(PATH_SEPARATOR);
    if (sep_pos != std::string::npos)
        left_pos = sep_pos + 1;

    if (stripAuthor) {
        const size_t underscore_pos = path.find_first_of('_', left_pos);
        if (underscore_pos != std::string::npos)
            left_pos = underscore_pos + 1;
    }

    const size_t dot_pos = path.find_last_of('.', right_pos);
    if (dot_pos != std::string::npos && left_pos < dot_pos)
        right_pos = dot_pos;

    assert(right_pos >= left_pos);
    return path.substr(left_pos, right_pos - left_pos);
}

//Takes a file name and gives you back just the name of the file with no author or file extention
//and the first letter of the name will come back capitalized
std::string stripCreatorAndExt(const std::string& filename)
{
    size_t firstUnderscore = filename.find("_");    //find first _
    if (firstUnderscore == std::string::npos)   //if not found start with first character
        firstUnderscore = 0;
    else
        firstUnderscore++;                      //we don't want the _

    std::string withoutPrefix = filename.substr(firstUnderscore);   //substring without bla_ and .map (length-4)
    withoutPrefix = withoutPrefix.substr(0, withoutPrefix.length() - 4);        //i have no idea why this doesn't work if i do it like this: (leaves .map if the map starts with an underscore)

    //Capitalize the first letter so the hash table sorting works correctly
    if (!withoutPrefix.empty())
        withoutPrefix[0] = static_cast<char>(::toupper(withoutPrefix[0]));

    return withoutPrefix;
}

//Takes a path to a file and gives you back just the name of the file with no author or file extention
std::string stripPathAndExtension(const std::string& path)
{
    size_t chopHere = path.find("_");   //find first _
    if (chopHere == std::string::npos) {    //if not found, then find the beginning of the filename
        chopHere = path.find_last_of(dirSeparator());  //find last /
        if (chopHere == std::string::npos)  //if not found, start with first character
            chopHere = 0;
        else
            chopHere++;                     //we don't want the /
    } else {
        chopHere++;                     //we don't want the _
    }

    std::string withoutPath = path.substr(chopHere);    //substring without bla_
    withoutPath = withoutPath.substr(0, withoutPath.length() - 4); //and without extension like .map (length-4)

    return withoutPath;
}
