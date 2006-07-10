import sys

# rb classes
from Loader import Loader
from Coroutine import Coroutine

def _excepthandler (exc_class, exc_inst, trace):
	import sys
	# print out stuff
	sys.__excepthook__ (exc_class, exc_inst, trace)


class _rbdebugfile:
	def __init__(self, fn):
		self.fn = fn

	def write(self, str):
		import sys, rb
		fr = sys._getframe(1)
		co = fr.f_code
		rb._debug (co.co_name, co.co_filename, co.co_firstlineno + fr.f_lineno,  False, str)

	def close(self):         pass
	def flush(self):         pass
	def fileno(self):        return self.fn
	def isatty(self):        return 0
	def read(self, a):       return ''
	def readline(self):      return ''
	def readlines(self):     return []
	writelines = write
	def seek(self, a):       raise IOError, (29, 'Illegal seek')
	def tell(self):          raise IOError, (29, 'Illegal seek')
	truncate = tell

sys.stdout = _rbdebugfile(sys.stdout.fileno())
sys.excepthook = _excepthandler

