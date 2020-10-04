# -*- Mode: python; coding: utf-8; tab-width: 8; indent-tabs-mode: t; -*-
#
# Copyright (C) 2009 Jonathan Matthew
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2, or (at your option)
# any later version.
#
# The Rhythmbox authors hereby grant permission for non-GPL compatible
# GStreamer plugins to be used and distributed together with GStreamer
# and Rhythmbox. This permission is above and beyond the permissions granted
# by the GPL license by which Rhythmbox is covered. If you modify this code
# you may extend this exception to your version of the code, but you are not
# obligated to do so. If you do not wish to do so, delete this exception
# statement from your version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA.


#from AstrawebParser import AstrawebParser
#from WinampcnParser import WinampcnParser
from TerraParser import TerraParser
from DarkLyricsParser import DarkLyricsParser
from JlyricParser import JlyricParser
from JetlyricsParser import JetlyricsParser

from gi.repository import RB

import gettext
gettext.install('rhythmbox', RB.locale_dir())

lyrics_sites = [
	#{ 'id': 'astraweb.com', 	'class': AstrawebParser, 	'name': _("Astraweb (www.astraweb.com)") 	},
	#{ 'id': 'winampcn.com', 	'class': WinampcnParser, 	'name': _("WinampCN (www.winampcn.com)") 	},
	{ 'id': 'terra.com.br',		'class': TerraParser,		'name': _("TerraBrasil (terra.com.br)")		},
	{ 'id': 'darklyrics.com',	'class': DarkLyricsParser,	'name': _("Dark Lyrics (darklyrics.com)")	},
	{ 'id': 'j-lyric.net', 		'class': JlyricParser, 		'name': _("Jlyric (j-lyric.net)") 		},
	{ 'id': 'jetlyrics.com', 	'class': JetlyricsParser, 	'name': _("Jetlyrics (jetlyrics.com)") 		}
]

