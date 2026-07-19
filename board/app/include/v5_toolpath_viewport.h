#ifndef V5_TOOLPATH_VIEWPORT_H
#define V5_TOOLPATH_VIEWPORT_H

typedef struct V5ToolpathViewport {
    int x;
    int y;
    int width;
    int height;
    int gesture_left_inset;
    int gesture_right_inset;
    int gesture_bottom_inset;
} V5ToolpathViewport;

static inline const V5ToolpathViewport *v5_toolpath_viewport(void)
{
    static const V5ToolpathViewport viewport = {
        0, 55, 394, 386,
        135, 67, 63
    };
    return &viewport;
}

#endif
