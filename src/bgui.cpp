#include <QApplication>
#include <QWidget>
#include <QPushButton>
#include <QGridLayout>
#include <QFrame>

#include "globals.hpp"
#include "booksim.hpp"
#include "bgui.hpp"



void* launchbooksim(void* j){
  bsjob* job =(bsjob*)j;
  job->status=1; //this is definitley not atomic
  job->bs(*(job->config)); 
  job->status=0;
}

BooksimGUI::BooksimGUI(QWidget *parent)
  : QWidget(parent)
{

  arbeit.bs = 0;
  arbeit.config = 0;
  arbeit.status = 0;
  QPushButton *go = new QPushButton("Run Simulation");
  connect(go, SIGNAL(clicked()), this, SLOT(run()));

  QGridLayout *gridLayout = new QGridLayout;
  gridLayout->addWidget(go, 0, 0);
  setLayout(gridLayout);
  
}
void BooksimGUI::RegisterAllocSim(booksimfunc wut, Configuration * cf){
  arbeit.bs = wut;
  arbeit.config = cf;
}

void BooksimGUI::run(){
  if(arbeit.status!=1){
    cout<<"Starting booksim run"<<endl;
    assert(arbeit.bs!=0 && arbeit.config!=0);
    pthread_create(&bsthread, NULL,launchbooksim,(void *)&arbeit);
  } else {
    cout<<"simulation already running\n";
  }
}



