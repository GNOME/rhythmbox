/* 
 * Copyright (C) 2001-2002 the xine project
 * 	Heavily modified by Bastien Nocera <hadess@hadess.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * $Id$
 *
 * the xine engine in a widget - implementation
 */

#include <config.h>

/* system */
#include <math.h>
#include <pthread.h>
#include <string.h>
#include <stdio.h>
/* X11 */
#include <X11/X.h>
#include <X11/Xlib.h>
#include <X11/extensions/XShm.h>
#include <X11/keysym.h>
/* gtk+/gnome */
#include <gdk/gdkx.h>
#include <gtk/gtk.h>
/* xine */
#include <xine.h>

#include "debug.h"
#include "gtk-xine.h"
#include "gtkxine-marshal.h"
#include "scrsaver.h"
#include "video-utils.h"

#ifdef ENABLE_NLS
#    include <libintl.h>
#    ifdef BONOBO_EXPLICIT_TRANSLATION_DOMAIN
#        undef _
#        define _(String) dgettext (BONOBO_EXPLICIT_TRANSLATION_DOMAIN, String)
#    else
#        define _(String) gettext (String)
#    endif
#    ifdef gettext_noop
#        define N_(String) gettext_noop (String)
#    else
#        define N_(String) (String)
#    endif
#else
/* Stubs that do something close enough.  */
#    define textdomain(String) (String)
#    define gettext(String) (String)
#    define dgettext(Domain,Message) (Message)
#    define dcgettext(Domain,Message,Type) (Message)
#    define bindtextdomain(Domain,Directory) (Domain)
#    define _(String) (String)
#    define N_(String) (String)
#endif

#define DEFAULT_HEIGHT 420
#define DEFAULT_WIDTH 315
#define CONFIG_FILE ".gnome2"G_DIR_SEPARATOR_S"totem_config"
#define DEFAULT_TITLE _("Totem Video Window")

#define BLACK_PIXEL \
	BlackPixel ((gtx->priv->display ? gtx->priv->display : gdk_display), \
			gtx->priv->screen)

/* missing stuff from X includes */
#ifndef XShmGetEventBase
extern int XShmGetEventBase (Display *);
#endif

/* this struct is used to decouple signals coming out of the Xine threads */
typedef struct
{
	gint type;		/* one of the signals in the following enum */
	GtkXineError error_type;
	char *message;		/* or NULL */
	guint keyval;		/* for KEY_PRESS events */
} GtkXineSignal;

/* Signals */
enum {
	ERROR,
	MOUSE_MOTION,
	KEY_PRESS,
	EOS,
	TITLE_CHANGE,
	LAST_SIGNAL
};

/* Enum for none-signal stuff that needs to go through the AsyncQueue */
enum {
	RATIO = LAST_SIGNAL
};

/* Arguments */
enum {
	PROP_0,
	PROP_FULLSCREEN,
	PROP_SPEED,
	PROP_POSITION,
	PROP_AUDIOCHANNEL,
	PROP_CURRENT_TIME,
	PROP_STREAM_LENGTH,
	PROP_PLAYING,
	PROP_SEEKABLE,
	PROP_SHOWCURSOR,
};

static int speeds[2] = {
	XINE_SPEED_PAUSE,
	XINE_SPEED_NORMAL,
};

struct GtkXinePrivate {
	/* Xine stuff */
	xine_t *xine;
	xine_stream_t *stream;
	xine_vo_driver_t *vo_driver;
	xine_ao_driver_t *ao_driver;
	pthread_t thread;
	xine_event_queue_t *ev_queue;
	double display_ratio;

	/* Configuration */
	gboolean null_out;

	/* X stuff */
	Display *display;
	int screen;
	Window video_window;
	int completion_event;

	/* Other stuff */
	int xpos, ypos;
	gboolean init_finished;

	GAsyncQueue *queue;
	int video_width, video_height;

	/* fullscreen stuff */
	gboolean fullscreen_mode;
	GdkWindow *fullscreen_window;
	gboolean cursor_shown;
	gboolean pml;
};


static void gtk_xine_class_init (GtkXineClass *klass);
static void gtk_xine_instance_init (GtkXine *gtx);

static void gtk_xine_set_property (GObject *object, guint property_id,
		const GValue *value, GParamSpec *pspec);
static void gtk_xine_get_property (GObject *object, guint property_id,
		GValue *value, GParamSpec *pspec);

static void gtk_xine_realize (GtkWidget *widget);
static void gtk_xine_unrealize (GtkWidget *widget);
static void gtk_xine_finalize (GObject *object);

static gint gtk_xine_expose (GtkWidget *widget, GdkEventExpose *event);

static void gtk_xine_size_allocate (GtkWidget *widget,
				    GtkAllocation *allocation);

static GtkWidgetClass *parent_class = NULL;

static void xine_event (void *user_data, const xine_event_t *event);
static gboolean gtk_xine_idle_signal (GtkXine *gtx);

static int gtx_table_signals[LAST_SIGNAL] = { 0 };

GtkType
gtk_xine_get_type (void)
{
	static GtkType gtk_xine_type = 0;

	if (!gtk_xine_type) {
		static const GTypeInfo gtk_xine_info = {
			sizeof (GtkXineClass),
			(GBaseInitFunc) NULL,
			(GBaseFinalizeFunc) NULL,
			(GClassInitFunc) gtk_xine_class_init,
			(GClassFinalizeFunc) NULL,
			NULL /* class_data */,
			sizeof (GtkXine),
			0 /* n_preallocs */,
			(GInstanceInitFunc) gtk_xine_instance_init,
		};

		gtk_xine_type = g_type_register_static (GTK_TYPE_WIDGET,
				"GtkXine", &gtk_xine_info, (GTypeFlags)0);
	}

	return gtk_xine_type;
}

