// $Id: config_utils.hpp 938 2008-12-12 03:06:32Z dub $

/*
 Copyright (c) 2007-2012, Trustees of The Leland Stanford Junior University
 All rights reserved.

 Redistribution and use in source and binary forms, with or without
 modification, are permitted provided that the following conditions are met:

 Redistributions of source code must retain the above copyright notice, this 
 list of conditions and the following disclaimer.
 Redistributions in binary form must reproduce the above copyright notice, this
 list of conditions and the following disclaimer in the documentation and/or
 other materials provided with the distribution.

 THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE 
 DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
 ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#ifndef _CONFIG_UTILS_HPP_
#define _CONFIG_UTILS_HPP_

#include<stdio.h>
#include<string>
#include<map>

extern int configparse( );

class Configuration {
  static Configuration *theConfig;
  FILE *_config_file;

protected:
  map<string,char *>       _str_map;
  map<string,unsigned int> _int_map;
  map<string,double>       _float_map;
  
public:
  Configuration( );

  void AddStrField( const string &field, const string &value );

  void Assign( const string &field, const string &value );
  void Assign( const string &field, unsigned int value );
  void Assign( const string &field, double value );

  void GetStr( const string &field, string &value, const string &def = "" ) const;
  unsigned int GetInt( const string &field, unsigned int def = 0 ) const;
  double GetFloat( const string &field, double def = 0.0 ) const;

  void Parse( const string& filename );
  int  Input( char *line, int max_size );
  void ParseError( const string &msg, unsigned int lineno ) const;

  static Configuration *GetTheConfig( );
};

bool ParseArgs( Configuration *cf, int argc, char **argv );

#endif


