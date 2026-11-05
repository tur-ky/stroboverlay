#ifndef VERSION_H
#define VERSION_H

// Basic metadata strings
#define VER_COMPANYNAME_STR      "stroboverlay"
#define VER_FILEDESCRIPTION_STR  "Stroboscopic overlay utility"
#define VER_INTERNALNAME_STR     "stroboverlay.exe"
#define VER_ORIGINALFILENAME_STR "stroboverlay.exe"
#define VER_PRODUCTNAME_STR      "stroboverlay"

// Semantic version
#define VER_MAJOR 0
#define VER_MINOR 9
#define VER_PATCH 3
#define VER_BUILD 0

// Numeric and string versions
#define VER_FILE_VERSION         VER_MAJOR,VER_MINOR,VER_PATCH,VER_BUILD
#define VER_FILE_VERSION_STR     "0.9.3.0"
#define VER_PRODUCT_VERSION      VER_MAJOR,VER_MINOR,VER_PATCH,VER_BUILD
#define VER_PRODUCT_VERSION_STR  "0.9.3.0"

// Unique build identifier for this change set
#define BUILD_ID_A "Build-0.9.3-revA"
#define BUILD_ID_W L"Build-0.9.3-revA"

#endif // VERSION_H
