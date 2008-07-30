#
# Makefile
#
CPP    = /usr/bin/g++
YACC   = /usr/bin/bison -d
LEX    = /usr/bin/flex
PURIFY = /usr/bin/purify
QUANT  = /usr/bin/quantify
CPPFLAGS = -O3 -I. -Iallocators -Irouters -Inetworks -ggdb 
# LFLAGS = -static-libgcc -static

OBJDIR := obj
PROG   := booksim

# simulator source files
CPP_SRCS = main.cpp \
   config_utils.cpp \
   booksim_config.cpp \
   module.cpp \
   vc.cpp \
   routefunc.cpp \
   traffic.cpp \
   flitchannel.cpp \
   creditchannel.cpp \
   network.cpp \
   trafficmanager.cpp \
   buffer_state.cpp \
   stats.cpp \
   credit.cpp \
   outputset.cpp \
   flit.cpp \
   injection.cpp\
   random_utils.cpp\
   misc_utils.cpp\
   rng_wrapper.cpp\
   rng_double_wrapper.cpp

LEX_OBJS  = ${OBJDIR}/configlex.o
YACC_OBJS = ${OBJDIR}/config_tab.o

# networks 
NETWORKS := $(wildcard networks/*.cpp) 
ALLOCATORS:= $(wildcard allocators/*.cpp)
ROUTERS:=$(wildcard routers/*.cpp)

#--- Make rules ---
OBJS :=  $(LEX_OBJS) $(YACC_OBJS)\
 $(CPP_SRCS:%.cpp=${OBJDIR}/%.o)\
 $(NETWORKS:networks/%.cpp=${OBJDIR}/%.o)\
 $(ALLOCATORS:allocators/%.cpp=${OBJDIR}/%.o)\
 $(ROUTERS:routers/%.cpp=${OBJDIR}/%.o)\

.PHONY: clean
.PRECIOUS: %_tab.cpp %_tab.hpp %lex.cpp

# rules to compile simulator
${OBJDIR}/%lex.o: %lex.cpp %_tab.hpp
	$(CPP) $(CPPFLAGS) -c $< -o $@

${OBJDIR}/%.o: %.cpp 
	$(CPP) $(CPPFLAGS) -c $< -o $@

${OBJDIR}/%.o: %.c

$(PROG): $(OBJS)
	 $(CPP) $(LFLAGS) $(OBJS) -o $(PROG)

# rules to compile networks
${OBJDIR}/%.o: networks/%.cpp 
	$(CPP) $(CPPFLAGS) -c $< -o $@

# rules to compile allocators
${OBJDIR}/%.o: allocators/%.cpp 
	$(CPP) $(CPPFLAGS) -c $< -o $@

# rules to compile routers
${OBJDIR}/%.o: routers/%.cpp 
	$(CPP) $(CPPFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) 
	rm -f $(PROG)
	rm -f *~
	rm -f networks/*~
	rm -f runfiles/*~

#purify: $(OBJS)
#	$(PURIFY) -always-use-cache-dir $(CPP) $(OBJS) -o $(PROG) -L/usr/pubsw/lib
#
#quantify: $(OBJS)
#	$(QUANT) -always-use-cache-dir $(CPP) $(OBJS) -o $(PROG) -L/usr/pubsw/lib

#	$(CPP) $(CPPFLAGS) $(VCSFLAGS) -c $< -o $@

##%_tab.cpp: %.y
##	$(YACC) -b$* -p$* $<
##	cp -f $*.tab.c $*_tab.cpp
##
##%_tab.hpp: %_tab.cpp
##	cp -f $*.tab.h $*_tab.hpp
##
##%lex.cpp: %.l
##	$(LEX) -P$* -o$@ $<

# *_tab.cpp *_tab.hpp #*.tab.c *.tab.h *lex.cpp

