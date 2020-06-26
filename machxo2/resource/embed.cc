#include <cstdio>
#include <windows.h>
#include "nextpnr.h"
#include "resource.h"

NEXTPNR_NAMESPACE_BEGIN

const char *chipdb_blob_1200;

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
    chipdb_blob_1200 = LoadFileInResource(IDR_CHIPDB_1200, BINARYFILE, size);
}

NEXTPNR_NAMESPACE_END
