
#include <config.h>

#define TE() { gdk_threads_enter (); }
#define TL() { gdk_threads_leave (); }

#ifdef TOTEM_DEBUG
#define D(x...) g_message (x)
#else
#define D(x...)
#endif


