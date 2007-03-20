/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  Copyright (C) 2004 Benjamin Otte <otte@gnome.org>
 *  Copyright (C) 2006 Jonathan Matthew
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA.
 */

/* fake visualizer element for evil rhythmbox purposes */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>

#include <gst/gst.h>
#include <gst/base/gstadapter.h>
#include <gst/video/video.h>
#include <gst/audio/audio.h>

#define RB_TYPE_FAKE_VIS (rb_fake_vis_get_type())
#define RB_IS_FAKE_VIS(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj),RB_TYPE_FAKE_VIS))
#define RB_FAKE_VIS(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj),RB_TYPE_FAKE_VIS,RBFakeVis))
#define RB_IS_FAKE_VIS_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass),RB_TYPE_FAKE_VIS))
#define RB_FAKE_VIS_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass),RB_TYPE_FAKE_VIS,RBFakeVisClass))
#define RB_FAKE_VIS_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS ((obj), RB_TYPE_FAKE_VIS, RBFakeVisClass))

typedef struct _RBFakeVis RBFakeVis;
typedef struct _RBFakeVisClass RBFakeVisClass;

GST_DEBUG_CATEGORY_STATIC (rb_fake_vis_debug);
#define GST_CAT_DEFAULT (rb_fake_vis_debug)

struct _RBFakeVis
{
  GstElement element;

  /* pads */
  GstPad *sinkpad;
  GstPad *srcpad;
  GstClockTime next_ts;
  GstSegment segment;

  /* audio/video state */
  gint channels;
  gint rate;                    /* Input samplerate */
  gint bps;

  /* framerate numerator & denominator */
  gint fps_n;
  gint fps_d;
  gint width;
  gint height;
  gint depth;
  GstClockTime duration;

  /* samples per frame based on caps */
  guint spf;

  /* state stuff */
  /*GstAdapter *adapter;*/
  guint avail;
  gboolean first_frame;

  /* QoS stuff *//* with LOCK */
  gdouble proportion;
  GstClockTime earliest_time;
};

struct _RBFakeVisClass
{
  GstElementClass parent_class;
};

GType rb_fake_vis_get_type (void);


static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_xRGB_HOST_ENDIAN)
    );

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-int, "
        "width = (int) 16, "
        "depth = (int) 16, "
        "endianness = (int) BYTE_ORDER, "
        "signed = (boolean) TRUE, "
        "channels = (int) { 1, 2 }, " "rate = (int) [ 1000, MAX ]")
    );

static GstStateChangeReturn rb_fake_vis_change_state (GstElement * element,
    GstStateChange transition);
static GstFlowReturn rb_fake_vis_chain (GstPad * pad, GstBuffer * buffer);
static gboolean rb_fake_vis_sink_event (GstPad * pad, GstEvent * event);
static gboolean rb_fake_vis_src_event (GstPad * pad, GstEvent * event);

static gboolean rb_fake_vis_sink_setcaps (GstPad * pad, GstCaps * caps);
static gboolean rb_fake_vis_src_setcaps (GstPad * pad, GstCaps * caps);
static GstCaps *rb_fake_vis_getcaps (GstPad * pad);

static GstElementDetails rb_fake_vis_details =
	GST_ELEMENT_DETAILS ("RB fake visualizer",
			     "Visualization",
			     "pretend to generate visualization from audio input",
			     "Benjamin Otte <otte@gnome.org>, Jonathan Matthew <jonathan@kaolin.wh9.net>");

static void
_do_init (GType fake_vis_type)
{
  GST_DEBUG_CATEGORY_INIT (rb_fake_vis_debug,
			   "fakevis", GST_DEBUG_FG_WHITE,
			   "Rhythmbox built-in fake visualizer");
}

GST_BOILERPLATE_FULL (RBFakeVis, rb_fake_vis, GstElement, GST_TYPE_ELEMENT, _do_init);

static void
rb_fake_vis_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);
  gst_element_class_set_details (element_class, &rb_fake_vis_details);
  gst_element_class_add_pad_template (element_class,
				      gst_static_pad_template_get (&src_template));
  gst_element_class_add_pad_template (element_class,
				      gst_static_pad_template_get (&sink_template));
}

static void
rb_fake_vis_class_init (RBFakeVisClass *klass)
{
  GstElementClass *element = GST_ELEMENT_CLASS (klass);
  element->change_state = rb_fake_vis_change_state;
}

