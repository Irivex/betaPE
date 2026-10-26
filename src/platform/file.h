#ifndef FILE_H__
#define FILE_H__

bool DeleteDirectory(const std::string&, bool noRecycleBin = true);

#ifdef WIN32
    #include <windows.h>
    #include <tchar.h>
    #include <shellapi.h>
    #include <string>

bool DeleteDirectory(const std::string& dir, bool noRecycleBin /*true*/)
{
    int len = strlen(dir.c_str());
    //TCHAR *pszFrom = new TCHAR[len+2];
	char* pszFrom = new char[len+2];
    strncpy(pszFrom, dir.c_str(), len);
    pszFrom[len] = 0;
    pszFrom[len+1] = 0;

    SHFILEOPSTRUCT fileop;
    fileop.hwnd   = NULL;    // no status display
    fileop.wFunc  = FO_DELETE;  // delete operation
    fileop.pFrom  = pszFrom;  // source file name as double null terminated string
    fileop.pTo    = NULL;    // no destination needed
    fileop.fFlags = FOF_NOCONFIRMATION|FOF_SILENT;  // do not prompt the user

    if(!noRecycleBin)
		fileop.fFlags |= FOF_ALLOWUNDO;

    fileop.fAnyOperationsAborted = FALSE;
    fileop.lpszProgressTitle     = NULL;
    fileop.hNameMappings         = NULL;

    int ret = SHFileOperation(&fileop);
    delete [] pszFrom;
    return (ret == 0);
}
#else
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

// Recursively delete everything inside a directory.
static bool deleteDirectoryContents(const std::string& folder)
{
    DIR* dir = opendir(folder.c_str());
    if (!dir)
        return false;

    bool success = true;
    struct dirent* entry;

    while ((entry = readdir(dir)) != NULL) {
        if (std::strcmp(entry->d_name, ".") == 0 ||
            std::strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        std::string childPath = folder;

        if (!childPath.empty() && childPath.back() != '/')
            childPath += '/';

        childPath += entry->d_name;

        struct stat info;

        // lstat checks the entry itself without following symlinks.
        if (lstat(childPath.c_str(), &info) != 0) {
            success = false;
            continue;
        }

        if (S_ISDIR(info.st_mode)) {
            // Empty the subdirectory first, then remove it.
            if (!deleteDirectoryContents(childPath))
                success = false;

            if (rmdir(childPath.c_str()) != 0)
                success = false;
        }
        else {
            // Delete regular files and symlinks.
            if (unlink(childPath.c_str()) != 0)
                success = false;
        }
    }

    if (closedir(dir) != 0)
        success = false;

    return success;
}

bool DeleteDirectory(const std::string& d, bool /*noRecycleBin*/)
{
    struct stat info;

    if (lstat(d.c_str(), &info) != 0)
        return false;

    // Handle a file or symlink passed to this function.
    if (!S_ISDIR(info.st_mode))
        return unlink(d.c_str()) == 0;

    // Delete all contents before removing the world folder itself.
    if (!deleteDirectoryContents(d))
        return false;

    return rmdir(d.c_str()) == 0;
}

#endif /*(ELSE) WIN32*/

#endif /*FILE_H__*/
