#
#  arch-tag: Some macros handy for debugging GObjects
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
  output *(GObject *) $arg0
  echo \n
  output (char *) g_type_name (((GObject *) $arg0)->g_type_instance.g_class->g_type)
  echo \n
end
document gtype
Print the type of the argument, assuming it is a GObject.
end

define gvalue
  output *(GValue *) $arg0
  if ((GValue *) $arg0)->g_type > 0
    echo \n
    output g_type_name (((GValue *) $arg0)->g_type)
    echo \n
    # is it a string?
    if ((GValue *) $arg0)->g_type = 64 
      output (char *) (((GValue *) $arg0)->data.v_pointer)
      echo \n
    end
  end
end
document gvalue
Print the contents of the argument, assuming it is a GValue.
end

define rbnode
  output *(RBNode *) $arg0
  echo \n
  output ((RBNode *) $arg0)->properties.len
  echo \n
  set $cnt = (int) (((RBNode *) $arg0)->properties.len)
  set $i = 0
  while $i < $cnt
    if (((RBNode *) $arg0)->properties.pdata[$i])
      echo property 
      output $i
      echo \n
      #gvalue (((RBNode *) $arg0)->properties.pdata[$i])
      set $x = (((RBNode *) $arg0)->properties.pdata[$i])
      output g_type_name (((GValue *) $x)->g_type)
      echo \n
      # is it a string?
      if ((GValue *) $x)->g_type = 64 
	output (char *) (((GValue *) $x)->data.v_pointer)
	echo \n
      end
    end
    set $i = $i + 1
  end
end
