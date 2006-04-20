/*
 *  Header for DAAP (iTunes Music Sharing) structures
 *
 *  Copyright (C) 2004,2005 Charles Schmidt <cschmidt2@emich.edu>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA.
 *
 */

#ifndef __RB_DAAP_STRUCTURE_H
#define __RB_DAAP_STRUCTURE_H

#include <glib.h>
#include <glib-object.h>

G_BEGIN_DECLS

typedef enum {
	RB_DAAP_CC_INVALID = 0,
	RB_DAAP_CC_MDCL = 1,
	RB_DAAP_CC_MSTT,
	RB_DAAP_CC_MIID,
	RB_DAAP_CC_MINM,
	RB_DAAP_CC_MIKD,
	RB_DAAP_CC_MPER,
	RB_DAAP_CC_MCON,
	RB_DAAP_CC_MCTI,
	RB_DAAP_CC_MPCO,
	RB_DAAP_CC_MSTS, // 10
	RB_DAAP_CC_MIMC,
	RB_DAAP_CC_MCTC,
	RB_DAAP_CC_MRCO,
	RB_DAAP_CC_MTCO,
	RB_DAAP_CC_MLCL,
	RB_DAAP_CC_MLIT,
	RB_DAAP_CC_MBCL,
	RB_DAAP_CC_MSRV, 
	RB_DAAP_CC_MSAU,
	RB_DAAP_CC_MSLR, // 20
	RB_DAAP_CC_MPRO, 
	RB_DAAP_CC_APRO,
	RB_DAAP_CC_MSAL,
	RB_DAAP_CC_MSUP,
	RB_DAAP_CC_MSPI,
	RB_DAAP_CC_MSEX,
	RB_DAAP_CC_MSBR,
	RB_DAAP_CC_MSQY,
	RB_DAAP_CC_MSIX,
	RB_DAAP_CC_MSRS, // 30
	RB_DAAP_CC_MSTM, 
	RB_DAAP_CC_MSDC,
	RB_DAAP_CC_MCCR,
	RB_DAAP_CC_MCNM,
	RB_DAAP_CC_MCNA,
	RB_DAAP_CC_MCTY,
	RB_DAAP_CC_MLOG,
	RB_DAAP_CC_MLID,
	RB_DAAP_CC_MUPD,
	RB_DAAP_CC_MUSR, // 40
	RB_DAAP_CC_MUTY, 
	RB_DAAP_CC_MUDL,
	RB_DAAP_CC_AVDB,
	RB_DAAP_CC_ABRO,
	RB_DAAP_CC_ABAL,
	RB_DAAP_CC_ABAR,
	RB_DAAP_CC_ABCP,
	RB_DAAP_CC_ABGN,
	RB_DAAP_CC_ADBS,
	RB_DAAP_CC_ASAL, // 50
	RB_DAAP_CC_ASAR, 
	RB_DAAP_CC_ASBT,
	RB_DAAP_CC_ASBR,
	RB_DAAP_CC_ASCM,
	RB_DAAP_CC_ASCO,
	RB_DAAP_CC_ASDA,
	RB_DAAP_CC_ASDM,
	RB_DAAP_CC_ASDC,
	RB_DAAP_CC_ASDN,
	RB_DAAP_CC_ASDB, // 60
	RB_DAAP_CC_ASEQ,
	RB_DAAP_CC_ASFM,
	RB_DAAP_CC_ASGN,
	RB_DAAP_CC_ASDT,
	RB_DAAP_CC_ASRV,
	RB_DAAP_CC_ASSR,
	RB_DAAP_CC_ASSZ,
	RB_DAAP_CC_ASST,
	RB_DAAP_CC_ASSP,
	RB_DAAP_CC_ASTM, // 70
	RB_DAAP_CC_ASTC, 
	RB_DAAP_CC_ASTN,
	RB_DAAP_CC_ASUR,
	RB_DAAP_CC_ASYR,
	RB_DAAP_CC_ASDK,
	RB_DAAP_CC_ASUL,
	RB_DAAP_CC_APLY,
	RB_DAAP_CC_ABPL,
	RB_DAAP_CC_APSO,
	RB_DAAP_CC_PRSV, // 80
	RB_DAAP_CC_ARIF, 
	RB_DAAP_CC_AESV,
	RB_DAAP_CC_MSAS,
	RB_DAAP_CC_AGRP,
	RB_DAAP_CC_ASCP,
} RBDAAPContentCode;

typedef struct _RBDAAPItem RBDAAPItem;

struct _RBDAAPItem {
	RBDAAPContentCode content_code;
	GValue content;
	guint size;
};

GNode * 
rb_daap_structure_add (GNode *parent, 
		       RBDAAPContentCode cc, 
		       ...);

gchar * 
rb_daap_structure_serialize (GNode *structure, 
			     guint *length);

GNode * 
rb_daap_structure_parse (const gchar *buf, 
			 gint buf_length);

RBDAAPItem * 
rb_daap_structure_find_item (GNode *structure, 
			     RBDAAPContentCode code);

GNode * 
rb_daap_structure_find_node (GNode *structure, 
			     RBDAAPContentCode code);

void 
rb_daap_structure_print (GNode *structure);

void 
rb_daap_structure_destroy (GNode *structure);

typedef enum {
	RB_DAAP_TYPE_BYTE = 0x0001,
	RB_DAAP_TYPE_SIGNED_INT = 0x0002,
	RB_DAAP_TYPE_SHORT = 0x0003,
	RB_DAAP_TYPE_INT = 0x0005,
	RB_DAAP_TYPE_INT64 = 0x0007,
	RB_DAAP_TYPE_STRING = 0x0009,
	RB_DAAP_TYPE_DATE = 0x000A,
	RB_DAAP_TYPE_VERSION = 0x000B,
	RB_DAAP_TYPE_CONTAINER = 0x000C
} RBDAAPType;

typedef struct _RBDAAPContentCodeDefinition RBDAAPContentCodeDefinition;

struct _RBDAAPContentCodeDefinition {
	RBDAAPContentCode code;
	gint32 int_code;
	const gchar *name;
	const gchar *string;
	RBDAAPType type;
};

const RBDAAPContentCodeDefinition * 
rb_daap_content_codes (guint *number);

gint32 
rb_daap_content_code_string_as_int32 (const gchar *str);

const gchar * 
rb_daap_content_code_name (RBDAAPContentCode code);

RBDAAPType 
rb_daap_content_code_rb_daap_type (RBDAAPContentCode code);

const gchar * 
rb_daap_content_code_string (RBDAAPContentCode code);

G_END_DECLS

#endif /* __RB_DAAP_STRUCTURE_H */
