import rhythmdb, rb
import gobject, gtk
import louie


class CoherencePlugin(rb.Plugin):
	def __init__(self):
		rb.Plugin.__init__(self)
			
	def activate(self, shell):
		from twisted.internet import gtk2reactor
		try:
			gtk2reactor.install()
		except AssertionError, e:
			# sometimes it's already installed
			print e

		self.coherence = self.get_coherence()
		if self.coherence is None:
			print "Coherence is not installed or too old, aborting"
			return

		print "coherence UPnP plugin activated"
		self.shell = shell
		self.sources = {}

		# watch for media servers
		louie.connect(self.detected_media_server,
				'Coherence.UPnP.ControlPoint.MediaServer.detected',
				louie.Any)
		louie.connect(self.removed_media_server,
				'Coherence.UPnP.ControlPoint.MediaServer.removed',
				louie.Any)

		# create our own media server
		from coherence.upnp.devices.media_server import MediaServer
		from MediaStore import MediaStore
		server = MediaServer(self.coherence, MediaStore, no_thread_needed=True, db=self.shell.props.db, plugin=self)

	def deactivate(self, shell):
		print "coherence UPnP plugin deactivated"
		if self.coherence is None:
			return

		self.coherence.shutdown()

		louie.disconnect(self.detected_media_server,
				'Coherence.UPnP.ControlPoint.MediaServer.detected',
				louie.Any)
		louie.disconnect(self.removed_media_server,
				'Coherence.UPnP.ControlPoint.MediaServer.removed',
				louie.Any)

		del self.shell
		del self.coherence

		for usn, source in self.sources.iteritems():
			source.delete_thyself()
		del self.sources

		# uninstall twisted reactor? probably not, since other thigngs may have used it


	def get_coherence (self):
		coherence_instance = None
		required_version = (0, 3, 2)

		try:
			from coherence.base import Coherence
			from coherence import __version_info__
		except ImportError, e:
			print "Coherence not found"
			return None

		if __version_info__ < required_version:
			required = '.'.join([str(i) for i in required_version])
			found = '.'.join([str(i) for i in __version_info__])
			print "Coherence %s required. %s found. Please upgrade" % (required, found)
			return None

		coherence_config = {
			#'logmode': 'info',
			'controlpoint': 'yes',
			'plugins':{}
		}
		coherence_instance = Coherence(coherence_config)

		return coherence_instance


	def removed_media_server(self, usn):
		print "upnp server went away %s" % usn
		if self.sources.has_key(usn):
			self.sources[usn].delete_thyself()
			del self.sources[usn]

	def detected_media_server(self, client, usn):
		print "found upnp server %s (%s)"  %  (client.device.get_friendly_name(), usn)

		db = self.shell.props.db
		group = rb.rb_source_group_get_by_name ("shared")
		entry_type = db.entry_register_type("CoherenceUpnp:" + usn)

		from UpnpSource import UpnpSource
		source = gobject.new (UpnpSource,
					shell=self.shell,
					entry_type=entry_type,
					source_group=group,
					plugin=self,
					client=client,
					usn=usn)

		self.sources[usn] = source

		self.shell.append_source (source, None)

