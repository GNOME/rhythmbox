#  Some macros handy for debugging GObjects.
#
#  Copyright © 2003 Colin Walters <walters@verbum.org>
#
#  This program is free software; you can redistribute it and/or modify
#  it under the terms of the GNU General Public License as published by
#  the Free Software Foundation; either version 2, or (at your option)
#  any later version.
#
#  This program is distributed in the hope that it will be useful,
#  but WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#  GNU General Public License for more details.
#
#  You should have received a copy of the GNU General Public License
#  along with this program; if not, write to the Free Software
#  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
#
#  $Id$

define gtype
  output *(GObject *) $
  echo \n
  output (char *) g_type_name (((GObject *) $)->g_type_instance.g_class->g_type)
  echo \n
end
document gtype
Print the type of $, assuming it is a GObject.
end

define gvalue
  output *(GValue *) $
  if ((GValue *) $)->g_type > 0
    echo \n
    output g_type_name (((GValue *) $)->g_type)
    echo \n
    # is it a string?
    if ((GValue *) $)->g_type = 64 
      output (char *) (((GValue *) $)->data.v_pointer)
      echo \n
    end
  end
end
document gvalue
Print the contents of $, assuming it is a GValue.
end