static void
gtk_xine_class_init (GtkXineClass *klass)
{

	GObjectClass *object_class;
	GtkWidgetClass *widget_class;

	object_class = (GObjectClass *) klass;
	widget_class = (GtkWidgetClass *) klass;

	parent_class = gtk_type_class (gtk_widget_get_type ());

	/* GtkWidget */
	widget_class->realize = gtk_xine_realize;
	widget_class->unrealize = gtk_xine_unrealize;
	widget_class->size_allocate = gtk_xine_size_allocate;
	widget_class->expose_event = gtk_xine_expose;

	/* GObject */
	object_class->set_property = gtk_xine_set_property;
	object_class->get_property = gtk_xine_get_property;
	object_class->finalize = gtk_xine_finalize;

	/* Properties */
	g_object_class_install_property (object_class, PROP_FULLSCREEN,
			g_param_spec_boolean ("fullscreen", NULL, NULL,
				FALSE, G_PARAM_READWRITE));
	g_object_class_install_property (object_class, PROP_SPEED,
			g_param_spec_int ("speed", NULL, NULL,
				SPEED_PAUSE, SPEED_NORMAL,
				0, G_PARAM_READWRITE));
	g_object_class_install_property (object_class, PROP_POSITION,
			g_param_spec_int ("position", NULL, NULL,
				0, 65535, 0, G_PARAM_READABLE));
	g_object_class_install_property (object_class, PROP_AUDIOCHANNEL,
			g_param_spec_int ("audiochannel", NULL, NULL,
				0, 65535, 0, G_PARAM_READWRITE));
	g_object_class_install_property (object_class, PROP_STREAM_LENGTH,
			g_param_spec_int ("stream_length", NULL, NULL,
				0, 65535, 0, G_PARAM_READABLE));
	g_object_class_install_property (object_class, PROP_PLAYING,
			g_param_spec_boolean ("playing", NULL, NULL,
				FALSE, G_PARAM_READABLE));
	g_object_class_install_property (object_class, PROP_SEEKABLE,
			g_param_spec_boolean ("seekable", NULL, NULL,
				FALSE, G_PARAM_READABLE));
	g_object_class_install_property (object_class, PROP_SHOWCURSOR,
			g_param_spec_boolean ("showcursor", NULL, NULL,
				FALSE, G_PARAM_READWRITE));

	/* Signals */
	gtx_table_signals[ERROR] =
		g_signal_new ("error",
				G_TYPE_FROM_CLASS (object_class),
				G_SIGNAL_RUN_LAST,
				G_STRUCT_OFFSET (GtkXineClass, error),
				NULL, NULL,
				gtkxine_marshal_VOID__INT_STRING,
				G_TYPE_NONE, 2, G_TYPE_INT, G_TYPE_STRING);

	gtx_table_signals[MOUSE_MOTION] =
		g_signal_new ("mouse-motion",
				G_TYPE_FROM_CLASS (object_class),
				G_SIGNAL_RUN_LAST,
				G_STRUCT_OFFSET (GtkXineClass, mouse_motion),
				NULL, NULL,
				g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);

	gtx_table_signals[KEY_PRESS] =
		g_signal_new ("key-press",
				G_TYPE_FROM_CLASS (object_class),
				G_SIGNAL_RUN_LAST,
				G_STRUCT_OFFSET (GtkXineClass, key_press),
				NULL, NULL,
				g_cclosure_marshal_VOID__UINT,
				G_TYPE_NONE, 1, G_TYPE_UINT);

	gtx_table_signals[EOS] =
		g_signal_new ("eos",
				G_TYPE_FROM_CLASS (object_class),
				G_SIGNAL_RUN_LAST,
				G_STRUCT_OFFSET (GtkXineClass, eos),
				NULL, NULL,
				g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);

	gtx_table_signals[TITLE_CHANGE] =
		g_signal_new ("title-change",
				G_TYPE_FROM_CLASS (object_class),
				G_SIGNAL_RUN_LAST,
				G_STRUCT_OFFSET (GtkXineClass, title_change),
				NULL, NULL,
				g_cclosure_marshal_VOID__STRING,
				G_TYPE_NONE, 1, G_TYPE_STRING);

	if (!g_thread_supported ())
		g_thread_init (NULL);
	gdk_threads_init ();
}

static void
gtk_xine_instance_init (GtkXine *gtx)
{
	char *configfile;

	GTK_WIDGET_SET_FLAGS (GTK_WIDGET(gtx), GTK_CAN_FOCUS);

	/* Set the default size to be a 4:3 ratio */
	gtx->widget.requisition.width = DEFAULT_HEIGHT;
	gtx->widget.requisition.height = DEFAULT_WIDTH;

	gtx->priv = g_new0 (GtkXinePrivate, 1);
	gtx->priv->xine = xine_new ();
	gtx->priv->stream = NULL;
	gtx->priv->vo_driver = NULL;
	gtx->priv->ao_driver = NULL;
	gtx->priv->ev_queue = NULL;
	gtx->priv->display = NULL;
	gtx->priv->null_out = FALSE;
	gtx->priv->fullscreen_mode = FALSE;
	gtx->priv->init_finished = FALSE;
	gtx->priv->cursor_shown = TRUE;
	gtx->priv->pml = FALSE;

	gtx->priv->queue = g_async_queue_new ();

	/* generate and init configuration  */
	configfile = g_build_path (G_DIR_SEPARATOR_S,
			g_get_home_dir (), CONFIG_FILE, NULL);
	xine_config_load (gtx->priv->xine, configfile);
	g_free (configfile);

	xine_init (gtx->priv->xine);
}

static void
gtk_xine_finalize (GObject *object)
{
	GtkXine *gtx = (GtkXine *) object;

	/* Should put here what needs to be destroyed */
	g_idle_remove_by_data (gtx);
	g_async_queue_unref (gtx->priv->queue);
	G_OBJECT_CLASS (parent_class)->finalize (object);

	gtx->priv = NULL;
	gtx = NULL;
}

static void
dest_size_cb (void *gtx_gen,
	      int video_width, int video_height,
	      double video_pixel_aspect,
	      int *dest_width, int *dest_height,
	      double *dest_pixel_aspect)
{
	GtkXine *gtx = (GtkXine *) gtx_gen;

	/* correct size with video_pixel_aspect */
	if (video_pixel_aspect >= gtx->priv->display_ratio)
		video_width  = video_width * video_pixel_aspect
			/ gtx->priv->display_ratio + .5;
	else
		video_height = video_height * gtx->priv->display_ratio
			/ video_pixel_aspect + .5;

	if (gtx->priv->fullscreen_mode)
	{
		*dest_width = gdk_screen_width ();
		*dest_height = gdk_screen_height ();
	} else {
		*dest_width = gtx->widget.allocation.width;
		*dest_height = gtx->widget.allocation.height;
	}

	*dest_pixel_aspect = gtx->priv->display_ratio;
}

