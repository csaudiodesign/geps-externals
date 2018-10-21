# Makefile to build the GePS externals library for Pure data.
# Needs Makefile.pdlibbuilder as helper makefile for platform-dependent build
# settings and rules.

# library name
lib.name = geps

lib.setup.sources = geps.c

# input source file (class name == source file basename)
class.sources = fbnet~.c

# all extra files to be included in binary distribution of the library
datafiles = fbnet~-help.pd

# include paths to sensel headers, in case they are not found
# i.e. cflags = -I /usr/local/include
cflags = 

# linker flags
# i.e. ldflags = -lpthread
ldflags = 

make-lib-executable = yes

suppress-wunused = yes

# include Makefile.pdlibbuilder from submodule directory 'pd-lib-builder'
PDLIBBUILDER_DIR=./pd-lib-builder
include $(PDLIBBUILDER_DIR)/Makefile.pdlibbuilder
