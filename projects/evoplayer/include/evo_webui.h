/* System browser dialog used by the YouTube cast receiver. */
#ifndef EVO_WEBUI_H
#define EVO_WEBUI_H

#ifdef __cplusplus
extern "C" {
#endif

#if defined(EVO_APP_MODULE)
void evo_webui_preload(void);
void evo_webui_pump(void);
int  evo_webui_open_direct(const char *url);
void evo_webui_close(void);
int  evo_webui_active(void);
int  evo_webui_session_active(void);
void evo_webui_shutdown(void);
#else
static inline void evo_webui_preload(void) {}
static inline void evo_webui_pump(void) {}
static inline int  evo_webui_open_direct(const char *url) { (void)url; return -1; }
static inline void evo_webui_close(void) {}
static inline int  evo_webui_active(void) { return 0; }
static inline int  evo_webui_session_active(void) { return 0; }
static inline void evo_webui_shutdown(void) {}
#endif

#ifdef __cplusplus
}
#endif

#endif /* EVO_WEBUI_H */
