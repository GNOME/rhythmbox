Title: Rhythmbox plugin writing guide
Slug: plugin-guide

# Introduction to Rhythmbox plugins

Rhythmbox plugins are external pieces of code that can be loaded to provide extra functionality that is not found in the normal "vanilla" Rhythmbox. Plugins can do basically everything that Rhythmbox itself could do, and in fact some "core" features like Audio CD support and Internet Radio are actually plugins (although not displayed to users as such).

Rhythmbox plugins can currently be written in two languages, C and Python.  Python plugins are generally easier to write and maintain, unless you are an experienced GObject-based C programmer, so they are recommended for new plugins.  Note that Python plugins must only use pygobject (introspected bindings) to access GObject-based libraries.  If you're importing such a library by doing anything other than `from gi.repository import Something`, your plugin will crash.  Static and introspected bindings do not mix.

Most code samples in this document are written in Python, as it is easier to read and can act as pseudo-code for C programmers. In addition Python plugins can be put into your per-user plugin directory and edited there, whereas C plugins must be built inside the source tree (because they need access to the headers).

An overview of the components that form Rhythmbox's internals is available on the [[Apps/Rhythmbox/InternalDesign|Internal Design]] page.  Some distributions ship the generated API documentation, but for those who don't have a local copy, the documentation for the latest release is available [here](https://gnome.pages.gitlab.gnome.org/rhythmbox/apidoc/), and the latest build [here](https://rb.d14n.org/apidoc/).

If you have any questions, please feel free to ask us in either the IRC channel (`#gnome-rhythmbox` on irc.libera.chat) or in [GNOME discourse](https://discourse.gnome.org/).


# What makes up a plugin

There are two files required for a plugin, a plugin description file (a .plugin file) and some plugin code (a .so for C, or a .py for Python). These files can potentially live in three locations, two of which are found on a user's system and one for developers.

The first place Rhythmbox looks for plugins is in "$libdir/rhythmbox/plugins" (usually /usr/lib/rhythmbox/plugins), and this is where system-installed plugins reside. Normally only "official" plugins that are shipped with Rhythmbox itself are installed here, although other plugins can be, because it's easier to install them in the per-user plugin directory.

The second place where Rhythmbox looks is in the per-user plugin directory, $HOME/.local/share/rhythmbox/plugins. Python plugins can simply be dropped into this folder, and C plugins can be installed there.


# The Plugin Description File

The plugin description file contains the metadata shown in the "Plugins" dialog and the information necessary for Rhythmbox to load your plugin. A sample file can be found in the "plugins/sample/" and "plugins/sample-python/" directories of the source. The basic format is:

```
[Plugin]
Module=floonitz
IAge=2
Name=Test plugin
Description=A test plugin
Authors=Your Name <your email address>
Copyright=Copyright © 2007 Your name
Website=http://some.website/
```
or

```
[Plugin]
Loader=python3
Module=floonitz
IAge=2
Name=Python test plugin
Description=A python test plugin
Authors=Your Name <your email address>
Copyright=Copyright © 2007 Your name
Website=http://some.website/
```

## Fields

Name | Description
---- | -----------
Loader | The name of the plugin-loading module to use. If you are writing a C plugin, leave this out. If you are writing a Python plugin, make loader be "python3".
Module | The name of the module to load. If you are writing a C plugin, Rhythmbox will look for "module.so". If you are writing a Python plugin, Rhythmbox will look for a directory called "module" with a `__init__.py` file inside, or "module.py" (the former is better).
IAge | Set to 2.
Name | The plugin will be shown as "name" in the list in the plugins dialog.
Description | Shown when your plugin is selected in the plugin list
Authors | Shown when your plugin is selected in the plugin list
Copyright | Shown when your plugin is selected in the plugin list
Website | Shown when your plugin is selected in the plugin list

For Python plugins, best practice is to have a `__init__.py` file containing your code, the .plugin file and and other files in a directory called "yourpluginname" in the per-user plugin directory.

For C plugins, you will need to create a directory named after your plugin in the "plugins/" folder of the source tree, and then edit meson.build to add your plugin. Copying the infrastructure from one of the existing C plugins is probably a good idea.


# Overview of Rhythmbox internals - pieces of the puzzle

Rhythmbox contains a large number of pieces, which work together to provide the whole program. When first encountering Rhythmbox's internals, it is very easy to try to understand the details of everything all at once, which can become confusing. It is a much better idea to think about what you want to do, and learn about the pieces need to do that, before spreading your wings into the rest of the application.

To further that gradual introduction, an overview of what the pieces are (so you have a general understanding of how things fit together) is provided below, followed by a more detailed explanation of the components. You should not need to follow the detailed explanations to work on a simple plugin, unless the component is relevant to what you are doing. The class names given below are the python variants, but the C versions are the same with the period removed.

## Top level

The window you see when you use Rhythmbox is called the "shell" (the single instance of [class@Shell]) and it is the glue that binds the application together. Plugins are given a reference to the shell when they start, as you can get access to basically every other useful object by starting with the shell. There are a few other single-instance objects that are accessible from the shell, such as the "removable media manager" and the "playlist manager".

## Sources

The source list in the side pane contains a list of all the instances of [class@Source], each of the sources controls the sections to the right which usually contains the track list, browsers, et al. Very few sources derive directly from [class@Source], as there are a number of classes implementing common useful functionality.

The "track list" is an instance of the [class@EntryView] class which uses a [class@RhythmDBQueryModel] as a data store to hold the list of tracks.  To get the entry view for a given source, call [method@Source.get_entry_view]. A "browser", such as the artist ones, is an instance of [class@PropertyView] which similarly uses [class@RhythmDBPropertyModel] as it's data store. To get a list of the property views for a given source, call [method@Source.get_property_views]. More on those data structures later.

### Lower-level database

Almost all of the data in Rhythmbox is stored in a database called [class@RhythmDB]. The most important data structure in the database is the ''entry'' [struct@RhythmDBEntry] which represents a single track, podcast episode, podcast feed, radio station or similar item. Each entry has a set of properties with associated values, such as "title", "artist", "play count" and so one; one of these properties is special, the "location" which must be unique among all the entries in the database.  Entries are identified with an entry type ([class@RhythmDBEntryType]) which defines some aspects of the entry's behaviour.

### Higher-level database

A very important higher-level structure is the "query model" mentioned above, which can be thought of as a list of tracks. There are two ways of creating and using a query model, one is to fill in the entries manually, as it done for "static" (non-automatic) playlists. The other is to give the model a ''query'' to process, such as "all entries of type SONG, with the title containing an 'a', sorted by artist", and the query model will take care of ensuring the list of entries is up to date and sorted correctly.

The property model is somewhat similar, being given a query model and a property type, it will create and keep up to date a sorted list of all values of that property. For example being given a query model containing all the SONG tracks in the database and the property "artist", it will produce a list of all artists in the music library, which is what is used for the "artist browser".

## Plugin basics - turn it on, turn it off

Rhythmbox uses [libpeas v1](https://gitlab.gnome.org/GNOME/libpeas) to manage its plugins.  Accordingly, all plugins must implement the libpeas Activatable interface, which provides methods that are called when the plugin is activated and deactivated.  Activation happens during startup, or when the user enables a plugin in the plugin dialog.  Deactivation happens on shutdown, or when the user disables a plugin in the plugin dialog.

All real work, such as initialization or shutting things down, should be done in these two function, not in a constructor or finalizer. This is because the plugin object may still exist even though it has been deactivated, may be re-activated after being deactivated, and can potentially cause reference cycles if certain things aren't released in the deactivation function.

The plugin class must have a GObject property named "object", which is how it receives a reference to the shell object (an instance of the RBShell class), through which it can access the rest of the application.

### Activation

This function usually performs such tasks as adding User Interface (UI) items, connecting signal handlers to watch for events, and the like.

The most basic Python plugin with an activation function would be:

```
from gi.repository import GObject, RB, Peas
class FloonitzPlugin (GObject.Object, Peas.Activatable):
    object = GObject.property(type=GObject.Object)

    def __init__(self):
        super(FloonitzPlugin, self).__init__()

    def do_activate(self):
        print("Hello World")
```

Note that if you try this, you won't actually see "Hello World" printed to standard output. Why? because print statements in python are re-directed to Rhythmbox's "debug logging" system.

To see the output, you have two options: passing "-d" to rhythmbox from a terminal, or passing "-D filter". If you do the former, you will get all of Rhythmbox debug output printed - which almost certainly isn't what you want, as there is a lot of it. The second option filters the debug output to those lines that are emitting from a file whose pathname or function/method whose name contains "filter". If you plugin is called "floonitz", try "rhythmbox -D floonitz".

The equivalent "simple" plugin in C is a lot longer.  See [rb-sample-plugin.c](https://gitlab.gnome.org/GNOME/rhythmbox/-/blob/master/sample-plugins/sample/rb-sample-plugin.c?ref_type=heads) in the source tree.  Most of the code should be obvious for anyone who has done some GObject C programming, with the exception of the `RB_DEFINE_PLUGIN` line which replaces `G_DEFINE_TYPE`, and using rb_debug instead of g_printf.

### Deactivation

Now that you've gotten your plugin activated, wouldn't you like to know how to turn it off?

The deactivation function is fairly simple, just undo everything what you did in your activation function, or while running. If you added UI, remove it; if you connected to some signals, disconnect them; if you created any objects destroy them. Most importantly, if you stored a reference to the shell object anywhere (or use it with a signal) the reference MUST BE released. If you have a reference to the shell object past the time your plugin's deactivation signal runs, it can make Rhythmbox not exit correctly. This is even more important with Python plugins, as it will cause a cross-runtime reference cycle.

A simple python plugin with deactivation function

```
from gi.repository import GObject, RB, Peas
class FloonitzPlugin (GObject.Object, Peas.Activatable):
    object = GObject.property(type=GObject.Object)

    def __init__(self):
        super(FloonitzPlugin, self).__init__()

    def do_activate(self):
        self.string = "Hello World"
        print(self.string)

    def do_deactivate(self):
        del self.string
```

The C version is left as an exercise to the reader - the deactivation function is identical to the activation function, but with the name "deactivate".

## Finding your files

Almost all non-trivial plugins have some kind of data files they use, whether UI definitions, icons, or something else - and your plugin will need some way of finding out where they are.

The Rhythmbox core library provides a function called [func@find_plugin_data_file] which locates data files for a given plugin instance.  You pass it your plugin instance and a short file name, like "myfile.txt", and it will look in various places to locate it for you, and return the path to the file.


## Database essentials

### Entries
Database entries have a number of properties, a few of which might not be obvious at first glance

 * LOCATION: this is the unique identifier for an entry. It doesn't have to actually exist (e.g. file:// URIs for files or http:// URIs for internet radio), but this will be used as the URI to play from unless you override the entry-type's [vfunc@RhythmDBEntryType.get_playback_uri] method.
 * DATE: this usually represents the recording date of a track, or the year that an album was released, and is stored as a Julian Date. If you just have the year, convert it with something like `julianday = datetime.date(year, 1, 1).toordinal()`

### Entry Types

An entry type encapsulates information and behaviour common to a class of entries, and is used for things like determining what entries are shown in a [class@BrowserSource]. Entry types have the following settable properties:

 * `name`: this is used for entry types that are saved on disk, and for debugging purposes. It must be set to a non-NULL value
 * `entry_type_data_size`: this is the amount of extra space that is allocated to each entry, to store extra per-entry data. Currently this isn't usable from Python
 * `save_to_disk`: a flag which indicates whether the entries should be saved into the on-disk db or not. If set to true entries will persist across sessions; and should be set to false to things that are generated dynamically, such as track on removable media.
 * `category`: this is used to indicate what behaviour to use for some things. See [enum@RhythmDBEntryCategory] for the possible values.

Entry types also have a few methods:

 * `post_entry_create`: run after an entry is created, which is useful to set up things stored in the extra data area (of size `entry_type_data_size`), or add to mapping tables.
 * `pre_entry_destroy`: run before an entry is destroyed, usually used to do the reverse of `post_entry_create`.
 * `get_playback_uri`: run when the entry is about to be played, and should return the URI to play, or NULL to indicate that it is unplayable. If there is no method set, the default is to use the LOCATION property for the playback URI.
 * `can_sync_metadata`: run to determine whether the entries metadata can be changed. If this returns false it cannot be edited, and if it returns true `sync_metadata` will probably be called later.
 * `sync_metadata`: run when the metadata of an entry has changed. For file-backed entries this may write the changes to disk, but if not set it will default to do nothing and just change the RB database.

## Making your plugin do something useful

### Adding UI

There are several methods of adding and removing widgets to the various parts of the Rhythmbox UI. We will explore the three most useful methods for use in a plugin. The first method uses the GTK/GLib [class@Gio.Menu] to modify the menus and toolbars. The second method is using [method@Shell.add_widget], which adds new widgets to various different areas of the main Rhythmbox UI. The other method is to add a new source page which is covered in the next section.

#### Adding to menus and toolbars

[class@Gio.Menu] maps a set of [iface@Gio.Action]s to a location in a toolbar or menu. Menus and toolbars are generally defined in [class@Gtk.Builder] XML files, split up by topic. Plugins can define their own menus and toolbars in their own builder files.

On top of GMenu, Rhythmbox provides a method for plugins to insert items in specific submenus, [method@Application.add_plugin_menu_item], and a corresponding method for removing items when a plugin is deactivated, [method@Application.remove_plugin_menu_item]. To find the name of the menu you want to add an item to, look through the builder files. The plugins included in the source tree can provide some useful examples of using these methods.


#### Adding UI somewhere other than toolbars and menus

The [method@Shell.add_widget] method is useful when you want to add a new widget somewhere other than a menu or toolbar. There are limited areas where widgets can be added, but
you can insert widgets of any kind, including custom widgets implemented in your plugin.

See [enum@ShellUILocation] for the set of locations available.


### Creating a Source

Creating a new source is fairly simple with Python, but before doing so you need to determine what kind of source it is. Source fall into two main groups: those that contain all the entries of a particular type (e.g. the Radio source showing all stations, and the Magnatune source showing songs from the Magnatune catalogue), and those that contain particular entries (e.g. the play queue or a static playlist).

For the former, Rhythmbox has a base class, called [class@BrowserSource], which takes care of much of the work and also provides things like the album/artist browsers for free. To use it, do something like the following

```
from gi.repository import GObject, Peas, RB

class MyPlugin (GObject.Object, Peas.Activatable):
    object = GObject.Property(type=GObject.Object)
    def __init__(self):
        super(MyPlugin, self).__init__()

    def do_activate(self):
	print("Plugin activated")

        shell = self.object
	db = shell.props.db
	entry_type = MyEntryType()
	db.register_entry_type(entry_type)
	mysource = GObject.new (MySource, shell=shell, name=_("My Source"), entry_type=entry_type)
        group = RB.DisplayPageGroup.get_by_id ("shared")
        shell.append_display_page (mysource, group)
	shell.register_entry_type_for_source(mysource, entry_type)

class MyEntryType(RB.RhythmDBEntryType):
    def __init__(self):
        RB.RhythmDBEntryType.__init__(self, name='my-entry-type')

class MySource(RB.BrowserSource):
    def __init__(self):
        RB.BrowserSource.__init__(self)
GObject.type_register(MySource)
```

which creates a new entry type for tracks, and creates a source to display them, and asks the shell to add it to the source list in the 'shared' group. Strictly, you don't need to create a new class in the above example and could used [class@BrowserSource] directly, but creating a new class would be required as soon as you want to do anything "interesting" with it.


### Adding an icon to your source

```
# The following code sets the correct size for your source icon,
# finds it by its filename and adds it to a pixbuf.
width, height = gtk.icon_size_lookup(gtk.ICON_SIZE_LARGE_TOOLBAR)
icon = gtk.gdk.pixbuf_new_from_file_at_size(self.find_file("myicon.png"), width, height)
# The following code sets the sources "icon" property to the image stored in the pixbuf created above.
self.mysource.set_property("pixbuf", icon)
```

### Adding support for new Removable Media

TODO


### Adding a new tab to the "Song Info" window
The shell emits a signal whenever a song info window has been constructed, to allow plugins to add new things to it (such as the lyrics tab). To use it, do something like the following in your activate method

```
self.csi_id = shell.connect('create_song_info', self.create_song_info)
```
the following in your deactivate method

```
shell.disconnect (self.csi_id)
del self.csi_id
```
and define the create_song_info method

```
def create_song_info (self, shell, song_info, is_multiple):
    if is_multiple is False:
        pane = MyWidget(song_info.props.current_entry)
        song_info.append_page(_("My Tab"), pane)
```

is_multiple will be True if the song-info window is for multiple entries, and false if it's for just one. If you're adding something to the single-entry version, you'll want to connect to the "notify::current-entry" signal, to update when the user uses the back and forwards buttons.
For multiple-entry song-info windows, use `song_info.props.entry_view.get_selected_entries()` to get the list of entries to process.

### Perform actions when a source is first selected

There are many circumstances when you may want to do some work when your source is first clicked on, rather than when the plugin loads, usually when they involve connection to remote machines. Examples include downloading the catalogue from an online store and connection to DAAP shares.

[class@DisplayPage] has a virtual function called [vfunc@DisplayPage.selected] which lets you implement this; the function is called whenever the source is selected (i.e. clicked on). To do something when the source is first activated (and not every time), add something like the following to your source's class

```
def do_selected(self):
    if not self.activated:
        self.activated = True
        # do your stuff here
```

# Doing things the right way

There are some things that many usually do when programming which can either suck or cause problems, some of which are specific to Rhythmbox and some of which aren't. This includes doing synchronous IO, using threads when you don't need to, and the like. This section describes some of these, and how to do them in a better way.

## Using threads when you don't need to

There are many valid uses for threads in programming, but many people use them when they don't need to and there is a perfectly good way of doing their task without threads - usually because they don't know about the other way. The main reason is that they don't want to block the UI while they perform a long task, with the usual candidate being some IO (especially network IO) or performing a long computation. Those two cases can usually be done without threads, and the associated things like locking, watching out for race conditions and the headaches that they can introduce. If you want to do IO, use asynchronous IO instead of a thread, and if you have a long computation which is actually a small computation done many times, you can do it in an idle callback.

## Using asynchronous IO

Synchronous IO, that is using functions which load data from somewhere and then return it, are bad because if it takes a non-trivial amount of time it will block the user interface and make Rhythmbox appear to lock up. Getting data over the network is the obvious example of this, but it it also happens with local file IO to, because it might be sitting on a NFS share, the hard disc could be busy with copying data, or a number of other things. So you should never use synchronous IO.

Now that I've convinced you to use asynchronous IO, the question is how to do it.  If you're doing more than just fetching URLs into memory, use GIO.  If all you want to do is retrieve a small amount of data from a remote location, the `rb.Loader` class may be suitable.

```
def mycallback(self, data):
        print data
loader = rb.Loader()
loader.get_url("http://www.gnome.org/", self.mycallback)
```

The above code retrieves the main GNOME website, and prints it out. One things to note is that you can pass extra parameters to `rb.Loader.get_url`, which are passed to your callback.

## Streaming IO

TODO

## Using idle callbacks for repeated tasks

Many times when some code has to perform a long computation or task, it is actually a small task which is repeated many times. For example loading the track database from an iPod isn't one long task, it's the process of loading one track from the iPod DB repeated over and over. This kind of task can be reworked to use Glib's "idle callback" mechanism, which allows you do the work without blocking the user interface and without a thread.

Conceptually, the long task looks like this:

```
finished = False
initialise()
while not finished:
        finished = do_piece_of_work()
cleanup()
```
which can be transformed into:

```
def idle_cb(self):
        finished = do_piece_of_work()
        if not finished:
                return True
        cleanup()
        return False
initialise()
Gdk.threads_add_idle(self.idle_cb)
```

If your code looks somewhat like the first one, it should be fairly easy to re-write in the second form. Once you've done this, your plugin will be much nicer as it doesn't block the UI while doing the task.

### Chunking idle callbacks
If the small piece of work you're doing is actually fairly small, and repeated a large number of times, the above method can reduce performance a fair bit due to the overhead of running the idle callback that many times. The solution to this is to do more work in each callback; rather than calling do_piece_of_work() once, call it a greater number of times, i.e.

```
def idle_cb(self):
        finished = False
        count = 0
        while not finished and count < SOME_NUMBER:
                finished = do_piece_of_work()
                count++
        if not finished:
                return True
        cleanup()
        return False
initialise()
Gdk.threads_add_idle(self.idle_cb)
```

The choice of SOME_NUMBER is fairly important, but could be completely different depending on what you're doing - it might be 10, 100, 1000 or something else. Larger numbers will give you better performance but will block the UI for longer, a happy value will give a bit of a performance increase over 1 (what the non-chunking version is equivalent to) but not take too long even with older/busy computers.

# Doing Cool Things(tm)

## Adding elements to the GStreamer playback pipeline

Plugins can add elements to the GStreamer pipeline, to do interesting things with the audio. You can add "filtering" elements, which alter the audio the user hears (such as for and equalizer, or other special effects) and "tee" elements which get a copy of the audio (e.g. to send across the network, to another application, or convert to a different format).

In either case, what you need to do is create a GStreamer element, and then ask the playback backend to insert it. Later, either when it is finished or your plugin is being deactivated, you need to ask the playback backend to remove it. Inserted elements will transparently be moved to the correct place if the pipeline changes. If you need multiple elements to do what you want, put them in a Bin and add that.

You should check that the playback backend implements the functionality before trying to use it, since not all backends may implement all the different types of elements. In C, you should check the object implements the appropriate GObject interface, an in Python you should check the methods exist.

As an example, here is how you add a "poor mans's visualisation" using Python (it can be run in the python console).

```
from gi.repository import Gst
goom = Gst.element_factory_make ("goom")
sink = Gst.element_factory_make ("ximagesink")
colour = Gst.element_factory_make ("ffmpegcolorspace")
b = Gst.Bin()
b.add (goom, colour, sink)
b.add_pad(Gst.GhostPad("sink", goom.get_pad("sink")))
goom.link(colour)
colour.link(sink)
shell.get_player().props.player.add_tee(b)
```

and remove it

```
shell.get_player().props.player.remove_tee(b)
```

# How do I ...

## Get the list of entries in a source?

Sources have a "query-model" property, which lets you access the RB.RhythmDBQueryModel (which is a Gtk.TreeModel) which backs the track list. You can simply iterate over the rows in the model, getting each of the entries in turn (a reference is stored in the zeroeth column). For example

```
for row in mysource.props.query_model:
        entry = row[0]
        print db.entry_get(entry, RB.RhythmDBPropType.PROP_TITLE)
```

## Filter by 'song' type

```
    def on_entry_added(self, _tree, entry):
        """
        'entry-added' signal handler
        """
        ### place the following in an 'init' section so
        ### it doesn't get repeated on each signal
        self.type_song=self.db.entry_type_get_by_name("song")
        type=entry.get_entry_type()
        if type==self.type_song:
            id=self.db.entry_get(entry, RB.RhythmDBPropType.ENTRY_ID)
            self.song_entries.append(int(id))
```

## Disable a bad plugin from outside Rhythmbox?

If you've written a plugin that crashes Rhythmbox, instead of moving the plugin to a different directory you can disable it in GSettings.
I find that when I activate a bad plugin from inside Rhythmbox, it provides more useful output than if you have started Rhythmbox with the plugin activated.
