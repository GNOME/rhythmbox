/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*- */
/*
 * arch-tag: Interface to audio recorder backend  
 *
 * Copyright (C) 2004 William Jon McCann <mccann@jhu.edu>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifndef __RB_RECORDER_H__
#define __RB_RECORDER_H__

#include <glib-object.h>

G_BEGIN_DECLS

typedef enum
{
        RB_RECORDER_ERROR_NO_INPUT_PLUGIN,
        RB_RECORDER_ERROR_NO_TYPEFIND_PLUGIN,
        RB_RECORDER_ERROR_NO_DEMUX_PLUGIN,
        RB_RECORDER_ERROR_NO_AUDIO,
        RB_RECORDER_ERROR_GENERAL,
        RB_RECORDER_ERROR_INTERNAL
} RBRecorderError;

typedef enum {
        RB_RECORDER_ACTION_UNKNOWN,
        RB_RECORDER_ACTION_FILE_CONVERTING,
        RB_RECORDER_ACTION_DISC_PREPARING_WRITE,
        RB_RECORDER_ACTION_DISC_WRITING,
        RB_RECORDER_ACTION_DISC_FIXATING,
        RB_RECORDER_ACTION_DISC_BLANKING
} RBRecorderAction;

typedef enum {
	RB_RECORDER_RESPONSE_NONE   =  0,
	RB_RECORDER_RESPONSE_CANCEL = -1,
	RB_RECORDER_RESPONSE_ERASE  = -2,
	RB_RECORDER_RESPONSE_RETRY  = -3
} RBRecorderResponse;

typedef enum {
	RB_RECORDER_RESULT_ERROR,
	RB_RECORDER_RESULT_CANCEL,
	RB_RECORDER_RESULT_FINISHED,
	RB_RECORDER_RESULT_RETRY
} RBRecorderResult;

#define RB_RECORDER_ERROR rb_recorder_error_quark ()

GQuark rb_recorder_error_quark (void);

#define RB_TYPE_RECORDER            (rb_recorder_get_type ())
#define RB_RECORDER(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), RB_TYPE_RECORDER, RBRecorder))
#define RB_RECORDER_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), RB_TYPE_RECORDER, RBRecorderClass))
#define RB_IS_RECORDER(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), RB_TYPE_RECORDER))
#define RB_IS_RECORDER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), RB_TYPE_RECORDER))
#define RB_RECORDER_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), RB_TYPE_RECORDER, RBRecorderClass))

typedef struct _RBRecorderPrivate RBRecorderPrivate;

typedef struct _RBRecorder
{
        GObject            parent;

        RBRecorderPrivate *priv;
} RBRecorder;

typedef struct
{
        GObjectClass parent_class;
} RBRecorderClass;


GType        rb_recorder_get_type           (void);

RBRecorder * rb_recorder_new                (GError    **error);

void         rb_recorder_open               (RBRecorder *recorder,
                                             const char *uri,
                                             const char *cdtext,
                                             GError    **error);

gboolean     rb_recorder_opened             (RBRecorder *recorder);

void         rb_recorder_close              (RBRecorder *recorder,
                                             GError    **error);

void         rb_recorder_write              (RBRecorder *recorder,
                                             GError    **error);
void         rb_recorder_pause              (RBRecorder *recorder,
                                             GError    **error);

char *       rb_recorder_get_default_device (void);

char *       rb_recorder_get_device         (RBRecorder *recorder,
                                             GError    **error);

gboolean     rb_recorder_set_device         (RBRecorder *recorder,
                                             const char *device,
                                             GError    **error);

void         rb_recorder_set_tmp_dir        (RBRecorder *recorder,
                                             const char *path,
                                             GError    **error);

gint64       rb_recorder_get_media_length   (RBRecorder *recorder,
                                             GError    **error);
        
int          rb_recorder_burn               (RBRecorder *recorder,
                                             int         speed,
                                             GError    **error);
int          rb_recorder_burn_cancel        (RBRecorder *recorder);

gboolean     rb_recorder_enabled            (void);

G_END_DECLS

#endif /* __RB_RECORDER_H__ */
