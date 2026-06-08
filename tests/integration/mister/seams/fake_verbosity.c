/* Link seam: RetroArch logging. With HAVE_LOGGER off, verbosity.h declares
 * RARCH_* as plain extern functions; gfx_mister.c calls RARCH_LOG. These are
 * no-ops for the test (we assert on streamed pixels, not log output). */
#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>

void RARCH_LOG(const char *fmt, ...)        { (void)fmt; }
void RARCH_ERR(const char *fmt, ...)        { (void)fmt; }
void RARCH_WARN(const char *fmt, ...)       { (void)fmt; }
void RARCH_DBG(const char *fmt, ...)        { (void)fmt; }
void RARCH_LOG_OUTPUT(const char *msg, ...) { (void)msg; }
void RARCH_LOG_V(const char *tag, const char *fmt, va_list ap) { (void)tag; (void)fmt; (void)ap; }
void RARCH_LOG_BUFFER(uint8_t *buffer, size_t len) { (void)buffer; (void)len; }
