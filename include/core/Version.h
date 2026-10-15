#ifndef __VERSION_H__
#define __VERSION_H__

/* ==============================================================================
 * Application Identity
 * ============================================================================== */
#define APP_NAME                    "LeLauncher"
#define APP_ID                      "com.lelauncher.app"
#define APP_TITLE                   "LeLauncher"
#define APP_DESCRIPTION             "Game Launcher and Content Delivery Client"
#define APP_AUTHOR                  "LeLauncher Team"
#define APP_COPYRIGHT               "Copyright (C) 2026 LeLauncher. All rights reserved."

/* ==============================================================================
 * Version Definition
 * ============================================================================== */
#define APP_VERSION_MAJOR           1
#define APP_VERSION_MINOR           0
#define APP_VERSION_PATCH           0
#define APP_VERSION_BUILD           1

#define APP_VERSION_STRING          "1.0.0"
#define APP_VERSION_STRING_FULL     "1.0.0.1"

/* ==============================================================================
 * Windows Application Resource (.rc) Definitions
 * ============================================================================== */
#define RC_FILEVERSION              APP_VERSION_MAJOR,APP_VERSION_MINOR,APP_VERSION_PATCH,APP_VERSION_BUILD
#define RC_FILEVERSION_STRING       APP_VERSION_STRING_FULL
#define RC_PRODUCTVERSION           APP_VERSION_MAJOR,APP_VERSION_MINOR,APP_VERSION_PATCH,APP_VERSION_BUILD
#define RC_PRODUCTVERSION_STRING    APP_VERSION_STRING

#define RC_COMPANY_NAME             "LeLauncher"
#define RC_FILEDESCRIPTION          APP_DESCRIPTION
#define RC_INTERNAL_NAME            APP_NAME
#define RC_ORIGINAL_FILENAME        "LeLauncher.exe"
#define RC_PRODUCT_NAME             APP_NAME
#define RC_LEGAL_COPYRIGHT          APP_COPYRIGHT

#endif // __VERSION_H__
