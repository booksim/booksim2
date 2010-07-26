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
#include <QTimer>
#include <QComboBox>
#include <QTextEdit>
#include <QPainter>
#include <QColor>

#include <QString>
#include "booksim_config.hpp"
#include "globals.hpp"

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
  bool return_status;
} ;


class configTab : public QWidget
{
  //qt required
  Q_OBJECT
  public:
  configTab(QWidget *parent = 0);
  void setup(Configuration * cf);

public slots:
  void toggleadvanced();
  void writeconfig();
  void saveconfig();
private:
  //layout manager for the config tab
  QGridLayout *configLayout;
  //another pointer to the booksim config file
  Configuration * config;

  //displaying the misc booksim options
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

class Heatmap: public QWidget
{
public: 
  Heatmap(QWidget *parent = 0);
  
  QColor GetColor(float max);
  Stats * plot_data;

  float min;
  float max;
protected:
  void paintEvent(QPaintEvent *event);


};

class Histogram : public QWidget
{
public: 
  Histogram(QWidget *parent = 0);

  Stats * plot_data;
  int bin_min;
  int bin_max;
protected:
  void paintEvent(QPaintEvent *event);

};


class simulationTab : public QWidget
{
  //Qt required
  Q_OBJECT
  public:
  simulationTab(QWidget *parent = 0);
  
  enum StatModes { GENERAL  = 0, 
		  PACKET_LATENCY    = 1,
		  NODE_THROUGHPUT = 2,
		   CHANNEL_LOAD   = 3};

signals:
  void gotostats(int);
  void redrawhist(int);
public slots:
  void getstats(int m );
  void readystats(bool status);
  void changescale(){
    pgraph->bin_min = pmin->text().toInt();
    pgraph->bin_max = pmax->text().toInt();
    emit redrawhist(simulationTab::PACKET_LATENCY);
  }
private:
  //
  QGridLayout *simulationLayout;

  //
  QTextEdit *generalFrame;
  
  Histogram * pgraph;
  QLineEdit *pmin;
  QLineEdit *pmax;
  QPushButton *pset;
  QFrame *packetFrame;
  QGridLayout *packetLayout;

  Heatmap* ngraph;
  QFrame *nodeFrame;
  QGridLayout *nodeLayout;
  QFrame *channelFrame;
  QGridLayout *channelLayout;

  //
  QComboBox* mode_selector;

  StatModes curr_mode;
  bool stats_ready;
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
public slots:
  void run();
  void checksimulation();
signals:
  void updatestats(bool status);
  void simulationstatus(bool status);
private:
  
  //booksim job data such as functio pointer are here for pthread
  bsjob arbeit;
  //pthread struct
  pthread_t bsthread;
  
  //Layout manger for the whole winodw
  QGridLayout *mainLayout;
  
  //each tab is declared here
  configTab *configtab;
  simulationTab *simulationtab;
  //
  QTimer *simulationTimer;

};



#endif
