#include "v5_native_sample.h"

#include <string.h>

void v5_native_display_sample_init(V5NativeDisplaySample *sample)
{
    if (!sample) {
        return;
    }

    memset(sample, 0, sizeof(*sample));
    memcpy(sample->runtime_modal_text, "UNAVAILABLE", sizeof("UNAVAILABLE"));
}

int v5_native_display_sample_read(V5NativeDisplaySample *sample)
{
    v5_native_display_sample_init(sample);
    return 0;
}