static void
frame_output_cb (void *gtx_gen,
		 int video_width, int video_height,
		 double video_pixel_aspect,
		 int *dest_x, int *dest_y,
		 int *dest_width, int *dest_height,
		 double *dest_pixel_aspect,
		 int *win_x, int *win_y)
{
	GtkXine *gtx = (GtkXine *) gtx_gen;

	if (gtx == NULL || gtx->priv == NULL)
		return;

	/* correct size with video_pixel_aspect */
	if (video_pixel_aspect >= gtx->priv->display_ratio)
		video_width = video_width * video_pixel_aspect
			/ gtx->priv->display_ratio + .5;
	else
		video_height = video_height * gtx->priv->display_ratio
			/ video_pixel_aspect + .5;

	*dest_x = 0;
	*dest_y = 0;
	*win_x = gtx->priv->xpos;
	*win_y = gtx->priv->ypos;

	if (gtx->priv->fullscreen_mode)
	{
		*dest_width = gdk_screen_width ();
		*dest_height = gdk_screen_height ();
	} else {
		*dest_width = gtx->widget.allocation.width;
		*dest_height = gtx->widget.allocation.height;

		/* Size changed */
		if (gtx->priv->video_width != video_width
				|| gtx->priv->video_height != video_height)
		{
			gtx->priv->video_width = video_width;
			gtx->priv->video_height = video_height;
		}
	}

	*dest_pixel_aspect = gtx->priv->display_ratio;
}

static xine_vo_driver_t *
load_video_out_driver (GtkXine *gtx)
{
	double res_h, res_v;
	x11_visual_t vis;
	const char *video_driver_id;
	xine_vo_driver_t *vo_driver;

	vis.display = gtx->priv->display;
	vis.screen = gtx->priv->screen;
	vis.d = gtx->priv->video_window;
	res_h =
	    (DisplayWidth (gtx->priv->display, gtx->priv->screen) * 1000 /
	     DisplayWidthMM (gtx->priv->display, gtx->priv->screen));
	res_v =
	    (DisplayHeight (gtx->priv->display, gtx->priv->screen) * 1000 /
	     DisplayHeightMM (gtx->priv->display, gtx->priv->screen));
	gtx->priv->display_ratio = res_v / res_h;

	if (fabs (gtx->priv->display_ratio - 1.0) < 0.01) {
		gtx->priv->display_ratio = 1.0;
	}

	vis.dest_size_cb = dest_size_cb;
	vis.frame_output_cb = frame_output_cb;
	vis.user_data = gtx;

	/* Try to init video with stored information */
	video_driver_id = xine_config_register_string (gtx->priv->xine,
			"video.driver", "auto", "video driver to use",
			NULL, 10, NULL, NULL);
	//FIXME
#if 0
	if (gtx->priv->null_out == TRUE)
	{
		vo_driver = xine_open_video_driver (gtx->priv->xine,
				"none", XINE_VISUAL_TYPE_NONE, (void *) &vis);

		return vo_driver;
	}
#endif
	if (strcmp (video_driver_id, "auto") != 0)
	{
		vo_driver = xine_open_video_driver (gtx->priv->xine,
						   video_driver_id,
						   XINE_VISUAL_TYPE_X11,
						   (void *) &vis);
		if (vo_driver)
			return vo_driver;
	}

	vo_driver = xine_open_video_driver (gtx->priv->xine, NULL,
			XINE_VISUAL_TYPE_X11, (void *) &vis);

	return vo_driver;
}

static xine_ao_driver_t *
load_audio_out_driver (GtkXine *gtx)
{
	xine_ao_driver_t *ao_driver;
	char *audio_driver_id = NULL;

	if (gtx->priv->null_out == TRUE)
		return NULL;

	/* No configuration, fallback to auto */
	if (audio_driver_id == NULL || strcmp (audio_driver_id, "") == 0)
		audio_driver_id = g_strdup ("auto");

	/* We know how to handle null driver */
	if (strcmp (audio_driver_id, "null") == 0)
		return NULL;

	/* auto probe */
	if (strcmp (audio_driver_id, "auto") == 0)
		return xine_open_audio_driver (gtx->priv->xine, NULL, NULL);

	/* no autoprobe */
	ao_driver = xine_open_audio_driver (gtx->priv->xine,
			audio_driver_id, NULL);

	/* if it failed without autoprobe, probe */
	if (ao_driver == NULL)
		ao_driver = xine_open_audio_driver (gtx->priv->xine,
				NULL, NULL);

	g_free (audio_driver_id);

	return ao_driver;
}

static gboolean
video_window_translate_point (GtkXine *gtx, int gui_x, int gui_y,
		int *video_x, int *video_y)
{
	x11_rectangle_t rect;

	rect.x = gui_x;
	rect.y = gui_y;
	rect.w = 0;
	rect.h = 0;

	if (xine_gui_send_vo_data (gtx->priv->stream,
				XINE_GUI_SEND_TRANSLATE_GUI_TO_VIDEO,
				(void*)&rect) != -1)
	{
		/* the driver implements gui->video coordinate space translation
		 * so we use it */
		*video_x = rect.x;
		*video_y = rect.y;
		return TRUE;
	}

	return FALSE;
}

static void
generate_mouse_event (GtkXine *gtx, XEvent *event, gboolean is_motion)
{
	XMotionEvent *mevent = (XMotionEvent *) event;
	XButtonEvent *bevent = (XButtonEvent *) event;
	int x, y;
	gboolean retval;

	if (is_motion == FALSE && bevent->button != Button1)
		return;

	if (is_motion == TRUE)
		retval = video_window_translate_point (gtx,
				mevent->x, mevent->y, &x, &y);
	else
		retval = video_window_translate_point (gtx,
				bevent->x, bevent->y, &x, &y);

	if (retval == TRUE)
	{
		xine_event_t event;
		xine_input_data_t input;

		if (is_motion == TRUE)
		{
			event.type = XINE_EVENT_INPUT_MOUSE_MOVE;
			input.button = 0; /* Just motion. */
		} else {
			event.type = XINE_EVENT_INPUT_MOUSE_BUTTON;
			input.button = 1;
		}

		input.x = x;
		input.y = y;
		event.stream = gtx->priv->stream;
		event.data = &input;
		event.data_length = sizeof(input);

		xine_event_send (gtx->priv->stream,
				(xine_event_t *) (&event));
	}
}

