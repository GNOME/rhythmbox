import sys

# rb classes
from Loader import Loader
from Coroutine import Coroutine

#def _excepthandler (exc_class, exc_inst, trace):
#	import sys
#	# print out stuff ignoring our debug redirect
#	sys.__excepthook__ (exc_class, exc_inst, trace)


def try_load_icon(theme, icon, size, flags):
	try:
		return theme.load_icon(icon, size, flags)
	except:
		return None

def append_plugin_source_path(theme, iconpath):
	# check for a Makefile.am in the dir the file was loaded from
	import sys, os
	fr = sys._getframe(1)
	co = fr.f_code
	filename = co.co_filename

	# and if found, append the icon path
	dir = filename[:filename.rfind(os.sep)]
	if os.path.exists(dir + "/Makefile.am"):
		plugindir = dir[:dir.rfind(os.sep)]
		icondir = plugindir + iconpath
		theme.append_search_path(icondir)

class _rbdebugfile:
	def __init__(self, fn):
		self.fn = fn

	def write(self, str):
		if str == '\n':
			return
		import sys, os, rb
		fr = sys._getframe(1)

		co = fr.f_code
		filename = co.co_filename

		# strip off the cwd, for if running uninstalled
		cwd = os.getcwd()
		if cwd[-1] != os.sep:
			cwd += os.sep
		if filename[:len(cwd)] == cwd:
			filename = filename[len(cwd):]

		# add the class name to the method, if 'self' exists
		methodname = co.co_name
		if fr.f_locals.has_key('self'):
			methodname = '%s.%s' % (fr.f_locals['self'].__class__.__name__, methodname)

		rb._debug (methodname, filename, co.co_firstlineno + fr.f_lineno,  True, str)

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
#sys.excepthook = _excepthandler

