from gi.repository import GObject, Peas
from gi.repository import RB

class SamplePython(GObject.Object, Peas.Activatable):
	__gtype_name = 'SamplePythonPlugin'

	def __init__(self):
		GObject.Object.__init__(self)
			
	def do_activate(self):
		print "activating sample python plugin"

		shell = self.object
		db = shell.props.db
		model = db.query_model_new_empty()
		self.source = gobject.new (PythonSource, shell=shell, name=_("Python Source"), query_model=model)
		shell.append_source(self.source, None)
	
	def do_deactivate(self):
		print "deactivating sample python plugin"
		self.source.delete_thyself()
		self.source = None

class PythonSource(RB.Source):
	def __init__(self):
		RB.Source.__init__(self)

GObject.type_register_dynamic(PythonSource)