static void *
xine_thread (void *gtx_gen)
{
	GtkXine *gtx = (GtkXine *) gtx_gen;
	XEvent event;

	gtx->priv->init_finished = TRUE;

	while (gtx->priv->display != NULL && gtx->priv->stream != NULL)
	{
		XNextEvent (gtx->priv->display, &event);

		switch (event.type)
		{
		case Expose:
			if (event.xexpose.count != 0)
				break;
			xine_gui_send_vo_data (gtx->priv->stream,
					XINE_GUI_SEND_EXPOSE_EVENT,
					&event);
			break;
		case MotionNotify:
			generate_mouse_event (gtx, &event, TRUE);
			if (gtx->priv->fullscreen_mode == TRUE)
			{
				GtkXineSignal *signal;

				signal = g_new0 (GtkXineSignal, 1);
				signal->type = MOUSE_MOTION;
				g_async_queue_push (gtx->priv->queue, signal);
				g_idle_add ((GSourceFunc) gtk_xine_idle_signal,
						gtx);
			}
			break;
		case ButtonPress:
			generate_mouse_event (gtx, &event, FALSE);
			break;
		case KeyPress:
			if (gtx->priv->fullscreen_mode == TRUE)
			{
				GtkXineSignal *signal;
				char buf[16];
				KeySym keysym;
				static XComposeStatus compose;

				signal = g_new0 (GtkXineSignal, 1);
				signal->type = KEY_PRESS;
				XLookupString (&event.xkey, buf, 16,
						&keysym, &compose);
				signal->keyval = keysym;
				g_async_queue_push (gtx->priv->queue, signal);
				g_idle_add ((GSourceFunc) gtk_xine_idle_signal,
						gtx);
			}
			break;
		}

		if (event.type == gtx->priv->completion_event)
		{
			xine_gui_send_vo_data (gtx->priv->stream,
					XINE_GUI_SEND_COMPLETION_EVENT,
					&event);
		}
	}

	pthread_exit (NULL);
	return NULL;
}

static gboolean
configure_cb (GtkWidget *widget, GdkEventConfigure *event, gpointer user_data)
{
	GtkXine *gtx = (GtkXine *) user_data;

	gtx->priv->xpos = event->x;
	gtx->priv->ypos = event->y;

	return FALSE;
}

static void
gtk_xine_realize (GtkWidget *widget)
{
	GtkXine *gtx;

	g_return_if_fail (widget != NULL);
	g_return_if_fail (GTK_IS_XINE (widget));

	gtx = GTK_XINE (widget);

	/* set realized flag */
	GTK_WIDGET_SET_FLAGS (widget, GTK_REALIZED);
#if 0
	attr.x = widget->allocation.x;
	attr.y = widget->allocation.y;
	attr.width = widget->allocation.width;
	attr.height = widget->allocation.height;
	attr.window_type = GDK_WINDOW_CHILD;
	attr.wclass = GDK_INPUT_OUTPUT;
	attr.event_mask = gtk_widget_get_events (widget) | GDK_EXPOSURE_MASK;
	widget->window = gdk_window_new (gtk_widget_get_parent_window (widget),
			&attr, GDK_WA_X | GDK_WA_Y | GDK_WA_WMCLASS);
	gdk_window_show (widget->window);
	gdk_window_set_user_data (widget->window, gtx);

	gtx->priv->video_window = GDK_WINDOW_XWINDOW (widget->window);
#else
	gtx->priv->video_window = XCreateSimpleWindow
		(gdk_display,
		 GDK_WINDOW_XWINDOW (gtk_widget_get_parent_window (widget)),
		 0, 0,
		 widget->allocation.width, widget->allocation.height,
		 1, BLACK_PIXEL, BLACK_PIXEL);

	widget->window = gdk_window_foreign_new (gtx->priv->video_window);
	gdk_window_set_user_data (widget->window, gtx);
#endif

	/* track configure events of toplevel window */
	g_signal_connect (GTK_OBJECT (gtk_widget_get_toplevel (widget)),
			  "configure-event",
			  GTK_SIGNAL_FUNC (configure_cb), gtx);

	/* Init threads in X and setup the needed X stuff */
	if (!XInitThreads ())
	{
		g_signal_emit (G_OBJECT (gtx),
				gtx_table_signals[ERROR], 0,
				0,
				_("Could not initialise the threads support.\n"
				"You should install a thread-safe Xlib."));
		return;
	}

	gtx->priv->display = XOpenDisplay (NULL);
	XLockDisplay (gtx->priv->display);
	gtx->priv->screen = DefaultScreen (gtx->priv->display);

	if (XShmQueryExtension (gtx->priv->display) == True)
	{
		gtx->priv->completion_event =
		    XShmGetEventBase (gtx->priv->display) + ShmCompletion;
	} else {
		gtx->priv->completion_event = -1;
	}

	XSelectInput (gtx->priv->display, gtx->priv->video_window,
		      StructureNotifyMask | ExposureMask
		      | ButtonPressMask | PointerMotionMask
		      | KeyPressMask | PropertyChangeMask);

	/* load audio, video drivers */
	gtx->priv->ao_driver = load_audio_out_driver (gtx);
	gtx->priv->vo_driver = load_video_out_driver (gtx);

	if (!gtx->priv->vo_driver)
	{
		XUnlockDisplay (gtx->priv->display);
		g_signal_emit (G_OBJECT (gtx),
				gtx_table_signals[ERROR], 0,
				GTX_STARTUP,
				_("Could not find a suitable video output."));
		return;
	}

	gtx->priv->stream = xine_stream_new (gtx->priv->xine,
			gtx->priv->ao_driver, gtx->priv->vo_driver);
	gtx->priv->ev_queue = xine_event_new_queue (gtx->priv->stream);

	XUnlockDisplay (gtx->priv->display);

	/* Setup xine events, the screensaver and the event filter */
	xine_event_create_listener_thread (gtx->priv->ev_queue,
			xine_event, (void *) gtx);

	scrsaver_init (gtx->priv->display);

	/* now, create a xine thread */
	pthread_create (&gtx->priv->thread, NULL, xine_thread, gtx);

	return;
}

static gboolean
gtk_xine_idle_signal (GtkXine *gtx)
{
	GtkXineSignal *signal;
	int queue_length;

	signal = g_async_queue_try_pop (gtx->priv->queue);
	if (signal == NULL)
		return FALSE;

	TE ();
	switch (signal->type)
	{
	case ERROR:
		/* We don't emit the ERROR signal when in fullscreen mode */
		if (gtx->priv->fullscreen_mode == TRUE)
			break;

		g_signal_emit (G_OBJECT (gtx),
				gtx_table_signals[ERROR], 0,
				signal->error_type, signal->message);
		break;
	case MOUSE_MOTION:
		g_signal_emit (G_OBJECT (gtx),
				gtx_table_signals[MOUSE_MOTION],
				0, NULL);
		break;
	case KEY_PRESS:
		g_signal_emit (G_OBJECT (gtx),
				gtx_table_signals[KEY_PRESS],
				0, signal->keyval);
		break;
	case EOS:
		g_signal_emit (G_OBJECT (gtx),
				gtx_table_signals[EOS], 0, NULL);
		break;
	case TITLE_CHANGE:
		g_signal_emit (G_OBJECT (gtx),
				gtx_table_signals[TITLE_CHANGE],
				0, signal->message);
		break;
	/* A bit of cheating right here */
	case RATIO:
		gtk_xine_set_scale_ratio (gtx, 0);
		break;
	default:
		break;
	}

	g_free (signal->message);
	g_free (signal);

	queue_length = g_async_queue_length (gtx->priv->queue);
	TL ();

	return (queue_length > 0);
}

