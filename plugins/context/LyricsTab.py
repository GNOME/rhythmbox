import rb, rhythmdb
import gtk, gobject
import urllib
import re, os
from rb.stringmatch import string_match
from mako.template import Template
import LyricsParse

LYRIC_TITLE_STRIP=["\(live[^\)]*\)", "\(acoustic[^\)]*\)", "\([^\)]*mix\)", "\([^\)]*version\)", "\([^\)]*edit\)", "\(feat[^\)]*\)"]
LYRIC_TITLE_REPLACE=[("/", "-"), (" & ", " and ")]
LYRIC_ARTIST_REPLACE=[("/", "-"), (" & ", " and ")]

class LyricsTab (gobject.GObject) :
    
    __gsignals__ = {
        'switch-tab' : (gobject.SIGNAL_RUN_LAST, gobject.TYPE_NONE,
                                (gobject.TYPE_STRING,))
    }

    def __init__ (self, shell, toolbar, ds, view) :
        gobject.GObject.__init__ (self)
        self.shell      = shell
        self.sp         = shell.get_player ()
        self.db         = shell.get_property ('db') 
        self.toolbar    = toolbar

        self.button     = gtk.ToggleButton (_("Lyrics"))
        self.datasource = ds
        self.view       = view
        self.song       = None
        
        self.button.show()
        self.button.set_relief( gtk.RELIEF_NONE ) 
        self.button.set_focus_on_click(False)
        self.button.connect ('clicked', 
            lambda button : self.emit('switch-tab', 'lyrics'))
        toolbar.pack_start (self.button, True, True)

    def activate (self) :
        print "activating Lyrics Tab"
        self.button.set_active(True)
        self.reload ()

    def deactivate (self) :
        print "deactivating Lyrics Tab"
        self.button.set_active(False)

    def reload (self) :
        entry = self.sp.get_playing_entry ()
        if entry is None : return
        song    = self.db.entry_get (entry, rhythmdb.PROP_TITLE)
        artist  = self.db.entry_get (entry, rhythmdb.PROP_ARTIST)
        if self.song != song :
            print "displaying loading screen"
            self.view.loading(artist, song)
            print "fetching lyrics"
            self.datasource.fetch_lyrics (artist, song)
        else :
            self.view.load_view()
        self.song = song

class LyricsView (gobject.GObject) :

    def __init__ (self, shell, plugin, webview, ds) :
        gobject.GObject.__init__ (self)
        self.webview = webview
        self.ds      = ds
        self.shell   = shell
        self.plugin  = plugin
        self.file    = ""
        self.basepath = 'file://' + os.path.split(plugin.find_file('AlbumTab.py'))[0]

        self.load_tmpl ()
        self.connect_signals ()

    def connect_signals (self) :
        self.ds.connect ('lyrics-ready', self.lyrics_ready)

    def load_view (self) :
        self.webview.load_string (self.file, 'text/html', 'utf-8', self.basepath)

    def loading (self, current_artist, song) :
        self.loading_file = self.loading_template.render (
            artist   = current_artist,
            info     = "Lyrics",
            song     = song,
            basepath = self.basepath)
        self.webview.load_string (self.loading_file, 'text/html', 'utf-8', self.basepath)
        print "loading screen loaded"

    def load_tmpl (self) :
        self.path = self.plugin.find_file('tmpl/lyrics-tmpl.html')
        self.loading_path = self.plugin.find_file ('tmpl/loading.html')
        self.template = Template (filename = self.path, 
                                  module_directory = '/tmp/context/')
        self.loading_template = Template (filename = self.loading_path, 
                                          module_directory = '/tmp/context')
        self.styles = self.basepath + '/tmpl/main.css'

    def lyrics_ready (self, ds) :
        print "loading lyrics into webview"
        lyrics = ds.get_lyrics()
        if lyrics is None : 
            lyrics = "Lyrics not found"
        else :
            lyrics = ds.get_lyrics().strip()
            lyrics = lyrics.replace ('\n', '<br />')

        self.file = self.template.render (artist     = ds.get_artist (),
                                          song       = ds.get_title (),
                                          lyrics     = lyrics,
                                          stylesheet = self.styles)
        self.load_view ()

class LyricsDataSource (gobject.GObject) :
    
    __gsignals__ = {
        'lyrics-ready' : (gobject.SIGNAL_RUN_LAST, gobject.TYPE_NONE, ()),
    }

    def __init__ (self) :
        gobject.GObject.__init__ (self)
        self.artist = "Nothing Playing"
        self.title = ""
        self.lyrics = ""

    def fetch_lyrics (self, artist, title) :
        self.artist = artist
        self.title = title
        scrubbed_artist, scrubbed_title = self.parse_song_data (artist, title)
        parser = LyricsParse.Parser (scrubbed_artist, scrubbed_title)
        parser.get_lyrics (self.fetch_lyrics_cb)

    def fetch_lyrics_cb (self, lyrics) :
        self.lyrics = lyrics
        self.emit ('lyrics-ready')

    def parse_song_data(self, artist, title):
	# don't search for 'unknown' when we don't have the 
        # artist or title information
	if artist == "Unknown" :
		artist = ""
	if title == "Unknown" :
		title = ""

	# convert to lowercase
	artist = artist.lower()
	title = title.lower()
	
	# replace ampersands and the like
	for exp in LYRIC_ARTIST_REPLACE:
		artist = re.sub(exp[0], exp[1], artist)
	for exp in LYRIC_TITLE_REPLACE:
		title = re.sub(exp[0], exp[1], title)

	# strip things like "(live at Somewhere)", "(accoustic)", etc
	for exp in LYRIC_TITLE_STRIP:
		title = re.sub (exp, '', title)

	# compress spaces
	title = title.strip()
	artist = artist.strip()	
	
	return (artist, title)
	
    def get_lyrics (self) :
        return self.lyrics

    def get_title (self) :
        return self.title

    def get_artist (self) :
        return self.artist 