static void
rb_fake_vis_init (RBFakeVis * visual, RBFakeVisClass *klass)
{
  /* create the sink and src pads */
  visual->sinkpad = gst_pad_new_from_static_template (&sink_template, "sink");
  gst_pad_set_setcaps_function (visual->sinkpad, rb_fake_vis_sink_setcaps);
  gst_pad_set_chain_function (visual->sinkpad, rb_fake_vis_chain);
  gst_pad_set_event_function (visual->sinkpad, rb_fake_vis_sink_event);
  gst_element_add_pad (GST_ELEMENT (visual), visual->sinkpad);

  visual->srcpad = gst_pad_new_from_static_template (&src_template, "src");
  gst_pad_set_setcaps_function (visual->srcpad, rb_fake_vis_src_setcaps);
  gst_pad_set_getcaps_function (visual->srcpad, rb_fake_vis_getcaps);
  gst_pad_set_event_function (visual->srcpad, rb_fake_vis_src_event);
  gst_element_add_pad (GST_ELEMENT (visual), visual->srcpad);
}

static void
rb_fake_vis_reset (RBFakeVis * visual)
{
  visual->next_ts = -1;
  visual->avail = 0;
  gst_segment_init (&visual->segment, GST_FORMAT_UNDEFINED);

  GST_OBJECT_LOCK (visual);
  visual->proportion = 1.0;
  visual->earliest_time = -1;
  visual->first_frame = FALSE;
  GST_OBJECT_UNLOCK (visual);
}

static gboolean
rb_fake_vis_src_setcaps (GstPad * pad, GstCaps * caps)
{
  RBFakeVis *visual = RB_FAKE_VIS (gst_pad_get_parent (pad));
  GstStructure *structure;

  structure = gst_caps_get_structure (caps, 0);

  GST_DEBUG_OBJECT (visual, "src pad got caps %" GST_PTR_FORMAT, caps);

  if (!gst_structure_get_int (structure, "width", &visual->width))
    goto error;
  if (!gst_structure_get_int (structure, "height", &visual->height))
    goto error;
  if (!gst_structure_get_int (structure, "bpp", &visual->depth))
    goto error;
  if (!gst_structure_get_fraction (structure, "framerate", &visual->fps_n,
          &visual->fps_d))
    goto error;

  visual->spf =
      gst_util_uint64_scale_int (visual->rate, visual->fps_d, visual->fps_n);
  visual->duration =
      gst_util_uint64_scale_int (GST_SECOND, visual->fps_d, visual->fps_n);

  gst_object_unref (visual);
  return TRUE;

  /* ERRORS */
error:
  {
    GST_DEBUG_OBJECT (visual, "error parsing caps");
    gst_object_unref (visual);
    return FALSE;
  }
}


static gboolean
rb_fake_vis_sink_setcaps (GstPad * pad, GstCaps * caps)
{
  RBFakeVis *visual = RB_FAKE_VIS (gst_pad_get_parent (pad));
  GstStructure *structure;

  structure = gst_caps_get_structure (caps, 0);

  gst_structure_get_int (structure, "channels", &visual->channels);
  gst_structure_get_int (structure, "rate", &visual->rate);

  if (visual->fps_n != 0) {
    visual->spf =
        gst_util_uint64_scale_int (visual->rate, visual->fps_d, visual->fps_n);
  }
  visual->bps = visual->channels * sizeof (gint16);

  gst_object_unref (visual);
  return TRUE;
}


static GstCaps *
rb_fake_vis_getcaps (GstPad * pad)
{
  GstCaps *ret;
  RBFakeVis *visual = RB_FAKE_VIS (gst_pad_get_parent (pad));

  ret = gst_caps_copy (gst_pad_get_pad_template_caps (visual->srcpad));

  GST_DEBUG_OBJECT (visual, "returning caps %" GST_PTR_FORMAT, ret);
  gst_object_unref (visual);
  return ret;
}


static gboolean
rb_fake_vis_src_negotiate (RBFakeVis * visual)
{
  GstCaps *othercaps, *target, *intersect;
  GstStructure *structure;
  const GstCaps *templ;

  templ = gst_pad_get_pad_template_caps (visual->srcpad);

  /* see what the peer can do */
  othercaps = gst_pad_peer_get_caps (visual->srcpad);
  if (othercaps) {
    intersect = gst_caps_intersect (othercaps, templ);
    gst_caps_unref (othercaps);

    if (gst_caps_is_empty (intersect))
      goto no_format;

    target = gst_caps_copy_nth (intersect, 0);
    gst_caps_unref (intersect);
  } else {
    target = gst_caps_ref ((GstCaps *) templ);
  }

  structure = gst_caps_get_structure (target, 0);
  gst_structure_fixate_field_nearest_int (structure, "width", 1);
  gst_structure_fixate_field_nearest_int (structure, "height", 1);
  gst_structure_fixate_field_nearest_fraction (structure, "framerate", 1, 1);

  gst_pad_set_caps (visual->srcpad, target);
  gst_caps_unref (target);

  return TRUE;

  /* ERRORS */
no_format:
  {
    GST_ELEMENT_ERROR (visual, STREAM, FORMAT, (NULL),
        ("could not negotiate output format"));
    gst_caps_unref (intersect);
    return FALSE;
  }
}

