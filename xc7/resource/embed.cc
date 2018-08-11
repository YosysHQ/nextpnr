#include <cstdio>
#include <windows.h>
#include "nextpnr.h"
#include "resource.h"

NEXTPNR_NAMESPACE_BEGIN

const char *chipdb_blob_384;
const char *chipdb_blob_1k;
const char *chipdb_blob_5k;
const char *chipdb_blob_8k;

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
    chipdb_blob_384 = LoadFileInResource(IDR_CHIPDB_384, BINARYFILE, size);
    chipdb_blob_1k = LoadFileInResource(IDR_CHIPDB_1K, BINARYFILE, size);
    chipdb_blob_5k = LoadFileInResource(IDR_CHIPDB_5K, BINARYFILE, size);
    chipdb_blob_8k = LoadFileInResource(IDR_CHIPDB_8K, BINARYFILE, size);
}

NEXTPNR_NAMESPACE_END