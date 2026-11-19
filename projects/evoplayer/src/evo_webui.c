/* System browser dialog used by the YouTube cast receiver. */
#if defined(EVO_APP_MODULE)

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "evo_webui.h"
#include "evo_boot_trace.h"

/* ---- libSceWebBrowserDialog, laid out after SharpProspero
 *      Interop/Dialog/WebBrowserDialog.cs (sizes 48 / 328 / 256). ---- */

#define SCE_SYSMODULE_WEB_BROWSER_DIALOG 0x00AB
#define WBD_MAGIC 0xC0D1A109u

enum { CDLG_STATUS_NONE = 0, CDLG_STATUS_INITIALIZED = 1,
       CDLG_STATUS_RUNNING = 2, CDLG_STATUS_FINISHED = 3 };

typedef struct {
    uint64_t size;
    uint8_t  reserved[36];
    uint32_t magic;               /* WBD_MAGIC + &this block */
} evo_cdlg_base_t;

typedef struct {
    evo_cdlg_base_t base;         /* 0   */
    uint64_t size;                /* 48  */
    int32_t  mode;                /* 56  1 = default, 2 = custom rect */
    int32_t  user_id;             /* 60  */
    const char *url;              /* 64  */
    void    *callback_init;       /* 72  layout unknown - left NULL */
    uint16_t width, height;       /* 80  custom mode only */
    uint16_t pos_x, pos_y;        /* 84  */
    uint32_t parts;               /* 88  */
    uint16_t header_width;        /* 92  */
    uint16_t header_x, header_y;  /* 94  */
    uint16_t pad0;                /* 98  */
    uint32_t control;             /* 100 */
    void    *ime_param;           /* 104 */
    void    *webview_param;       /* 112 */
    uint32_t animation;           /* 120 */
    uint8_t  reserved[202];       /* 124 */
    uint16_t tail_pad;            /* 326 */
} evo_wbd_param_t;

typedef struct {
    int32_t  result;
    int32_t  pad0;
    void    *callback_result;     /* 8 */
    uint8_t  reserved[240];
} evo_wbd_result_t;

_Static_assert(sizeof(evo_cdlg_base_t) == 48, "CommonDialogBaseParam");
_Static_assert(__builtin_offsetof(evo_wbd_param_t, callback_init) == 72, "CallbackInitParam @72");
_Static_assert(__builtin_offsetof(evo_wbd_param_t, control) == 100, "Control @100");
_Static_assert(__builtin_offsetof(evo_wbd_param_t, animation) == 120, "Animation @120");
_Static_assert(sizeof(evo_wbd_param_t) == 328, "WebBrowserDialogParam");
_Static_assert(sizeof(evo_wbd_result_t) == 256, "WebBrowserDialogResult");

extern int sceWebBrowserDialogInitialize(void);
extern int sceWebBrowserDialogOpen(evo_wbd_param_t *param);
extern int sceWebBrowserDialogUpdateStatus(void);
extern int sceWebBrowserDialogGetResult(evo_wbd_result_t *result);
extern int sceWebBrowserDialogClose(void);
extern int sceWebBrowserDialogTerminate(void);

extern int sceSysmoduleLoadModule(uint16_t id);
extern int sceCommonDialogInitialize(void);
extern int sceUserServiceGetInitialUser(int *user_id);
extern int sceUserServiceGetLoginUserIdList(int *user_ids);

enum DialogState { CLOSED, OPEN_PENDING, OPEN };
static enum DialogState s_state = CLOSED;
static int s_preload_rc = -1;
static int s_init_done;
static int s_wait_frames;
static int s_close_requested;
static evo_wbd_param_t s_param __attribute__((aligned(16)));
static char s_url[2048];

#define LOG(...) do { evo_bt("web: " __VA_ARGS__); evo_boot_log_flush(); } while (0)

static int pick_user(void)
{
    int uid = -1;
    int rc = sceUserServiceGetInitialUser(&uid);
    if (rc == 0 && uid != -1 && uid != 0xFF)
        return uid;
    LOG("sceUserServiceGetInitialUser -> 0x%08x uid=0x%08x", (unsigned)rc, (unsigned)uid);
    int list[4] = { -1, -1, -1, -1 };
    rc = sceUserServiceGetLoginUserIdList(list);
    LOG("sceUserServiceGetLoginUserIdList -> 0x%08x [0x%08x ...]", (unsigned)rc, (unsigned)list[0]);
    return list[0];
}