static gboolean
rb_fake_vis_sink_event (GstPad * pad, GstEvent * event)
{
  RBFakeVis *visual;
  gboolean res;

  visual = RB_FAKE_VIS (gst_pad_get_parent (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_START:
      res = gst_pad_push_event (visual->srcpad, event);
      break;
    case GST_EVENT_FLUSH_STOP:
      rb_fake_vis_reset (visual);
      res = gst_pad_push_event (visual->srcpad, event);
      break;
    case GST_EVENT_NEWSEGMENT:
    {
      GstFormat format;
      gdouble rate, arate;
      gint64 start, stop, time;
      gboolean update;

      /* the newsegment values are used to clip the input samples
       * and to convert the incoming timestamps to running time so
       * we can do QoS */
      gst_event_parse_new_segment_full (event, &update, &rate, &arate, &format,
          &start, &stop, &time);

      /* now configure the values */
      gst_segment_set_newsegment_full (&visual->segment, update,
          rate, arate, format, start, stop, time);

      /* and forward */
      res = gst_pad_push_event (visual->srcpad, event);
      break;
    }
    default:
      res = gst_pad_push_event (visual->srcpad, event);
      break;
  }

  gst_object_unref (visual);
  return res;
}

static gboolean
rb_fake_vis_src_event (GstPad * pad, GstEvent * event)
{
  RBFakeVis *visual;
  gboolean res;

  visual = RB_FAKE_VIS (gst_pad_get_parent (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_QOS:
    {
      gdouble proportion;
      GstClockTimeDiff diff;
      GstClockTime timestamp;

      gst_event_parse_qos (event, &proportion, &diff, &timestamp);

      /* save stuff for the _chain function */
      GST_OBJECT_LOCK (visual);
      visual->proportion = proportion;
      if (diff >= 0)
        /* we're late, this is a good estimate for next displayable
         * frame (see part-qos.txt) */
        visual->earliest_time = timestamp + 2 * diff + visual->duration;
      else
        visual->earliest_time = timestamp + diff;

      GST_OBJECT_UNLOCK (visual);

      res = gst_pad_push_event (visual->sinkpad, event);
      break;
    }
    default:
      res = gst_pad_push_event (visual->sinkpad, event);
      break;
  }

  gst_object_unref (visual);
  return res;
}

/* allocate and output buffer, if no format was negotiated, this
 * function will negotiate one. After calling this function, a
 * reverse negotiation could have happened. */
static GstFlowReturn
get_buffer (RBFakeVis * visual, GstBuffer ** outbuf)
{
  GstFlowReturn ret;
  guint outsize;

  /* we don't know an output format yet, pick one */
  if (GST_PAD_CAPS (visual->srcpad) == NULL) {
    if (!rb_fake_vis_src_negotiate (visual))
      return GST_FLOW_NOT_NEGOTIATED;
  }

  outsize = visual->height * visual->width * (visual->depth / 8);

  GST_DEBUG_OBJECT (visual, "allocating output buffer with caps %"
      GST_PTR_FORMAT, GST_PAD_CAPS (visual->srcpad));

  /* now allocate a buffer with the last negotiated format.
   * Downstream could renegotiate a new format, which will trigger
   * our setcaps function on the source pad. */
  ret =
      gst_pad_alloc_buffer_and_set_caps (visual->srcpad,
      GST_BUFFER_OFFSET_NONE, outsize,
      GST_PAD_CAPS (visual->srcpad), outbuf);

  /* no buffer allocated, we don't care why. */
  if (ret != GST_FLOW_OK)
    return ret;

  /* this is bad and should not happen. When the alloc function
   * returns _OK, core ensures we have a valid buffer. */
  if (*outbuf == NULL)
    return GST_FLOW_ERROR;

  memset (GST_BUFFER_DATA (*outbuf), 0, outsize);

  return GST_FLOW_OK;
}

static GstFlowReturn
rb_fake_vis_chain (GstPad * pad, GstBuffer * buffer)
{
  GstBuffer *outbuf = NULL;
  RBFakeVis *visual = RB_FAKE_VIS (gst_pad_get_parent (pad));
  GstFlowReturn ret = GST_FLOW_OK;

  GST_DEBUG_OBJECT (visual, "chain function called");

  /* If we don't have an output format yet, preallocate a buffer to try and
   * set one */
  if (GST_PAD_CAPS (visual->srcpad) == NULL) {
    GST_DEBUG_OBJECT (visual, "calling buffer alloc to set caps");
    ret = get_buffer (visual, &outbuf);
    if (ret != GST_FLOW_OK) {
      GST_DEBUG_OBJECT (visual, "couldn't allocate buffer: %s", gst_flow_get_name (ret));
      gst_buffer_unref (buffer);
      goto beach;
    }
  }

  /* resync on DISCONT */
  if (GST_BUFFER_FLAG_IS_SET (buffer, GST_BUFFER_FLAG_DISCONT)) {
    visual->avail = 0;
    visual->next_ts = -1;
  }

  /* Try to push a frame as soon as possible to avoid stalling the pipeline */
  if (visual->first_frame == FALSE) {
    if (outbuf == NULL) {
      ret = get_buffer (visual, &outbuf);
      if (ret != GST_FLOW_OK) {
	goto beach;
      }
    }
    GST_BUFFER_TIMESTAMP (outbuf) = GST_BUFFER_TIMESTAMP (buffer);
    ret = gst_pad_push (visual->srcpad, outbuf);
    if (ret != GST_FLOW_OK) {
      goto beach;
    }
    outbuf = NULL;
    visual->first_frame = TRUE;
  }

  /* Match timestamps from the incoming audio */
  if (GST_BUFFER_TIMESTAMP (buffer) != GST_CLOCK_TIME_NONE)
    visual->next_ts = GST_BUFFER_TIMESTAMP (buffer);

  GST_DEBUG_OBJECT (visual,
      "Input buffer has %d samples, time=%" G_GUINT64_FORMAT,
      GST_BUFFER_SIZE (buffer) / visual->bps, GST_BUFFER_TIMESTAMP (buffer));

  visual->avail += GST_BUFFER_SIZE (buffer);
  gst_buffer_unref (buffer);

  while (visual->avail > MAX (512, visual->spf) * visual->bps) {
    gboolean need_skip;

    GST_DEBUG_OBJECT (visual, "processing buffer (%u avail)", visual->avail);

    if (visual->next_ts != -1) {
      gint64 qostime;

      /* QoS is done on running time */
      qostime = gst_segment_to_running_time (&visual->segment, GST_FORMAT_TIME,
          visual->next_ts);

      GST_OBJECT_LOCK (visual);
      /* check for QoS, don't compute buffers that are known to be late */
      need_skip = visual->earliest_time != -1 &&
          qostime <= visual->earliest_time;
      GST_OBJECT_UNLOCK (visual);

      if (need_skip) {
        GST_WARNING_OBJECT (visual,
            "QoS: skip ts: %" GST_TIME_FORMAT ", earliest: %" GST_TIME_FORMAT,
            GST_TIME_ARGS (qostime), GST_TIME_ARGS (visual->earliest_time));
        goto skip;
      }
    }
    /* alloc a buffer if we don't have one yet, this happens
     * when we pushed a buffer in this while loop before */
    if (outbuf == NULL) {
      ret = get_buffer (visual, &outbuf);
      if (ret != GST_FLOW_OK) {
        goto beach;
      }
    }

    GST_BUFFER_TIMESTAMP (outbuf) = visual->next_ts;
    GST_BUFFER_DURATION (outbuf) = visual->duration;

    ret = gst_pad_push (visual->srcpad, outbuf);
    outbuf = NULL;

    GST_DEBUG_OBJECT (visual, "finished frame, flushing %u samples from input",
        visual->spf);
  skip:
    /* interpolate next timestamp */
    if (visual->next_ts != -1)
      visual->next_ts += visual->duration;

    /* Flush out the number of samples per frame * channels * sizeof (gint16) */
    if (visual->avail < visual->spf * visual->bps)
      visual->avail = 0;
    else
      visual->avail -= visual->spf * visual->bps;

    /* quit the loop if something was wrong */
    if (ret != GST_FLOW_OK)
      break;
  }

  if (outbuf != NULL)
    gst_buffer_unref (outbuf);

beach:
  gst_object_unref (visual);

  GST_DEBUG_OBJECT (visual, "leaving chain function");
  return ret;
}

static GstStateChangeReturn
rb_fake_vis_change_state (GstElement * element, GstStateChange transition)
{
  RBFakeVis *visual = RB_FAKE_VIS (element);
  GstStateChangeReturn ret;

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      rb_fake_vis_reset (visual);
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      break;
    default:
      break;
  }

  return ret;
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "rbfakevis", GST_RANK_NONE, RB_TYPE_FAKE_VIS);
}

GST_PLUGIN_DEFINE_STATIC (GST_VERSION_MAJOR, GST_VERSION_MINOR,
			  "rbfakevis",
			  "fake visualizer",
			  plugin_init,
			  VERSION,
			  "GPL",
			  PACKAGE,
			  "");

