
#include <gdk/gdk.h>
#include <X11/Xlib.h>

void old_wmspec_set_fullscreen (Window window);
void window_set_fullscreen (Window window, gboolean set);
void eel_gdk_window_set_invisible_cursor (GdkWindow *window);

