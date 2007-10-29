/* -*- mode: C; c-basic-offset: 8 -*- */

#ifndef RB_RADIO_TUNER_H
#define RB_RADIO_TUNER_H

#include <glib-object.h>

G_BEGIN_DECLS

#define RB_TYPE_RADIO_TUNER         (rb_radio_tuner_get_type ())
#define RB_RADIO_TUNER(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_RADIO_TUNER, RBRadioTuner))
#define RB_RADIO_TUNER_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST ((k), RB_TYPE_RADIO_TUNER, RBRadioTunerClass))
#define RB_IS_RADIO_TUNER(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_RADIO_TUNER))
#define RB_IS_RADIO_TUNER_CLASS(o)  (G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_RADIO_TUNER))
#define RB_RADIO_TUNER_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_RADIO_TUNER, RBRadioTunerClass))

typedef struct _RBRadioTuner RBRadioTuner;
typedef struct _RBRadioTunerPrivate RBRadioTunerPrivate;
typedef struct _RBRadioTunerClass RBRadioTunerClass;

struct _RBRadioTuner {
	GObject parent;
	RBRadioTunerPrivate *priv;

	gchar *card_name;

	double frequency;
	double min_freq;
	double max_freq;

	guint32 signal;

	guint is_stereo : 1;
	guint is_muted : 1;
};

struct _RBRadioTunerClass {
	GObjectClass parent_class;

	void (* changed) (RBRadioTuner *self);
};

GType         rb_radio_tuner_get_type      (void);
GType         rb_radio_tuner_register_type (GTypeModule *module);

RBRadioTuner *rb_radio_tuner_new           (const gchar *devname,
					    GError **err);
void          rb_radio_tuner_update        (RBRadioTuner *self);
gboolean      rb_radio_tuner_set_frequency (RBRadioTuner *self,
					    double frequency);
gboolean      rb_radio_tuner_set_mute      (RBRadioTuner *self,
					    gboolean mute);

G_END_DECLS

#endif /* RB_RADIO_TUNER_H */
