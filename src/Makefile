# $Id $

# Copyright (c) 2007-2012, Trustees of The Leland Stanford Junior University
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#
# Redistributions of source code must retain the above copyright notice, this
# list of conditions and the following disclaimer.
# Redistributions in binary form must reproduce the above copyright notice,
# this list of conditions and the following disclaimer in the documentation
# and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
# LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
# CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
# SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
# INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
# CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
# ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
# POSSIBILITY OF SUCH DAMAGE.

#
# Makefile
#
CPP    = mpicxx
YACC   = /usr/bin/bison -d
LEX    = /usr/bin/flex
PURIFY = /usr/bin/purify
QUANT  = /usr/bin/quantify
CPPFLAGS =-mpe=mpilog -O3 -I. -Iarbiters -Iallocators -Irouters -Inetworks -ggdb 
LFLAGS =  -mpe=mpilog

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
   trafficmanager.cpp \
   ptrafficmanager.cpp \
   buffer_state.cpp \
   stats.cpp \
   credit.cpp \
   outputset.cpp \
   flit.cpp \
   injection.cpp\
   random_utils.cpp\
   misc_utils.cpp\
   rng_wrapper.cpp\
   rng_double_wrapper.cpp\
   power_module.cpp

LEX_OBJS  = ${OBJDIR}/configlex.o
YACC_OBJS = ${OBJDIR}/config_tab.o

# networks 
NETWORKS := $(wildcard networks/*.cpp) 
ALLOCATORS:= $(wildcard allocators/*.cpp)
ARBITERS:= $(wildcard arbiters/*.cpp)
ROUTERS:=$(wildcard routers/*.cpp)

#--- Make rules ---
OBJS :=  $(LEX_OBJS) $(YACC_OBJS)\
 $(CPP_SRCS:%.cpp=${OBJDIR}/%.o)\
 $(NETWORKS:networks/%.cpp=${OBJDIR}/%.o)\
 $(ALLOCATORS:allocators/%.cpp=${OBJDIR}/%.o)\
 $(ARBITERS:arbiters/%.cpp=${OBJDIR}/%.o)\
 $(ROUTERS:routers/%.cpp=${OBJDIR}/%.o)

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

# rules to compile arbiters
${OBJDIR}/%.o: arbiters/%.cpp 
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
	rm -f allocators/*~
	rm -f arbiters/*~
	rm -f networks/*~
	rm -f runfiles/*~
	rm -f routers/*~
	rm -f examples/*~

#purify: $(OBJS)
#	$(PURIFY) -always-use-cache-dir $(CPP) $(OBJS) -o $(PROG) -L/usr/pubsw/lib
#
#quantify: $(OBJS)
#	$(QUANT) -always-use-cache-dir $(CPP) $(OBJS) -o $(PROG) -L/usr/pubsw/lib

#	$(CPP) $(CPPFLAGS) $(VCSFLAGS) -c $< -o $@

%_tab.cpp: %.y
	$(YACC) -b$* -p$* $<
	cp -f $*.tab.c $*_tab.cpp

%_tab.hpp: %_tab.cpp
	cp -f $*.tab.h $*_tab.hpp

%lex.cpp: %.l
	$(LEX) -P$* -o$@ $<

# *_tab.cpp *_tab.hpp #*.tab.c *.tab.h *lex.cpp

