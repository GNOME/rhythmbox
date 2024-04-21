Title: Rhythmbox plugin examples
Slug: plugin-examples

Some example bits of code on how to various things for Python Plugins.

# User Interface

## Adding a context menu to a source

How to add a context menu to a given browser source: implement [vfunc@BrowserSource.show_entry_popup] in the source.
```
class DuplicateSource(RB.BrowserSource):

	def __init__(self):
		RB.BrowserSource.__init__(self, name=_("Duplicates"))

                app = Gio.Application.get_default()
                action = Gio.SimpleAction(name="duplicate-mark-non-duplicate")
                action.connect("activate", self.mark_non_duplicate)
                app.add_action(action)

                # you can also do this bit with GtkBuilder xml if you like
                self.__popup = Gio.Menu()
                section = Gio.Menu()
                section.append("Mark Non-duplicate", "app.duplicate-mark-non-duplicate")
                self.__popup.append_section(None, section)

                section = Gio.Menu()
                section.append("Move to Trash", "app.clipboard-move-to-trash")
                section.append("Remove", "app.clipboard-delete")
                self.__popup.append_section(None, section)

                section = Gio.Menu()
                section.append("Properties", "app.clipboard-properties")
                self.__popup.append_section(None, section)

	def do_impl_show_entry_popup(self):
                menu = Gtk.Menu.new_from_model(self.__popup)
                menu.attach_to_widget(self, None)
                menu.popup(None, None, None, None, 3, Gtk.get_current_event_time())

GObject.type_register(DuplicateSource)
```

## Add Menu Items

Plugins can add items to menus and toolbars in specific locations by calling [method@Application.add_plugin_menu_item], passing an item ID that uniquely identifies the item being added, a [class@Gio.MenuItem] instance, and the name of a plugin menu location.  Some plugin menu locations:
 * 'view': the 'view' submenu of the app menu
 * 'tools': the 'tools' submenu of the app menu
 * 'edit': the edit menu that appears in most source toolbars
 * 'browser-popup': the popup menu for sources based on RBBrowserSource (library and friends)
 * 'display-page-add': the menu attached to the 'add' button in the source list toolbar
 * 'display-page-add-playlist': the playlist section of the display-page-add menu

You can find plugin menu locations by searching for 'rb-plugin-menu-link' in the GtkBuilder UI files defining menus and toolbars.

An example:

```
	def do_activate(self):

                app = Gio.Application.get_default()

                # create action
                action = Gio.SimpleAction(name="smart-playlist-create")
                action.connect("activate", self.generate_playlist)
                app.add_action(action)

                # add plugin menu item (note the "app." prefix here)
                item = Gio.MenuItem(label=("Create Smart Playlist"), detailed_action="app.smart-playlist-create")
                app.add_plugin_menu_item('browser-popup', 'create-smart-playlist', item)

	def do_deactivate(self):

                Gio.Application.get_default().remove_plugin_menu_item('browser-popup', 'create-smart-playlist')
```

# Exploring the music library

## How do I get a list of the user's playlists?

Playlists are stored in the display page model, the [iface@Gtk.TreeModel] in the left hand pane. Get a reference to the display page model with `(shell.props.display_page_model.props.model`. The "Playlists" header item is therefore `playlist_mode_header = [x for x in list(shell.props.display_page_model.props.model) if list(x)[2] == "Playlists"][0]`, and you can get a list of playlist TreeModelRows with `playlist_model_header.iterchildren()`, so

```
playlist_model_entries = [x for x in
   list(shell.props.display_page_model.props.model)
   if list(x)[2] == "Playlists"]
if playlist_model_entries:
    playlist_iter = playlist_model_entries[0].iterchildren()
    for playlist_item in playlist_iter:
        print "Playlist image: %s, name: %s, source: %s" % (playlist_item[1], playlist_item[2], playlist_item[3])
```

## How do I get a list of the user's podcast feeds?

You can interrogate the [class@RhythmDB] object to find out a number of things to do with the users library.
The following example will print out the users podcast feed:

```
db = shell.get_property("db")

def print_podcast_info(entry):
    """Takes a podcast entry and prints out its info"""
    name = entry.get_string(RB.RhythmDBPropType.ARTIST)
    uri = entry.get_string(RB.RhythmDBPropType.LOCATION)
    print "%s: %s" % (name, uri)

db.entry_foreach_by_type(db.entry_type_get_by_name("podcast-feed"), print_podcast_info)

```

## How do I create a new "Page Group"?

First, we need to create a new "page group" type and register it with RB.
```
page_group = RB.DisplayPageGroup(shell=shell, id='lastfm-playlist', name="Last.Fm Playlist", category=RB.DisplayPageGroupCategory.TRANSIENT)
shell.append_display_page(page_group, None)
```

Next, we can add a "source" to this new group.

```
## Dummy Source Class
class lfmbs(RB.BrowserSource):
  def __init__(self):
    RB.BrowserSource.__init__(self)

GObject.type_register(lfmbs)

## Insert the a new "source"
s1=GObject.new(lfmbs, shell=shell, entry_type=shell.db.entry_register_type("lfm"))
shell.append_display_page(s1, page_group)
```

## How do I jump to the playing song?

```
# get the library's Source
src = shell.props.library_source
# get the gtk widget that views the contents of that source
lst = src.get_entry_view()
# jump to a particular entry
lst.scroll_to_entry(entry)
```

## How do I get the list of songs in a source?

A source's songs are stored in `source.props.base_query_model`, a [iface@Gtk.TreeModel]. Each item in that model has two columns, a [struct@RhythmDBEntry] and a path. So, to print all songs in a source:

```
for treerow in source.props.base_query_model:
  entry, path = list(treerow)
  print entry
```

## How do I switch to a source?

If you've got a source, and want to switch Rhythmbox over to playing it:

```
player = shell.player.get_player()
player.stop()
shell.props.display_page_tree.select(your_source)
# start playing from the beginning
player.play()
# or, if you've got a specific RhythmDBEntry that you want to play
player.play_entry(entry)
```

## How do I get the metadata details of a song?

If you have a [struct@RhythmDBEntry] for a song, then you can get the metadata by querying the database with special properties. Access to the database is from `shell.props.db`. So, expanding the above example:

```
for treerow in source.props.base_query_model:
  entry, path = list(treerow)
  artist = entry.get_string(RB.RhythmDBPropType.ARTIST)
  album = entry.get_string(RB.RhythmDBPropType.ALBUM)
  title = entry.get_string(RB.RhythmDBPropType.TITLE)
  duration = entry.get_ulong(RB.RhythmDBPropType.DURATION)
  print "%s - %s" % (title, artist)
```

There are lots and lots of defined properties; see [enum@RhythmDBPropType].