static void
xine_event (void *user_data, const xine_event_t *event)
{
	GtkXine *gtx = (GtkXine *) user_data;
	GtkXineSignal *signal;
	xine_ui_data_t *ui_data;

	switch (event->type)
	{
	case XINE_EVENT_UI_PLAYBACK_FINISHED:
			signal = g_new0 (GtkXineSignal, 1);
			signal->type = EOS;
			g_async_queue_push (gtx->priv->queue, signal);
			g_idle_add ((GSourceFunc) gtk_xine_idle_signal, gtx);
			break;
	case XINE_EVENT_UI_SET_TITLE:
			ui_data = (xine_ui_data_t *) event->data;

			signal = g_new0 (GtkXineSignal, 1);
			signal->type = TITLE_CHANGE;
			signal->message = g_strdup (ui_data->str);
			g_async_queue_push (gtx->priv->queue, signal);
			g_idle_add ((GSourceFunc) gtk_xine_idle_signal, gtx);
			break;
	}
}

static void
xine_error (GtkXine *gtx)
{
	GtkXineSignal *signal;
	int error;

	error = xine_get_error (gtx->priv->stream);
	if (error == XINE_ERROR_NONE)
		return;

	signal = g_new0 (GtkXineSignal, 1);
	signal->type = ERROR;

	switch (error)
	{
	case XINE_ERROR_NO_INPUT_PLUGIN:
		signal->error_type = GTX_NO_INPUT_PLUGIN;
		break;
	case XINE_ERROR_NO_DEMUX_PLUGIN:
		signal->error_type = GTX_NO_DEMUXER_PLUGIN;
		break;
	case XINE_ERROR_DEMUX_FAILED:
		signal->error_type = GTX_DEMUXER_FAILED;
		break;
	default:
		break;
	}

	g_async_queue_push (gtx->priv->queue, signal);
	g_idle_add ((GSourceFunc) gtk_xine_idle_signal, gtx);
}

static void
gtk_xine_unrealize (GtkWidget *widget)
{
	GtkXine *gtx;
	char *configfile;

	g_return_if_fail (widget != NULL);
	g_return_if_fail (GTK_IS_XINE (widget));

	/* Hide all windows */
	if (GTK_WIDGET_MAPPED (widget))
		gtk_widget_unmap (widget);
	GTK_WIDGET_UNSET_FLAGS (widget, GTK_MAPPED);

	gtx = GTK_XINE (widget);

	/* stop the playback */
	xine_close (gtx->priv->stream);
	xine_event_dispose_queue (gtx->priv->ev_queue);
	xine_dispose (gtx->priv->stream);
	gtx->priv->stream = NULL;

	/* stop the event thread, generating an event to get out of it */
	pthread_cancel (gtx->priv->thread);
	/* we shouldn't need pthread_join here */

	/* Kill the drivers */
	if (gtx->priv->vo_driver != NULL)
		xine_close_video_driver (gtx->priv->xine,
				gtx->priv->vo_driver);
	if (gtx->priv->vo_driver != NULL)
		xine_close_audio_driver (gtx->priv->xine,
				gtx->priv->ao_driver);

	/* save config */
	configfile = g_build_path (G_DIR_SEPARATOR_S,
			g_get_home_dir (), CONFIG_FILE, NULL);
	xine_config_save (gtx->priv->xine, configfile);
	g_free (configfile);

	/* stop event thread */
	xine_exit (gtx->priv->xine);
	gtx->priv->xine = NULL;

	/* Finally, kill the left-over windows */
	if (gtx->priv->fullscreen_window != NULL)
		g_object_unref (GDK_DRAWABLE (gtx->priv->fullscreen_window));

	/* This destroys widget->window and unsets the realized flag */
	if (GTK_WIDGET_CLASS (parent_class)->unrealize)
		(*GTK_WIDGET_CLASS (parent_class)->unrealize) (widget);
}

GtkWidget *
gtk_xine_new (int width, int height, gboolean null_out)
{
	GtkWidget *gtx;

	gtx = GTK_WIDGET (g_object_new (gtk_xine_get_type (), NULL));

	GTK_XINE (gtx)->priv->null_out = null_out;

	/* defaults are fine if both are negative */
	if (width <= 0 && height <= 0)
		return gtx;
	/* figure out the missing measure from the other one with a 4:3 ratio */
	if (width <= 0)
		width = (int) (height * 4 / 3);
	if (height <= 0)
		height = (int) (width * 3 / 4);

	GTK_XINE (gtx)->widget.requisition.width = width;
	GTK_XINE (gtx)->widget.requisition.height = height;

	return gtx;
}

gboolean
gtk_xine_check (GtkXine *gtx)
{
	g_return_val_if_fail (gtx != NULL, FALSE);
	g_return_val_if_fail (GTK_IS_XINE (gtx), FALSE);

	if (gtx->priv->stream == NULL)
		return FALSE;

	return gtx->priv->init_finished;
}

static gint
gtk_xine_expose (GtkWidget *widget, GdkEventExpose *event)
{
	return FALSE;
}

static void
gtk_xine_size_allocate (GtkWidget *widget, GtkAllocation *allocation)
{
	GtkXine *gtx;

	g_return_if_fail (widget != NULL);
	g_return_if_fail (GTK_IS_XINE (widget));

	gtx = GTK_XINE (widget);

	widget->allocation = *allocation;
	gtx->priv->xpos = allocation->x;
	gtx->priv->ypos = allocation->y;

	if (GTK_WIDGET_REALIZED (widget)) {
		/* HACK it seems to be 1 pixel off, weird */
		gdk_window_move_resize (widget->window,
					allocation->x - 1,
					allocation->y - 1,
					allocation->width,
					allocation->height);
	}
}

static char *
get_fourcc_string (uint32_t f)
{
	char fcc[5];

	memset(&fcc, 0, sizeof(fcc));

	/* Should we take care about endianess ? */
	fcc[0] = f     | 0xFFFFFF00;
	fcc[1] = f>>8  | 0xFFFFFF00;
	fcc[2] = f>>16 | 0xFFFFFF00;
	fcc[3] = f>>24 | 0xFFFFFF00;
	fcc[4] = 0;

	if(f <= 0xFFFF)
		sprintf(fcc, "0x%x", f);

	if((fcc[0] == 'm') && (fcc[1] == 's'))
	{
		if((fcc[2] = 0x0) && (fcc[3] == 0x55))
			*(uint32_t *) fcc = 0x33706d2e; /* Force to '.mp3' */
	}

	return g_strdup (&fcc[0]);
}


