
#include "video-utils.h"

#include <stdint.h>
#include <gdk/gdk.h>
#include <gdk/gdkx.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>

#define MWM_HINTS_DECORATIONS   (1L << 1)
#define PROP_MWM_HINTS_ELEMENTS 5
typedef struct {
	uint32_t flags;
	uint32_t functions;
	uint32_t decorations;
	int32_t input_mode;
	uint32_t status;
} MWMHints;

static void
wmspec_change_state (gboolean   add,
		Window     window,
		GdkAtom    state1,
		GdkAtom    state2)
{
	XEvent xev;

#define _NET_WM_STATE_REMOVE        0    /* remove/unset property */
#define _NET_WM_STATE_ADD           1    /* add/set property */
#define _NET_WM_STATE_TOGGLE        2    /* toggle property  */  

	xev.xclient.type = ClientMessage;
	xev.xclient.serial = 0;
	xev.xclient.send_event = True;
	xev.xclient.display = gdk_display;
	xev.xclient.window = window;
	xev.xclient.message_type = gdk_x11_get_xatom_by_name ("_NET_WM_STATE");
	xev.xclient.format = 32;
	xev.xclient.data.l[0] = add ? _NET_WM_STATE_ADD : _NET_WM_STATE_REMOVE;
	xev.xclient.data.l[1] = gdk_x11_atom_to_xatom (state1);
	xev.xclient.data.l[2] = gdk_x11_atom_to_xatom (state2);

	XSendEvent (gdk_display,
			GDK_ROOT_WINDOW (),
			False,
			SubstructureRedirectMask | SubstructureNotifyMask,
			&xev);
}

void
old_wmspec_set_fullscreen (Window window)
{
	Atom XA_WIN_LAYER = None;
	long propvalue[1];
	MWMHints mwmhints;
	Atom prop;

	if (XA_WIN_LAYER == None)
		XA_WIN_LAYER = XInternAtom(GDK_DISPLAY (), "_WIN_LAYER", False);

	/* layer above most other things, like gnome panel
	 * WIN_LAYER_ABOVE_DOCK = 10 */
	propvalue[0] = 10;

	XChangeProperty (GDK_DISPLAY (), window, XA_WIN_LAYER,
			XA_CARDINAL, 32, PropModeReplace,
			(unsigned char *) propvalue, 1);

	/* Now set the decorations hints */
	prop = XInternAtom (GDK_DISPLAY (), "_MOTIF_WM_HINTS", False);
	mwmhints.flags = MWM_HINTS_DECORATIONS;
	mwmhints.decorations = 0;
	XChangeProperty (GDK_DISPLAY (), window, prop, prop, 32,
			PropModeReplace, (unsigned char *) &mwmhints,
			PROP_MWM_HINTS_ELEMENTS);
}

void
window_set_fullscreen (Window window, gboolean set)
{
	/* Set full-screen hint */
	wmspec_change_state (set, window,
			gdk_atom_intern ("_NET_WM_STATE_FULLSCREEN", FALSE),
			GDK_NONE);
}

void
eel_gdk_window_set_invisible_cursor (GdkWindow *window)
{
	GdkBitmap *empty_bitmap;
	GdkCursor *cursor;
	GdkColor useless;
	char invisible_cursor_bits[] = { 0x0 }; 

	useless.red = useless.green = useless.blue = 0;
	useless.pixel = 0;

	empty_bitmap = gdk_bitmap_create_from_data (window,
			invisible_cursor_bits,
			1, 1);

	cursor = gdk_cursor_new_from_pixmap (empty_bitmap,
			empty_bitmap,
			&useless,
			&useless, 0, 0);

	gdk_window_set_cursor (window, cursor);

	gdk_cursor_unref (cursor);

	g_object_unref (empty_bitmap);
}

