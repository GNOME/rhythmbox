/*
**  Sinek (Media Player)
**  Copyright (c) 2001-2002 Gurer Ozen
**
**  This code is free software; you can redistribute it and/or
**  modify it under the terms of the GNU General Public License.
**
**  screen saver control
*/

#include "config.h"
#include "scrsaver.h"

#include <glib.h>
#ifdef HAVE_XTEST
#include <X11/extensions/XTest.h>
#endif /* HAVE_XTEST */
#include <X11/keysym.h>

static int disabled;
static int timeout, interval, prefer_blanking, allow_exposures;

#ifdef HAVE_XTEST
static int keycode;
static Bool xtest;

static gboolean fake_event (gpointer data)
{
	Display *display = (Display *)data;

	if(disabled)
	{
		XTestFakeKeyEvent (display, keycode, True, CurrentTime);
		XTestFakeKeyEvent (display, keycode, False, CurrentTime);
	}

	return TRUE;
}
#endif /* HAVE_XTEST */

void scrsaver_init (Display *display)
{
#ifdef HAVE_XTEST
	int a, b, c, d;
	xtest = XTestQueryExtension (display, &a, &b, &c, &d);
	if(xtest == True)
	{
		keycode = XKeysymToKeycode (display, XK_Shift_L);
		g_timeout_add (15000, (GSourceFunc)fake_event, display);
	}
#endif /* HAVE_XTEST */
}

void scrsaver_disable(Display *display)
{
#ifdef HAVE_XTEST
	if(xtest == True)
	{
		disabled = 1;
		return;
	}
#endif /* HAVE_XTEST */
	if(disabled == 0)
	{
		XGetScreenSaver(display, &timeout, &interval,
				&prefer_blanking, &allow_exposures);
		XSetScreenSaver(display, 0, 0,
				DontPreferBlanking, DontAllowExposures);
		disabled = 1;
	}
}


void scrsaver_enable(Display *display)
{
#ifdef HAVE_XTEST
	if(xtest == True)
	{
		disabled = 0;
		return;
	}
#endif /* HAVE_XTEST */
	if(disabled)
	{
		XSetScreenSaver(display, timeout, interval,
				prefer_blanking, allow_exposures);
		disabled = 0;
	}
}

