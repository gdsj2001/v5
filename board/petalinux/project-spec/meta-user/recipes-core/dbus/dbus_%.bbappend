# Keep D-Bus available for the wireless control path without its optional
# X11 autolaunch support, which is outside the product runtime closure.
PACKAGECONFIG_remove = "x11"
