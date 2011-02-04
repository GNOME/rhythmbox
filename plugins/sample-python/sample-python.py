
import gobject
import rb
from gi.repository import RB

class SamplePython(RB.Plugin):

	def __init__(self):
		RB.Plugin.__init__(self)
			
	def activate(self, shell):
		print "activating sample python plugin"

		db = shell.get_property("db")
		model = db.query_model_new_empty()
		self.source = gobject.new (PythonSource, shell=shell, name=_("Python Source"), query_model=model)
		shell.append_source(self.source, None)
	
	def deactivate(self, shell):
		print "deactivating sample python plugin"
		self.source.delete_thyself()
		self.source = None


class PythonSource(RB.Source):
	def __init__(self):
		RB.Source.__init__(self)
		
gobject.type_register(PythonSource)
