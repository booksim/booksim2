# $Id $

# Copyright (c) 2007-2015, Trustees of The Leland Stanford Junior University
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

LEX = flex
YACC   = bison -y
DEFINE = 
INCPATH = -I. -Iarbiters -Iallocators -Irouters -Inetworks -Ipower
CPPFLAGS += -Wall $(INCPATH) $(DEFINE)
CPPFLAGS += -O3
CPPFLAGS += -g
LFLAGS +=

PROG := booksim

# simulator source files
CPP_SRCS = $(wildcard *.cpp) $(wildcard */*.cpp)
CPP_HDRS = $(wildcard *.hpp) $(wildcard */*.hpp)
CPP_DEPS = $(CPP_SRCS:.cpp=.d)
CPP_OBJS = $(CPP_SRCS:.cpp=.o)

LEX_SRCS = lex.yy.c
LEX_OBJS = lex.yy.o

YACC_SRCS = y.tab.c
YACC_HDRS = y.tab.h
YACC_OBJS = y.tab.o

OBJS :=  $(CPP_OBJS) $(LEX_OBJS) $(YACC_OBJS)

.PHONY: clean

all: $(PROG)

$(PROG): $(OBJS)
	 $(CXX) $(LFLAGS) $^ -o $@

$(LEX_SRCS): config.l
	$(LEX) $<

$(YACC_SRCS) $(YACC_HDRS): config.y
	$(YACC) -d $<

$(LEX_OBJS): $(LEX_SRCS) $(YACC_HDRS)
	$(CC) $(CPPFLAGS) -c $< -o $@

$(YACC_OBJS): $(YACC_SRCS)
	$(CC) $(CPPFLAGS) -c $< -o $@

%.o: %.cpp
	$(CXX) $(CPPFLAGS) -MMD -c $< -o $@

clean:
	rm -f $(YACC_SRCS) $(YACC_HDRS)
	rm -f $(LEX_SRCS)
	rm -f $(CPP_DEPS)
	rm -f $(OBJS)
	rm -f $(PROG)

distclean: clean
	rm -f *~ */*~
	rm -f *.o */*.o
	rm -f *.d */*.d

-include $(CPP_DEPS)
