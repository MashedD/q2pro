#include <FidelityFX/host/ffx_message.h>

static ffxMessageCallback q2_fsr3_message_callback;
static uint32_t q2_fsr3_message_level;

void ffxSetPrintMessageCallback(ffxMessageCallback callback, uint32_t debugLevel)
{
    q2_fsr3_message_callback = callback;
    q2_fsr3_message_level = debugLevel;
}

void ffxPrintMessage(uint32_t type, const wchar_t *message)
{
    (void)q2_fsr3_message_level;
    if (q2_fsr3_message_callback)
        q2_fsr3_message_callback(type, message);
}