gboolean
gtk_xine_open (GtkXine *gtx, const gchar *mrl)
{
	int error;
	gboolean has_video;

	g_return_val_if_fail (gtx != NULL, -1);
	g_return_val_if_fail (mrl != NULL, -1);
	g_return_val_if_fail (GTK_IS_XINE (gtx), -1);
	g_return_val_if_fail (gtx->priv->xine != NULL, -1);

	error = xine_open (gtx->priv->stream, mrl);
	if (!error)
	{
		xine_error (gtx);
		return FALSE;
	}

	if (xine_get_stream_info (gtx->priv->stream,
				XINE_STREAM_INFO_VIDEO_HANDLED) == FALSE
		&& xine_get_stream_info (gtx->priv->stream,
				XINE_STREAM_INFO_AUDIO_HANDLED) == FALSE)
	{
		GtkXineSignal *signal;
		uint32_t fourcc;
		char *fourcc_str;
		const char *name;

		fourcc = xine_get_stream_info (gtx->priv->stream,
				XINE_STREAM_INFO_VIDEO_FOURCC);
		fourcc_str = get_fourcc_string (fourcc);
		name = xine_get_meta_info (gtx->priv->stream,
				XINE_META_INFO_VIDEOCODEC);

		gtk_xine_close (gtx);

		signal = g_new0 (GtkXineSignal, 1);
		signal->type = ERROR;
		signal->error_type = GTX_NO_CODEC;
		signal->message = g_strdup_printf (_("Reason: Video type '%s' is not handled."), name ? name : fourcc_str );
		g_async_queue_push (gtx->priv->queue, signal);
		g_idle_add ((GSourceFunc) gtk_xine_idle_signal, gtx);

		g_free (fourcc_str);

		return FALSE;
	}

	has_video = xine_get_stream_info(gtx->priv->stream,
			XINE_STREAM_INFO_HAS_VIDEO);

	return TRUE;
}

gboolean
gtk_xine_play (GtkXine *gtx, guint pos, guint start_time)
{
	int error, length;

	g_return_val_if_fail (gtx != NULL, -1);
	g_return_val_if_fail (GTK_IS_XINE (gtx), -1);
	g_return_val_if_fail (gtx->priv->xine != NULL, -1);

	length = gtk_xine_get_stream_length (gtx);
	error = xine_play (gtx->priv->stream, pos,
			CLAMP (start_time, 0, length));

	if (error == 0)
	{
		xine_error (gtx);
		return FALSE;
	}
	return TRUE;
}

void
gtk_xine_stop (GtkXine *gtx)
{
	g_return_if_fail (gtx != NULL);
	g_return_if_fail (GTK_IS_XINE (gtx));
	g_return_if_fail (gtx->priv->xine != NULL);

	if (gtx->priv->stream == NULL)
		return;

	xine_stop (gtx->priv->stream);
}

void
gtk_xine_close (GtkXine *gtx)
{
	g_return_if_fail (gtx != NULL);
	g_return_if_fail (GTK_IS_XINE (gtx));
	g_return_if_fail (gtx->priv->xine != NULL);

	if (gtx->priv->stream == NULL)
		return;

	xine_close (gtx->priv->stream);
}

