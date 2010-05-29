[Import ()]
[CCode (cprefix = "RB", lower_case_cprefix = "rb_")]

namespace RB {
	[CCode (cheader_filename = "rb-shell.h")]
	public class Shell {
		[CCode (array_length = false)]
		[CCode (cname = "rb_shell_get_type")]
		public static GLib.Type get_type ();

		[CCode (array_length = false)]
		[CCode (cname = "rb_shell_get_player")]
		public unowned ShellPlayer get_player ();

		[CCode (array_length = false)]
		[CCode (cname = "rb_shell_get_ui_manager")]
		public unowned GLib.Object get_ui_manager();
	}

	[CCode (cheader_filename = "rb-plugin.h")]
	public abstract class Plugin : GLib.Object {
		[CCode (array_length = false)]
		[CCode (cname = "rb_plugin_get_type")]
		public static GLib.Type get_type ();

		[CCode (array_length = false)]
		[CCode (cname = "rb_plugin_activate")]
		public abstract void activate (RB.Shell shell);

		[CCode (array_length = false)]
		[CCode (cname = "rb_plugin_deactivate")]
		public abstract void deactivate (RB.Shell shell);

		[CCode (array_length = false)]
		[CCode (cname = "rb_plugin_is_configurable")]
		public virtual bool is_configurable ();

		[CCode (array_length = false)]
		[CCode (cname = "rb_plugin_create_configure_dialog")]
		public virtual Gtk.Widget create_configure_dialog ();

		[CCode (array_length = false)]
		[CCode (cname = "rb_plugin_find_file")]
		public virtual unowned string find_file (string file);
	}

	[CCode (cheader_filename = "rb-player-gst-filter.h")]
	public interface PlayerGstFilter : GLib.Object {
		[CCode (array_length = false)]
		[CCode (cname = "rb_player_gst_filter_add_filter")]
		public virtual bool add_filter(Gst.Element e);

		[CCode (array_length = false)]
		[CCode (cname = "rb_player_gst_filter_remove_filter")]
		public virtual bool remove_filter(Gst.Element e);
	}

	[NoCompact, CCode (cheader_filename = "rb-shell-player.h")]
	public class ShellPlayer : Gtk.HBox {

		[CCode (array_length = false)]
		[CCode (cname = "rb_shell_player_pause")]
		public bool pause(ref GLib.Error? err = null);

		[CCode (array_length = false)]
		[CCode (cname = "rb_shell_player_play")]
		public bool play(ref GLib.Error? err = null);

		[CCode (array_length = false)]
		[CCode (cname = "rb_shell_player_stop")]
		public bool stop();

		[CCode (array_length = false)]
		[CCode (cname = "rb_shell_player_get_playing")]
		public bool get_playing(ref bool playing, ref GLib.Error? err = null);

		[CCode (array_length = false)]
		[CCode (cname = "rb_shell_player_do_next")]
		public bool do_next(ref GLib.Error? err = null);

		[CCode (array_length = false)]
		[CCode (cname = "rb_shell_player_do_previous")]
		public bool do_previous(ref GLib.Error? err = null);

		public virtual signal void playing_changed(bool playing);
	}

	[CCode (cheader_filename = "rb-player.h")]
	public interface Player : GLib.Object {
		[CCode (cname = "rb_player_opened")]
		public bool opened();
	}
}
