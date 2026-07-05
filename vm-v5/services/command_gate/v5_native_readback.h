#ifndef V5_NATIVE_READBACK_H
#define V5_NATIVE_READBACK_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct V5NativeReadback {
    int rtcp_actual_available;
    int rtcp_enabled;
    int wcs_actual_available;
    int wcs_index;
    char unavailable_reason[96];
} V5NativeReadback;

void v5_native_readback_init(V5NativeReadback *readback);
void v5_native_readback_set_unavailable(V5NativeReadback *readback, const char *reason);
void v5_native_readback_set_rtcp_actual(V5NativeReadback *readback, int enabled);
void v5_native_readback_set_wcs_actual(V5NativeReadback *readback, int wcs_index);
int v5_native_readback_rtcp_known(const V5NativeReadback *readback);
int v5_native_readback_wcs_known(const V5NativeReadback *readback);

#ifdef __cplusplus
}
#endif

#endif
