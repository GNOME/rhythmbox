# Some macros handy for debugging GObjects.

define gtype
output (char *) g_type_name (((GObject *) $)->g_type_instance.g_class->g_type)
echo \n
end
document gtype
Print the type of $, assuming it is an GObject.
end
