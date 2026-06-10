/* Enabled by -Wl,--wrap=scaler_ctx_scale: records the scaler's output dimensions. */
#include "spy_scaler.h"
#include <gfx/scaler/scaler.h>

void __real_scaler_ctx_scale(struct scaler_ctx *ctx, void *output, const void *input);

static int g_last_out_w;
static int g_last_out_h;

void __wrap_scaler_ctx_scale(struct scaler_ctx *ctx, void *output, const void *input)
{
   if (ctx)
   {
      g_last_out_w = ctx->out_width;
      g_last_out_h = ctx->out_height;
   }
   __real_scaler_ctx_scale(ctx, output, input);
}

int spy_scaler_last_out_width(void)  { return g_last_out_w; }
int spy_scaler_last_out_height(void) { return g_last_out_h; }
