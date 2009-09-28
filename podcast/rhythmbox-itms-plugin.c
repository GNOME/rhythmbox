/*
 *  Copyright © 2007 Bastien Nocera <hadess@hadess.net>
 *  Copyright © 2005 Jorn Baayen <jbaayen@gnome.org>
 *  Copyright © 2005 Christian Persch
 *
 *  Based on the work of:
 *
 *  Copyright © 2004 Bastien Nocera <hadess@hadess.net>
 *  Copyright © 2002 David A. Schleef <ds@schleef.org>
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Library General Public
 *  License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Library General Public License for more details.
 *
 *  You should have received a copy of the GNU Library General Public
 *  License along with this library; if not, write to the
 *  Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 *  Boston, MA 02110-1301, USA.
 */

#include "config.h"

#define bool int
#include <string.h>
#include <npupp.h>

static NPNetscapeFuncs mozilla_functions;

static NPError
plugin_new_instance (NPMIMEType mime_type,
		     NPP instance,
		     uint16 mode,
		     int16 argc,
		     char **argn,
		     char **argv,
		     NPSavedData *saved)
{
	return NPERR_INVALID_INSTANCE_ERROR;
}

static NPError
plugin_destroy_instance (NPP instance,
			 NPSavedData **save)
{
	return NPERR_NO_ERROR;
}

static NPError
plugin_new_stream (NPP instance,
		   NPMIMEType type,
		   NPStream *stream_ptr,
		   NPBool seekable,
		   uint16 *stype)
{
	return NPERR_INVALID_PARAM;
}

static NPError
plugin_stream_as_file (NPP instance,
		       NPStream* stream,
		       const char *filename)
{
	return NPERR_INVALID_PARAM;
}

static NPError
plugin_destroy_stream (NPP instance,
		       NPStream *stream,
		       NPError reason)
{
	return NPERR_NO_ERROR;
}

static int32
plugin_write_ready (NPP instance,
		    NPStream *stream)
{
	return 0;
}

static int32
plugin_write (NPP instance,
	      NPStream *stream,
	      int32 offset,
	      int32 len,
	      void *buffer)
{
	return -1;
}

static NPError
plugin_get_value (NPP instance,
		  NPPVariable variable,
		  void *value)
{
	NPError err = NPERR_NO_ERROR;

	switch (variable) {
	case NPPVpluginNameString:
		*((char **) value) = "iTunes Application Detector";
		break;

	case NPPVpluginDescriptionString:
		*((char **) value) = "This plug-in detects the presence of iTunes when opening iTunes Store URLs in a web page with Firefox.";
		break;

	case NPPVpluginNeedsXEmbed:
		*((NPBool *) value) = FALSE;
		break;

	default:
		err = NPERR_INVALID_PARAM;
		break;
	}

	return err;
}

NPError
NP_GetValue (void *future,
	     NPPVariable variable,
	     void *value)
{
	return plugin_get_value (NULL, variable, value);
}

char *
NP_GetMIMEDescription (void)
{
	return "application/itunes-plugin::;";
}

NPError
NP_Initialize (NPNetscapeFuncs *moz_funcs,
               NPPluginFuncs *plugin_funcs)
{
	if (moz_funcs == NULL || plugin_funcs == NULL)
		return NPERR_INVALID_FUNCTABLE_ERROR;

	if ((moz_funcs->version >> 8) > NP_VERSION_MAJOR)
		return NPERR_INCOMPATIBLE_VERSION_ERROR;
	if (moz_funcs->size < sizeof (NPNetscapeFuncs))
		return NPERR_INVALID_FUNCTABLE_ERROR;
	if (plugin_funcs->size < sizeof (NPPluginFuncs))
		return NPERR_INVALID_FUNCTABLE_ERROR;

	/*
	 * Copy all of the fields of the Mozilla function table into our
	 * copy so we can call back into Mozilla later.  Note that we need
	 * to copy the fields one by one, rather than assigning the whole
	 * structure, because the Mozilla function table could actually be
	 * bigger than what we expect.
	 */
	mozilla_functions.size             = moz_funcs->size;
	mozilla_functions.version          = moz_funcs->version;
	mozilla_functions.geturl           = moz_funcs->geturl;
	mozilla_functions.posturl          = moz_funcs->posturl;
	mozilla_functions.requestread      = moz_funcs->requestread;
	mozilla_functions.newstream        = moz_funcs->newstream;
	mozilla_functions.write            = moz_funcs->write;
	mozilla_functions.destroystream    = moz_funcs->destroystream;
	mozilla_functions.status           = moz_funcs->status;
	mozilla_functions.uagent           = moz_funcs->uagent;
	mozilla_functions.memalloc         = moz_funcs->memalloc;
	mozilla_functions.memfree          = moz_funcs->memfree;
	mozilla_functions.memflush         = moz_funcs->memflush;
	mozilla_functions.reloadplugins    = moz_funcs->reloadplugins;
	mozilla_functions.getJavaEnv       = moz_funcs->getJavaEnv;
	mozilla_functions.getJavaPeer      = moz_funcs->getJavaPeer;
	mozilla_functions.geturlnotify     = moz_funcs->geturlnotify;
	mozilla_functions.posturlnotify    = moz_funcs->posturlnotify;
	mozilla_functions.getvalue         = moz_funcs->getvalue;
	mozilla_functions.setvalue         = moz_funcs->setvalue;
	mozilla_functions.invalidaterect   = moz_funcs->invalidaterect;
	mozilla_functions.invalidateregion = moz_funcs->invalidateregion;
	mozilla_functions.forceredraw      = moz_funcs->forceredraw;
	mozilla_functions.geturl           = moz_funcs->geturl;

	/*
	 * Set up a plugin function table that Mozilla will use to call
	 * into us.  Mozilla needs to know about our version and size and
	 * have a UniversalProcPointer for every function we implement.
	 */

	plugin_funcs->size = sizeof (NPPluginFuncs);
	plugin_funcs->version = (NP_VERSION_MAJOR << 8) + NP_VERSION_MINOR;
	plugin_funcs->newp = NewNPP_NewProc (plugin_new_instance);
	plugin_funcs->destroy = NewNPP_DestroyProc (plugin_destroy_instance);
	plugin_funcs->setwindow = NewNPP_SetWindowProc (NULL);
	plugin_funcs->newstream = NewNPP_NewStreamProc (plugin_new_stream);
	plugin_funcs->destroystream = NewNPP_DestroyStreamProc (plugin_destroy_stream);
	plugin_funcs->asfile = NewNPP_StreamAsFileProc (plugin_stream_as_file);
	plugin_funcs->writeready = NewNPP_WriteReadyProc (plugin_write_ready);
	plugin_funcs->write = NewNPP_WriteProc (plugin_write);
	plugin_funcs->print = NewNPP_PrintProc (NULL);
	plugin_funcs->event = NewNPP_HandleEventProc (NULL);
	plugin_funcs->urlnotify = NewNPP_URLNotifyProc (NULL);
	plugin_funcs->javaClass = NULL;
	plugin_funcs->getvalue = NewNPP_GetValueProc (plugin_get_value);
	plugin_funcs->setvalue = NewNPP_SetValueProc (NULL);

	return NPERR_NO_ERROR;
}

NPError
NP_Shutdown (void)
{
	return NPERR_NO_ERROR;
}
