/* app_desc.c: the application descriptor at U3_APP_BASE + 0x100 (see ld/app.ld, src/shared/u3_shared.h).
 * imageSize and crc32 are patched into the .bin by tools/post_app.py; the boot manager checks both. */
#include "../shared/u3_shared.h"
#include "version.h"

__attribute__((section(".app_desc"), used))
const u3_app_desc_t g_app_desc = {
    U3_APP_DESC_MAGIC,
    U3_FW_VERSION,
    0,
    0xFFFFFFFFu,    /* imageSize: post_app.py */
    0xFFFFFFFFu,    /* crc32: post_app.py */
    {0, 0, 0, 0},
};