static int ensure_initialized(void)
{
    if (s_init_done) return 0;
    int rc = sceWebBrowserDialogInitialize();
    LOG("sceWebBrowserDialogInitialize (post-unjail) -> 0x%08x", (unsigned)rc);
    if (rc == 0) s_init_done = 1;
    return rc;
}

void evo_webui_preload(void)
{
    int rc = sceCommonDialogInitialize();
    evo_bt("web: sceCommonDialogInitialize -> 0x%08x (0x80b80002 = already up, fine)", (unsigned)rc);
    s_preload_rc = sceSysmoduleLoadModule(SCE_SYSMODULE_WEB_BROWSER_DIALOG);
    evo_bt("web: sceSysmoduleLoadModule(0xAB) -> 0x%08x", (unsigned)s_preload_rc);
    if (s_preload_rc < 0) return;
    rc = sceWebBrowserDialogInitialize();
    evo_bt("web: sceWebBrowserDialogInitialize (pre-unjail) -> 0x%08x", (unsigned)rc);
    if (rc == 0) s_init_done = 1;
}

int evo_webui_open_direct(const char *url)
{
    if (!url || !url[0]) return -1;
    if (s_state != CLOSED) return 1;
    if (s_preload_rc < 0) return -2;
    snprintf(s_url, sizeof s_url, "%s", url);
    s_close_requested = 0;
    s_wait_frames = 6;  /* Let the app draw before opening the dialog. */
    s_state = OPEN_PENDING;
    return 0;
}

static void open_dialog(void)
{
    if (ensure_initialized() != 0) {
        s_state = CLOSED;
        return;
    }

    memset(&s_param, 0, sizeof s_param);
    s_param.base.size = sizeof s_param.base;
    s_param.base.magic = (uint32_t)(WBD_MAGIC + (uintptr_t)&s_param.base);
    s_param.size = sizeof s_param;
    s_param.mode = 2;  /* Custom rectangle beside the navigation rail. */
    s_param.user_id = pick_user();
    s_param.url = s_url;
    s_param.pos_x = s_param.header_x = 108;
    s_param.width = s_param.header_width = 1812;
    s_param.height = 1080;
    s_param.parts = 0x7;  /* Native browser chrome, including Close. */

    int rc = sceWebBrowserDialogOpen(&s_param);
    LOG("sceWebBrowserDialogOpen -> 0x%08x", (unsigned)rc);
    s_state = rc == 0 ? OPEN : CLOSED;
}

int evo_webui_active(void)
{
    return s_state == OPEN;
}

int evo_webui_session_active(void)
{
    return s_state != CLOSED;
}

void evo_webui_close(void)
{
    if (s_state == OPEN_PENDING)
        s_state = CLOSED;
    else if (s_state == OPEN)
        s_close_requested = 1;
}

void evo_webui_pump(void)
{
    if (s_state == OPEN_PENDING) {
        if (--s_wait_frames == 0) open_dialog();
        return;
    }
    if (s_state != OPEN) return;

    int status = sceWebBrowserDialogUpdateStatus();
    if (status == CDLG_STATUS_RUNNING) {
        if (s_close_requested) {
            int rc = sceWebBrowserDialogClose();
            if (rc == 0) s_close_requested = 0;
        }
        return;
    }
    if (status == CDLG_STATUS_FINISHED) {
        evo_wbd_result_t result = {0};
        int rc = sceWebBrowserDialogGetResult(&result);
        LOG("GetResult -> 0x%08x result=0x%08x", (unsigned)rc, (unsigned)result.result);
        sceWebBrowserDialogClose();
    }
    s_state = CLOSED;
}

void evo_webui_shutdown(void)
{
    if (s_init_done) {
        sceWebBrowserDialogTerminate();
        s_init_done = 0;
    }
    s_state = CLOSED;
}

#else
typedef int evo_webui_not_built;
#endif
