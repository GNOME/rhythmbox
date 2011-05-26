


#include "eggwrapbox-enums.h"

/* enumerations from "eggwrapbox.h" */
#include "eggwrapbox.h"
GType
egg_wrap_allocation_mode_get_type (void)
{
  static GType type = 0;

  if (!type)
  {
    static const GEnumValue _egg_wrap_allocation_mode_values[] = {
      { EGG_WRAP_ALLOCATE_FREE, "EGG_WRAP_ALLOCATE_FREE", "free" },
      { EGG_WRAP_ALLOCATE_ALIGNED, "EGG_WRAP_ALLOCATE_ALIGNED", "aligned" },
      { EGG_WRAP_ALLOCATE_HOMOGENEOUS, "EGG_WRAP_ALLOCATE_HOMOGENEOUS", "homogeneous" },
      { 0, NULL, NULL }
    };

    type = g_enum_register_static ("EggWrapAllocationMode", _egg_wrap_allocation_mode_values);
  }

  return type;
}

GType
egg_wrap_box_spreading_get_type (void)
{
  static GType type = 0;

  if (!type)
  {
    static const GEnumValue _egg_wrap_box_spreading_values[] = {
      { EGG_WRAP_BOX_SPREAD_START, "EGG_WRAP_BOX_SPREAD_START", "start" },
      { EGG_WRAP_BOX_SPREAD_END, "EGG_WRAP_BOX_SPREAD_END", "end" },
      { EGG_WRAP_BOX_SPREAD_EVEN, "EGG_WRAP_BOX_SPREAD_EVEN", "even" },
      { EGG_WRAP_BOX_SPREAD_EXPAND, "EGG_WRAP_BOX_SPREAD_EXPAND", "expand" },
      { 0, NULL, NULL }
    };

    type = g_enum_register_static ("EggWrapBoxSpreading", _egg_wrap_box_spreading_values);
  }

  return type;
}

GType
egg_wrap_box_packing_get_type (void)
{
  static GType type = 0;

  if (!type)
  {
    static const GFlagsValue _egg_wrap_box_packing_values[] = {
      { EGG_WRAP_BOX_H_EXPAND, "EGG_WRAP_BOX_H_EXPAND", "h-expand" },
      { EGG_WRAP_BOX_V_EXPAND, "EGG_WRAP_BOX_V_EXPAND", "v-expand" },
      { 0, NULL, NULL }
    };

    type = g_flags_register_static ("EggWrapBoxPacking", _egg_wrap_box_packing_values);
  }

  return type;
}




