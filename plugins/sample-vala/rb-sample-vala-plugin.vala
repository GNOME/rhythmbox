using GLib;
using RB;

class SampleValaPlugin: RB.Plugin {
	public override void activate (RB.Shell shell) {
		stdout.printf ("Hello world\n");
	}

	public override void deactivate (RB.Shell shell) {
		stdout.printf ("Goodbye world\n");
	}
}


[ModuleInit]
public GLib.Type register_rb_plugin (GLib.TypeModule module)
{
	stdout.printf ("Registering plugin %s\n", "SampleValaPlugin");

	return typeof (SampleValaPlugin);
}
