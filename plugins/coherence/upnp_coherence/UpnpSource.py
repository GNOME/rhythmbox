import rb, rhythmdb
import gobject, gtk

class UpnpSource(rb.BrowserSource):
	__gproperties__ = {
		'plugin': (rb.Plugin, 'plugin', 'plugin', gobject.PARAM_WRITABLE|gobject.PARAM_CONSTRUCT_ONLY),
		'client': (gobject.TYPE_PYOBJECT, 'client', 'client', gobject.PARAM_WRITABLE|gobject.PARAM_CONSTRUCT_ONLY),
		'usn': (gobject.TYPE_PYOBJECT, 'usn', 'usn', gobject.PARAM_WRITABLE|gobject.PARAM_CONSTRUCT_ONLY),
	}

	def __init__(self):
		rb.BrowserSource.__init__(self)
		self.__db = None
		self.__activated = False


	def do_set_property(self, property, value):
		if property.name == 'plugin':
			self.__plugin = value
		elif property.name == 'client':
			self.__client = value
			self.props.name = self.__client.device.get_friendly_name()
		elif property.name == 'usn':
			self.__usn = value
		else:
			raise AttributeError, 'unknown property %s' % property.name


	def do_impl_activate(self):
		if not self.__activated:
			print "activating upnp source"
			self.__activated = True

			shell = self.get_property('shell')
			self.__db = shell.get_property('db')
			self.__entry_type = self.get_property('entry-type')

			# load upnp db
			self.load_db(0)
			self.__client.content_directory.subscribe_for_variable('ContainerUpdateIDs', self.state_variable_change)
			self.__client.content_directory.subscribe_for_variable('SystemUpdateID', self.state_variable_change)


	def load_db(self, id):
		d = self.__client.content_directory.browse(id, browse_flag='BrowseDirectChildren', backward_compatibility=False)
		d.addCallback(self.process_media_server_browse, self.__usn)


	def state_variable_change(self, variable, usn):
		print "%s changed from %s to %s" % (variable.name, variable.old_value, variable.value)
		if variable.old_value == '':
			return

		if variable.name == 'SystemUpdateID':
			self.load_db(0)
		elif variable.name == 'ContainerUpdateIDs':
			changes = variable.value.split(',')
			while len(changes) > 1:
				container = changes.pop(0).strip()
				update_id = changes.pop(0).strip()
				if container in self.container_watch:
					print "we have a change in %s, container needs a reload" % container
					self.load_db(container)


	def process_media_server_browse(self, results, usn):
		for k,v in results.iteritems():
			if k == 'items':
				for id, values in v.iteritems():
					if values['upnp_class'].startswith('object.container'):
						self.load_db(id)
					if values['upnp_class'].startswith('object.item.audioItem'):
						# (url, [method, something which is in asterix,  format, semicolon delimited key=value map of something])
						resources = [(k, v.split(':')) for (k, v) in values['resources'].iteritems()]
						# break data into map
						for r in resources:
							if r[1][3] is not '*':
								r[1][3] = dict([v.split('=') for v in r[1][3].split(';')])
							else:
								r[1][3] = dict()

						url = None
						for r in resources:
							if r[1][3].has_key('DLNA.ORG_CI') and r[1][3]['DLNA.ORG_CI'] is not '1':
								url = r[0]
								break

						if url is None:
							# use transcoded format, since we can't find a normal one
							url = resources[0][0]

						entry = self.__db.entry_lookup_by_location (url)
						if entry == None:
							entry = self.__db.entry_new(self.__entry_type, url)

						self.__db.set(entry, rhythmdb.PROP_TITLE, values['title'])
						
						self.__db.commit()

gobject.type_register(UpnpSource)