/* Properties */
static void
gtk_xine_set_property (GObject *object, guint property_id,
		const GValue *value, GParamSpec *pspec)
{
	GtkXine *gtx;

	g_return_if_fail (GTK_IS_XINE (object));

	gtx = GTK_XINE (object);

	switch (property_id)
	{
	case PROP_FULLSCREEN:
		gtk_xine_set_fullscreen (gtx, g_value_get_boolean (value));
		break;
	case PROP_SPEED:
		gtk_xine_set_speed (gtx, g_value_get_int (value));
		break;
	case PROP_AUDIOCHANNEL:
		gtk_xine_set_audio_channel (gtx, g_value_get_int (value));
		break;
	case PROP_SHOWCURSOR:
		gtk_xine_set_show_cursor (gtx, g_value_get_boolean (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
	}
}

static void
gtk_xine_get_property (GObject *object, guint property_id,
		GValue *value, GParamSpec *pspec)
{
	GtkXine *gtx;

	g_return_if_fail (GTK_IS_XINE (object));

	gtx = GTK_XINE (object);

	switch (property_id)
	{
	case PROP_FULLSCREEN:
		g_value_set_boolean (value, gtk_xine_is_fullscreen (gtx));
		break;
	case PROP_SPEED:
		g_value_set_int (value, gtk_xine_get_speed (gtx));
		break;
	case PROP_POSITION:
		g_value_set_int (value, gtk_xine_get_position (gtx));
		break;
	case PROP_AUDIOCHANNEL:
		g_value_set_int (value, gtk_xine_get_audio_channel (gtx));
		break;
	case PROP_STREAM_LENGTH:
		g_value_set_int (value, gtk_xine_get_stream_length (gtx));
		break;
	case PROP_PLAYING:
		g_value_set_boolean (value, gtk_xine_is_playing (gtx));
		break;
	case PROP_SEEKABLE:
		g_value_set_boolean (value, gtk_xine_is_seekable (gtx));
		break;
	case PROP_SHOWCURSOR:
		g_value_set_boolean (value, gtk_xine_get_show_cursor (gtx));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
	}
}

void
gtk_xine_set_speed (GtkXine *gtx, Speeds speed)
{
	g_return_if_fail (gtx != NULL);
	g_return_if_fail (GTK_IS_XINE (gtx));
	g_return_if_fail (gtx->priv->xine != NULL);

	xine_set_param (gtx->priv->stream, XINE_PARAM_SPEED, speeds[speed]);
}

gint
gtk_xine_get_speed (GtkXine *gtx)
{
	g_return_val_if_fail (gtx != NULL, SPEED_NORMAL);
	g_return_val_if_fail (GTK_IS_XINE (gtx), SPEED_NORMAL);
	g_return_val_if_fail (gtx->priv->xine != NULL, SPEED_NORMAL);

	return xine_get_param (gtx->priv->stream, XINE_PARAM_SPEED);
}

gint
gtk_xine_get_position (GtkXine *gtx)
{
	int pos_stream = 0, i = 0;
	int pos_time, length_time;
	gboolean ret;

	g_return_val_if_fail (gtx != NULL, 0);
	g_return_val_if_fail (GTK_IS_XINE (gtx), 0);
	g_return_val_if_fail (gtx->priv->xine != NULL, 0);

	if (gtk_xine_is_playing (gtx) == FALSE)
		return 0;

	ret = xine_get_pos_length (gtx->priv->stream, &pos_stream,
			&pos_time, &length_time);

	while (ret == FALSE && i < 10)
	{
		usleep (100000);
		ret = xine_get_pos_length (gtx->priv->stream, &pos_stream,
				&pos_time, &length_time);
		i++;
	}

	if (ret == FALSE)
		return -1;

	return pos_stream;
}

void
gtk_xine_set_audio_channel (GtkXine *gtx, gint audio_channel)
{
	g_return_if_fail (gtx != NULL);
	g_return_if_fail (GTK_IS_XINE (gtx));
	g_return_if_fail (gtx->priv->xine != NULL);

	xine_set_param (gtx->priv->stream,
			XINE_PARAM_AUDIO_CHANNEL_LOGICAL, audio_channel);
}

gint
gtk_xine_get_audio_channel (GtkXine *gtx)
{
	g_return_val_if_fail (gtx != NULL, 0);
	g_return_val_if_fail (GTK_IS_XINE (gtx), 0);
	g_return_val_if_fail (gtx->priv->xine != NULL, 0);

	return xine_get_param (gtx->priv->stream,
			XINE_PARAM_AUDIO_CHANNEL_LOGICAL);
}

void
gtk_xine_set_fullscreen (GtkXine *gtx, gboolean fullscreen)
{
	g_return_if_fail (gtx != NULL);
	g_return_if_fail (GTK_IS_XINE (gtx));
	g_return_if_fail (gtx->priv->xine != NULL);

	XLockDisplay (gtx->priv->display);

	if (gtx->priv->pml == FALSE)
		gtx->priv->pml = TRUE;
	else
		return;

	if (fullscreen == gtx->priv->fullscreen_mode)
	{
		XUnlockDisplay (gtx->priv->display);
		return;
	}

	gtx->priv->fullscreen_mode = fullscreen;

	if (fullscreen)
	{
		Window win;
		XEvent xev;
		XSizeHints hint;
		GdkWindow *parent;

		parent = gdk_window_get_toplevel (gtx->widget.window);

		hint.x = 0;
		hint.y = 0;
		hint.width = gdk_screen_width ();
		hint.height = gdk_screen_height ();

		win = XCreateSimpleWindow (gtx->priv->display,
				GDK_ROOT_WINDOW (),
				0, 0,
				hint.width, hint.height, 1,
				BLACK_PIXEL, BLACK_PIXEL);

		hint.win_gravity = StaticGravity;
		hint.flags = PPosition | PSize | PWinGravity;

		XSetStandardProperties (gtx->priv->display, win,
				DEFAULT_TITLE, DEFAULT_TITLE, None,
				NULL, 0, 0);

		XSetWMNormalHints (gtx->priv->display, win, &hint);

		old_wmspec_set_fullscreen (win);
		/* TODO add check for full-screen from
		 * fullscreen_callback
		 * in terminal-window.c (profterm) */
		window_set_fullscreen (win, TRUE);
		XRaiseWindow(gtx->priv->display, win);

		XSelectInput (gtx->priv->display, win,
				StructureNotifyMask | ExposureMask
				| FocusChangeMask | ButtonPressMask
				| PointerMotionMask | KeyPressMask);

		/* Map window */
		XMapRaised (gtx->priv->display, win);
		XFlush (gtx->priv->display);

		/* Wait for map */
		do {
			XMaskEvent (gtx->priv->display,
					StructureNotifyMask, &xev);
		} while (xev.type != MapNotify || xev.xmap.event != win);

		XMoveWindow (gtx->priv->display, win, 0, 0);

		/* FIXME Why does this sometimes return NULL ? */
		gtx->priv->fullscreen_window =
			gdk_window_foreign_new (win);
		gdk_window_set_transient_for
			(gtx->priv->fullscreen_window, parent);

		xine_gui_send_vo_data (gtx->priv->stream,
			 XINE_GUI_SEND_DRAWABLE_CHANGED,
			 (void*) GDK_WINDOW_XID (gtx->priv->fullscreen_window));

		/* switch off mouse cursor */
		gtk_xine_set_show_cursor (gtx, FALSE);

		scrsaver_disable (gtx->priv->display);
		XFlush(gtx->priv->display);
	} else {
		xine_gui_send_vo_data (gtx->priv->stream,
			 XINE_GUI_SEND_DRAWABLE_CHANGED,
			 (void *) gtx->priv->video_window);

		/* Hide the window */
		XDestroyWindow (gtx->priv->display, GDK_WINDOW_XID
				(gtx->priv->fullscreen_window));
		gtx->priv->fullscreen_window = NULL;

		scrsaver_enable (gtx->priv->display);

		gdk_window_focus (gdk_window_get_toplevel
				(gtk_widget_get_parent_window
				 (GTK_WIDGET (gtx))),
				GDK_CURRENT_TIME);
	}

	gtx->priv->pml = FALSE;
	XUnlockDisplay (gtx->priv->display);
}

gint
gtk_xine_is_fullscreen (GtkXine *gtx)
{
	g_return_val_if_fail (gtx != NULL, 0);
	g_return_val_if_fail (GTK_IS_XINE (gtx), 0);
	g_return_val_if_fail (gtx->priv->xine != NULL, 0);

	return gtx->priv->fullscreen_mode;
}

gboolean
gtk_xine_can_set_volume (GtkXine *gtx)
{
	g_return_val_if_fail (gtx != NULL, 0);
	g_return_val_if_fail (GTK_IS_XINE (gtx), 0);
	g_return_val_if_fail (gtx->priv->xine != NULL, 0);

	if (gtx->priv->stream == NULL)
		return FALSE;

	if (xine_get_param (gtx->priv->stream, XINE_PARAM_AUDIO_VOLUME) == -1)
		return FALSE;

	if (xine_get_param (gtx->priv->stream,
				XINE_PARAM_AUDIO_CHANNEL_LOGICAL) == -2)
		return FALSE;

	return xine_get_stream_info (gtx->priv->stream,
			XINE_STREAM_INFO_HAS_AUDIO);
}

void
gtk_xine_set_volume (GtkXine *gtx, gint volume)
{
	g_return_if_fail (gtx != NULL);
	g_return_if_fail (GTK_IS_XINE (gtx));
	g_return_if_fail (gtx->priv->xine != NULL);

	if (gtk_xine_can_set_volume (gtx) == TRUE)
	{
		volume = CLAMP (volume, 0, 100);
		xine_set_param (gtx->priv->stream, XINE_PARAM_AUDIO_VOLUME,
				volume);
	}
}

gint
gtk_xine_get_volume (GtkXine *gtx)
{
	int volume = 0;

	g_return_val_if_fail (gtx != NULL, 0);
	g_return_val_if_fail (GTK_IS_XINE (gtx), 0);
	g_return_val_if_fail (gtx->priv->xine != NULL, 0);

	volume = xine_get_param (gtx->priv->stream,
			XINE_PARAM_AUDIO_VOLUME);

	return volume;
}

void
gtk_xine_set_show_cursor (GtkXine *gtx, gboolean show_cursor)
{
	g_return_if_fail (gtx != NULL);
	g_return_if_fail (GTK_IS_XINE (gtx));
	g_return_if_fail (gtx->priv->xine != NULL);

	if (GDK_IS_WINDOW (gtx->priv->fullscreen_window) == FALSE)
		return;

	if (show_cursor == FALSE)
	{
		eel_gdk_window_set_invisible_cursor
			(gtx->priv->fullscreen_window);
	} else {
		gdk_window_set_cursor (gtx->priv->fullscreen_window, NULL);
	}

	gtx->priv->cursor_shown = show_cursor;
}

gboolean
gtk_xine_get_show_cursor (GtkXine *gtx)
{
	g_return_val_if_fail (gtx != NULL, FALSE);
	g_return_val_if_fail (GTK_IS_XINE (gtx), FALSE);
	g_return_val_if_fail (gtx->priv->xine != NULL, FALSE);

	return gtx->priv->cursor_shown;
}

gint
gtk_xine_get_current_time (GtkXine *gtx)
{
	int pos_time = 0, i = 0;
	int pos_stream, length_time;
	gboolean ret;

	g_return_val_if_fail (gtx != NULL, 0);
	g_return_val_if_fail (GTK_IS_XINE (gtx), 0);
	g_return_val_if_fail (gtx->priv->xine != NULL, 0);

	if (gtk_xine_is_playing (gtx) == FALSE)
		return 0;

	ret = xine_get_pos_length (gtx->priv->stream, &pos_stream,
			&pos_time, &length_time);

	while (ret == FALSE && i < 10)
	{
		usleep (100000);
		ret = xine_get_pos_length (gtx->priv->stream, &pos_stream,
				&pos_time, &length_time);
		i++;
	}

	if (ret == FALSE)
		return -1;

	return pos_time;
}

gint
gtk_xine_get_stream_length (GtkXine *gtx)
{
	int length_time = 0;
	int pos_stream, pos_time;

	g_return_val_if_fail (gtx != NULL, 0);
	g_return_val_if_fail (GTK_IS_XINE (gtx), 0);
	g_return_val_if_fail (gtx->priv->xine != NULL, 0);

	xine_get_pos_length (gtx->priv->stream, &pos_stream,
			&pos_time, &length_time);

	return length_time;
}

gboolean
gtk_xine_is_playing (GtkXine *gtx)
{
	g_return_val_if_fail (gtx != NULL, 0);
	g_return_val_if_fail (GTK_IS_XINE (gtx), 0);
	g_return_val_if_fail (gtx->priv->xine != NULL, 0);

	if (gtx->priv->stream == NULL)
		return FALSE;

	return xine_get_status (gtx->priv->stream) == XINE_STATUS_PLAY;
}

gboolean
gtk_xine_is_seekable (GtkXine *gtx)
{
	g_return_val_if_fail (gtx != NULL, 0);
	g_return_val_if_fail (GTK_IS_XINE (gtx), 0);
	g_return_val_if_fail (gtx->priv->xine != NULL, 0);

	if (gtk_xine_get_stream_length (gtx) == 0)
		return FALSE;

	return xine_get_stream_info (gtx->priv->stream,
			XINE_STREAM_INFO_SEEKABLE);
}

void
gtk_xine_toggle_aspect_ratio (GtkXine *gtx)
{
	int tmp;

	g_return_if_fail (gtx != NULL);
	g_return_if_fail (GTK_IS_XINE (gtx));
	g_return_if_fail (gtx->priv->xine != NULL);

	tmp = xine_get_param (gtx->priv->stream, XINE_PARAM_VO_ASPECT_RATIO);
	xine_set_param (gtx->priv->stream, XINE_PARAM_VO_ASPECT_RATIO, tmp + 1);
}

static gboolean
gtk_xine_ratio_fits_screen (GtkXine *gtx, gfloat ratio)
{
	int new_w, new_h;

	g_return_val_if_fail (gtx != NULL, FALSE);
	g_return_val_if_fail (GTK_IS_XINE (gtx), FALSE);
	g_return_val_if_fail (gtx->priv->xine != NULL, FALSE);

	new_w = gtx->priv->video_width * ratio;
	new_h = gtx->priv->video_height * ratio;

	if (new_w > (gdk_screen_width () - 128) ||
			new_h > (gdk_screen_height () - 128))
	{
		return FALSE;
	}

	return TRUE;
}

void
gtk_xine_set_scale_ratio (GtkXine *gtx, gfloat ratio)
{
	GtkWindow *toplevel;
	int new_w, new_h;

	g_return_if_fail (gtx != NULL);
	g_return_if_fail (GTK_IS_XINE (gtx));
	g_return_if_fail (gtx->priv->xine != NULL);
	g_return_if_fail (ratio >= 0);

	if (gtx->priv->fullscreen_mode == TRUE)
		return;

	/* Try best fit for the screen */
	if (ratio == 0)
	{
		if (gtk_xine_ratio_fits_screen (gtx, 2) == TRUE)
			ratio = 2;
		else if (gtk_xine_ratio_fits_screen (gtx, 1) == TRUE)
			ratio = 1;
		else if (gtk_xine_ratio_fits_screen (gtx, 0.5) == TRUE)
			ratio = 0.5;
		else
			return;
	} else {
		/* don't scale to something bigger than the screen, and leave
		 * us some room */
		if (gtk_xine_ratio_fits_screen (gtx, ratio) == FALSE)
			return;
	}

	new_w = gtx->priv->video_width * ratio;
	new_h = gtx->priv->video_height * ratio;

	toplevel = GTK_WINDOW (gtk_widget_get_toplevel (GTK_WIDGET (gtx)));

	gtk_window_set_resizable (toplevel, FALSE);
	gtx->widget.allocation.width = new_w;
	gtx->widget.allocation.height = new_h;
	gtk_widget_set_size_request (gtk_widget_get_parent (GTK_WIDGET (gtx)),
			new_w, new_h);
	gtk_widget_queue_resize (gtk_widget_get_parent (GTK_WIDGET (gtx)));
	while (gtk_events_pending ())
		gtk_main_iteration ();
	gtk_window_set_resizable (toplevel, TRUE);
}
