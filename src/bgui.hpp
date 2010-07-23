#ifndef _BGUI_HPP_
#define _BGUI_HPP_
#include <pthread.h>
#include <QApplication>
#include <QWidget>
#include <QPushButton>
#include <QGridLayout>
#include <QFrame>
#include <QLineEdit>
#include <QLabel>
#include <QTabWidget>


#include <QString>
#include "booksim_config.hpp"


#include <map>
#include <vector>
#include <string>

//for allocsim in main.cpp
typedef bool (*booksimfunc)( const Configuration& );

//for pthread
struct bsjob{
  booksimfunc bs;
  Configuration * config;
  //0 booksim is free, 1 booksim is already running
  int status;
} ;


class configTab : public QWidget
{
  //qt required
  Q_OBJECT
  public:
  configTab(QWidget *parent = 0);
  void setup(Configuration * cf);

protected slots:
  void toggleadvanced();
  void writeconfig();
  void saveconfig();
private:
  //layout manager for the config tab
  QGridLayout *configLayout;
  //another pointer to the booksim config file
  Configuration * config;

  //displaying the misc booksim options
  bool show_advanced;
  QFrame* advanced_frame;

  //for the layout manager
  int vmax;
  int hmax;

  //write cofig file name widget
  QLineEdit *write_edit;
  
  //all the widgets asoociated with all the booksi options
  //labels are texts and lineedits are the text boxes
  //can't consolidate them because config->assign is type specific
  map<string, QLabel*> str_map_label;
  map<string, QLineEdit*> str_map_obj;
  map<string, QLabel*> int_map_label;
  map<string, QLineEdit*> int_map_obj;
  map<string, QLabel*> float_map_label;
  map<string, QLineEdit*> float_map_obj;
};

class simulationTab : public QWidget
{
public:
  simulationTab(QWidget *parent = 0);
  
private:
  QGridLayout *simulationLayout;
};



class BooksimGUI : public QWidget
{
  //Qt required
  Q_OBJECT
  
public:
  BooksimGUI(QWidget *parent = 0);
  ~BooksimGUI(){}
  
  //allocsime is called by the gui instead of main, 
  void RegisterAllocSim(booksimfunc ,Configuration * cf);
protected slots:
  void run();
private:
  
  //booksim job data such as functio pointer are here for pthread
  bsjob arbeit;
  //pthread struct
  pthread_t bsthread;
  
  //Layout manger for the whole winodw
  QGridLayout *mainLayout;
  
  //each tab is declared here
  configTab *configtab;

};



#endif
