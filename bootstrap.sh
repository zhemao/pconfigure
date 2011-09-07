#!/bin/bash

# Builds every file into pconfigure
gcc -Wall -Werror -pedantic -DDEBUG_RETURNS -g `find src/pconfigure/ -iname "*.c"` -o "pconfigure" || exit $?

# Runs pconfigure in order to build itself
valgrind --leak-check=full --show-reachable=yes ./pconfigure
err="$?"
if [[ "$err" != "0" ]]
then
    exit $err
fi
rm pconfigure

# Actually builds itself
make || exit $?

# Informational messages to the user
prefix=`cat src/pconfigure/defaults.h  | grep PREFIX | cut -d ' ' -f 2 | cut -d '"' -f 2`
echo "run 'make install' to install this to the system"
echo -e "\tby default it is installed into $prefix"
