#!/bin/sh
# arch-tag: Simple script to generate mime keys database 


MIME_TYPES=`cat $1 | grep mime_types | cut -f 2 -d "=" | tr "," " "`

for i in $MIME_TYPES ; do
	if [ ! $i = "x-directory/normal" ] ; then
		echo $i
		echo "	short_list_application_ids_for_novice_user_level=rhythmbox"
		echo "	short_list_application_ids_for_intermediate_user_level=rhythmbox"
		echo "	short_list_application_ids_for_advanced_user_level=rhythmbox"
		echo
	fi
done
