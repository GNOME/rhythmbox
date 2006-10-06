import gnomevfs
import xml.sax, xml.sax.handler

class BuyAlbumHandler(xml.sax.handler.ContentHandler): # Class to download the track, etc.
	
	format_map =	{
			'ogg'		:	'URL_OGGZIP',
			'flac'		:	'URL_FLACZIP',
			'wav'		:	'URL_WAVZIP',
			'mp3-cbr'	:	'URL_128KMP3ZIP',
			'mp3-vbr'	:	'URL_VBRZIP'
			}
	
	def __init__(self, format):
		xml.sax.handler.ContentHandler.__init__(self)
		self._format_tag = self.format_map[format] # format of audio to download
	
	def startElement(self, name, attrs):
		self._text = ""
	
	def endElement(self, name):
		if name == "ERROR": # Something went wrong. Display error message to user.
			raise MagnatunePurchaseError(self._text)
		elif name == "DL_USERNAME":
			self.username = self._text
		elif name == "DL_PASSWORD":
			self.password = self._text
		elif name == self._format_tag:
			self.url = self._text
	
	def characters(self, content):
		self._text = self._text + content

class MagnatunePurchaseError(Exception):
	pass
