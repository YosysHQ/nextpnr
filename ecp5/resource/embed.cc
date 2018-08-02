#include <cstdio>
#include <windows.h>
#include "nextpnr.h"
#include "resource.h"

NEXTPNR_NAMESPACE_BEGIN

const char *chipdb_blob_25k;
const char *chipdb_blob_45k;
const char *chipdb_blob_85k;

const char *LoadFileInResource(int name, int type, DWORD &size)
{
    HMODULE handle = ::GetModuleHandle(NULL);
    HRSRC rc = ::FindResource(handle, MAKEINTRESOURCE(name), MAKEINTRESOURCE(type));
    HGLOBAL rcData = ::LoadResource(handle, rc);
    size = ::SizeofResource(handle, rc);
    return static_cast<const char *>(::LockResource(rcData));
}
void load_chipdb()
{
    DWORD size = 0;
    chipdb_blob_25k = LoadFileInResource(IDR_CHIPDB_25K, BINARYFILE, size);
    chipdb_blob_45k = LoadFileInResource(IDR_CHIPDB_45K, BINARYFILE, size);
    chipdb_blob_85k = LoadFileInResource(IDR_CHIPDB_85K, BINARYFILE, size);
}

NEXTPNR_NAMESPACE_END