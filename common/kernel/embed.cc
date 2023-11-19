#include <map>
#if defined(WIN32)
#define NOMINMAX
#include <windows.h>
#endif
#include <boost/filesystem.hpp>
#include <boost/iostreams/device/mapped_file.hpp>
#include "embed.h"
#include "nextpnr.h"

NEXTPNR_NAMESPACE_BEGIN

#if defined(EXTERNAL_CHIPDB_ROOT)

const void *get_chipdb(const std::string &filename)
{
    static std::map<std::string, boost::iostreams::mapped_file> files;
    if (!files.count(filename)) {
        std::string full_filename = EXTERNAL_CHIPDB_ROOT "/" + filename;
        if (boost::filesystem::exists(full_filename))
            files[filename].open(full_filename, boost::iostreams::mapped_file::priv);
    }
    if (files.count(filename))
        return files.at(filename).data();
    return nullptr;
}

#elif defined(WIN32)

const void *get_chipdb(const std::string &filename)
{
    HRSRC rc = ::FindResource(nullptr, filename.c_str(), RT_RCDATA);
    HGLOBAL rcData = ::LoadResource(nullptr, rc);
    return ::LockResource(rcData);
}

#else

EmbeddedFile *EmbeddedFile::head = nullptr;

const void *get_chipdb(const std::string &filename)
{
    for (EmbeddedFile *file = EmbeddedFile::head; file; file = file->next)
        if (file->filename == filename)
            return file->content;
    return nullptr;
}

#endif

NEXTPNR_NAMESPACE_END
