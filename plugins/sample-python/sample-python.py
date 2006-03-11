import rb

class SamplePython(rb.Plugin):

	def __init__(self):
		rb.Plugin.__init__(self)
			
	def activate(self, shell):
		print "activating sample python plugin"
	
	def deactivate(self, shell):
		print "deactivating sample python plugin"
