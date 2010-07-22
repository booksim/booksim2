#ifndef _BGUI_HPP_
#define _BGUI_HPP_
#include <pthread.h>
#include <QApplication>
#include <QWidget>
#include "booksim_config.hpp"

//for allocsim in main.cpp
typedef bool (*booksimfunc)( const Configuration& );

struct bsjob{
  booksimfunc bs;
  Configuration * config;
  //0 booksim is free, 1 booksim is already running
  int status;
} ;


class BooksimGUI : public QWidget
{
  //LOL QT, make sure moc runs in the make file or else you get vtable errors
  Q_OBJECT

public:
  BooksimGUI(QWidget *parent = 0);
  ~BooksimGUI(){}
  
  //allocsime is called by the gui instead of main, 
  void RegisterAllocSim(booksimfunc ,Configuration * cf);
protected slots:
  void run();
private:
  
  bsjob arbeit;
  //exactly
  pthread_t bsthread;
};



#endif
