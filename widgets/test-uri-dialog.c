
#include "rb-uri-dialog.h"
#include "rb-file-helpers.h"

static void
location_added (RBURIDialog *dialog,
		const char  *uri,
		gpointer     user_data)
{
	g_message ("URI selected was: %s", uri);
}

int main (int argc, char **argv)
{
	GtkWidget *dialog;

	gtk_init (&argc, &argv);
	rb_file_helpers_init ();

	dialog = rb_uri_dialog_new ("Dialog title", "dialog label");
	g_signal_connect (G_OBJECT (dialog), "location-added",
			  G_CALLBACK (location_added), NULL);

	gtk_dialog_run (GTK_DIALOG (dialog));

	gtk_widget_destroy (dialog);

	return 0;
}
