import rhythmdb, rb
import gobject
import gtk, gtk.glade
import gconf, gnomevfs, gnome

import urllib
import zipfile
import sys, os.path
import xml
import datetime

from MagnatuneSource import MagnatuneSource


popup_ui = """
<ui>
  <popup name="MagnatuneSourceViewPopup">
    <menuitem name="AddToQueueLibraryPopup" action="AddToQueue"/>
    <menuitem name="MagnatunePurchaseAlbum" action="MagnatunePurchaseAlbum"/>
    <menuitem name="MagnatuneArtistInfo" action="MagnatuneArtistInfo"/>
    <menuitem name="MagnatuneCancelDownload" action="MagnatuneCancelDownload"/>
    <separator/>
    <menuitem name="BrowseGenreLibraryPopup" action="BrowserSrcChooseGenre"/>
    <menuitem name="BrowseArtistLibraryPopup" action="BrowserSrcChooseArtist"/>
    <menuitem name="BrowseAlbumLibraryPopup" action="BrowserSrcChooseAlbum"/>
    <separator/>
    <menuitem name="PropertiesLibraryPopup" action="MusicProperties"/>
  </popup>
</ui>
"""


class Magnatune(rb.Plugin):
	client = gconf.client_get_default()

	format_list = ['ogg', 'flac', 'wav', 'mp3-vbr', 'mp3-cbr']

	gconf_keys = {
		'format': "/apps/rhythmbox/plugins/magnatune/format",
		'pay': "/apps/rhythmbox/plugins/magnatune/pay",
		'ccnumber': "/apps/rhythmbox/plugins/magnatune/cc_number",
		'ccyear': "/apps/rhythmbox/plugins/magnatune/cc_year",
		'ccmonth': "/apps/rhythmbox/plugins/magnatune/cc_month",
		'ccname': "/apps/rhythmbox/plugins/magnatune/name",
		'email': "/apps/rhythmbox/plugins/magnatune/email",
		'forget': "/apps/rhythmbox/plugins/magnatune/forget"
	}


	#
	# Core methods
	#
	
	def __init__(self):
		rb.Plugin.__init__(self)
		
	def activate(self, shell):
		self.shell = shell # so buy_track can update the progress bar
		self.db = shell.get_property("db")

		self.entry_type = self.db.entry_register_type("MagnatuneEntryType")
		# allow changes which don't do anything
		self.entry_type.can_sync_metadata = True
		self.entry_type.sync_metadata = None

		self.source = gobject.new (MagnatuneSource, shell=shell, entry_type=self.entry_type, plugin=self)
		shell.register_entry_type_for_source(self.source, self.entry_type)
		shell.append_source(self.source, None) # Add the source to the list
		
		manager = shell.get_player().get_property('ui-manager')
		# Add the popup menu actions
		action = gtk.Action('MagnatunePurchaseAlbum', _('Purchase Album'),
				_("Purchase this album from Magnatune"),
				'gtk-save')
		action.connect('activate', lambda a: self.shell.get_property("selected-source").purchase_tracks())
		self.action_group = gtk.ActionGroup('MagnatunePluginActions')
		self.action_group.add_action(action)
		action = gtk.Action('MagnatuneArtistInfo', _('Artist Information'),
				_("Get information about this artist"),
				'gtk-info')
		action.connect('activate', lambda a: self.shell.get_property("selected-source").display_artist_info())
		self.action_group.add_action(action)
		action = gtk.Action('MagnatuneCancelDownload', _('Cancel Downloads'),
				_("Stop downloading purchased albums"),
				'gtk-stop')
		action.connect('activate', lambda a: self.shell.get_property("selected-source").cancel_downloads())
		action.set_sensitive(False)
		self.action_group.add_action(action)
		
		manager.insert_action_group(self.action_group, 0)
		self.ui_id = manager.add_ui_from_string(popup_ui)
		manager.ensure_update()
	
	def deactivate(self, shell):
		manager = shell.get_player().get_property('ui-manager')
		manager.remove_ui (self.ui_id)
		manager.remove_action_group(self.action_group)
		self.action_group = None

		self.db.entry_delete_by_type(self.entry_type)
		self.db.commit()
		self.db = None
		self.entry_type = None
		self.source.delete_thyself()
		self.source = None
		self.shell = None
	
	def create_configure_dialog(self, dialog=None):
		if dialog == None:
			
			def ignore_checkbox_toggled(button):
				if button.get_active():
					gladexml.get_widget("name_entry").set_sensitive(False)
					gladexml.get_widget("cc_entry").set_sensitive(False)
					gladexml.get_widget("mm_entry").set_sensitive(False)
					gladexml.get_widget("yy_entry").set_sensitive(False)
					gladexml.get_widget("name_entry").set_text("")
					gladexml.get_widget("cc_entry").set_text("")
					gladexml.get_widget("mm_entry").set_active(0)
					gladexml.get_widget("yy_entry").set_text("")
					self.client.set_bool(self.gconf_keys['forget'], True)
				else:
					gladexml.get_widget("name_entry").set_sensitive(True)
					gladexml.get_widget("cc_entry").set_sensitive(True)
					gladexml.get_widget("mm_entry").set_sensitive(True)
					gladexml.get_widget("yy_entry").set_sensitive(True)
					self.client.set_bool(self.gconf_keys['forget'], False)
			
			def yy_entry_changed(entry):
				self.client.set_string(self.gconf_keys['ccyear'], entry.get_text())
				try:
					mm = gladexml.get_widget("mm_entry").get_active_text()
					if int(entry.get_text()) < (datetime.date.today().year % 100):
						gladexml.get_widget("cc_expired_label").visible = True
						gladexml.get_widget("cc_expired_label").show()
					elif (int(entry.get_text()) == (datetime.date.today().year % 100) and int(mm) < datetime.date.today().month):
						gladexml.get_widget("cc_expired_label").visible = True
						gladexml.get_widget("cc_expired_label").show()
					else:
						gladexml.get_widget("cc_expired_label").visible = False
						gladexml.get_widget("cc_expired_label").hide()
				except Exception,e:
					print e
			
			def mm_entry_changed(entry):
				self.client.set_string(self.gconf_keys['ccmonth'], entry.get_active_text())
				try:
					yy = gladexml.get_widget("yy_entry").get_text()
					if int(yy) < (datetime.date.today().year % 100):
						gladexml.get_widget("cc_expired_label").visible = True
						gladexml.get_widget("cc_expired_label").show()
					elif (int(yy) == (datetime.date.today().year % 100) and int(entry.get_active_text()) < datetime.date.today().month):
						gladexml.get_widget("cc_expired_label").visible = True
						gladexml.get_widget("cc_expired_label").show()
					else:
						gladexml.get_widget("cc_expired_label").visible = False
						gladexml.get_widget("cc_expired_label").hide()
				except Exception,e:
					print e
			
			self.configure_callback_dic = {
				"rb_magnatune_name_entry_changed_cb" : lambda w: self.client.set_string(self.gconf_keys['ccname'], w.get_text()),
				"rb_magnatune_email_entry_changed_cb" : lambda w: self.client.set_string(self.gconf_keys['email'], w.get_text()),
				"rb_magnatune_cc_entry_changed_cb" : lambda w: self.client.set_string(self.gconf_keys['ccnumber'], w.get_text()),
				"rb_magnatune_yy_entry_changed_cb" : yy_entry_changed,
				"rb_magnatune_mm_entry_changed_cb" : mm_entry_changed,
				"rb_magnatune_pay_combobox_changed_cb" : lambda w: self.client.set_int(self.gconf_keys['pay'], w.get_active() + 5),
				"rb_magnatune_audio_combobox_changed_cb" : lambda w: self.client.set_string(self.gconf_keys['format'], self.format_list[w.get_active()]),
				"rb_magnatune_ignore_cc_info_checkbox_toggled_cb" : ignore_checkbox_toggled
			}

			gladexml = gtk.glade.XML(self.find_file("magnatune-prefs.glade"))
			gladexml.signal_autoconnect(self.configure_callback_dic)

			# FIXME this bit should be in glade too
			dialog = gladexml.get_widget('preferences_dialog')
			dialog.connect("response", lambda w, r: w.hide())
			
			gladexml.get_widget("name_entry").set_text(self.client.get_string(self.gconf_keys['ccname']))
			gladexml.get_widget("email_entry").set_text(self.client.get_string(self.gconf_keys['email']))
			gladexml.get_widget("cc_entry").set_text(self.client.get_string(self.gconf_keys['ccnumber']))
			gladexml.get_widget("yy_entry").set_text(self.client.get_string(self.gconf_keys['ccyear']))
			gladexml.get_widget("mm_entry").set_active(int(self.client.get_string(self.gconf_keys['ccmonth'])) - 1)
			gladexml.get_widget("pay_combobox").set_active(self.client.get_int(self.gconf_keys['pay']) - 5)
			gladexml.get_widget("audio_combobox").set_active(self.format_list.index(self.client.get_string(self.gconf_keys['format'])))
			gladexml.get_widget("ignore_cc_info_checkbox").set_active(self.client.get_bool(self.gconf_keys['forget']))

		dialog.present()
		return dialog
