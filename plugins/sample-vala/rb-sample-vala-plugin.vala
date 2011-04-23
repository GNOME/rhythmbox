using GLib;
using RB;

class SampleValaPlugin: Peas.ExtensionBase, Peas.Activatable {
	public override void activate () {
		stdout.printf ("Hello world\n");
	}

	public override void deactivate () {
		stdout.printf ("Goodbye world\n");
	}
}


[ModuleInit]
public void peas_register_types (GLib.TypeModule module) {
{
	var objmodule = module as Peas.ObjectModule;
	stdout.printf ("Registering plugin %s\n", "SampleValaPlugin");

	objmodule.register_extension_type (typeof (Peas.Activatable), typeof (SampleValaPlugin));
}
