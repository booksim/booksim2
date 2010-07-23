
#include <sstream>
#include "globals.hpp"
#include "booksim.hpp"
#include "bgui.hpp"

//pthread function
void* launchbooksim(void* j){
  bsjob* job =(bsjob*)j;
  job->status=1; //lol this is definitley not atomic
  job->bs(*(job->config)); 
  job->status=0;
}

BooksimGUI::BooksimGUI(QWidget *parent)
  : QWidget(parent)
{

  arbeit.bs = 0;
  arbeit.config = 0;
  arbeit.status = 0;
 
  //tab declaration
  mainLayout = new QGridLayout;
  QTabWidget *tabWidget = new QTabWidget; 
  mainLayout->addWidget(tabWidget,0,0);
  configtab = new configTab(this);
  tabWidget->addTab(configtab, tr("Configuration"));
  tabWidget->addTab(new simulationTab, tr("Simulations"));

  //run button
  QPushButton *go_button = new QPushButton("Run Simulation");
  connect(go_button, SIGNAL(clicked()), this, SLOT(run()));
  mainLayout->addWidget(go_button, 1, 0);  

  setLayout(mainLayout); 
}

//transfer all the control and data from main.cpp
void BooksimGUI::RegisterAllocSim(booksimfunc wut, Configuration * cf){
  arbeit.bs = wut;
  arbeit.config = cf;
  configtab->setup(cf);
}

//spawn A thread
void BooksimGUI::run(){
  if(arbeit.status!=1){
    cout<<"Starting booksim run"<<endl;
    assert(arbeit.bs!=0 && arbeit.config!=0);
    pthread_create(&bsthread, NULL,launchbooksim,(void *)&arbeit);
  } else {
    cout<<"simulation already running\n";
  }
}

configTab::configTab(QWidget *parent)
  :QWidget(parent){
  configLayout = new QGridLayout;
  config = 0;
  show_advanced = false;
  setLayout(configLayout); 
  
}

void configTab::setup( Configuration* cf){
  
  config = cf;

  //differentiate which options are part of the "important_map"
  map<string, int> not_advanced_list;
  //data directly from booksim config
  map<string, char*> *str_map = cf->GetStrMap();
  map<string, unsigned int> *int_map = cf->GetIntMap();
  map<string, double> *float_map = cf->GetFloatMap();
  //gui display data from booksim config
  vector< pair<string, vector< string> > > *important_map = cf->GetImportantMap(); 
  hmax = 0;
  vmax = 0;
  int grid_hindex = 0;
  int grid_vindex = 0;


  /////////Create a label and a textbox for each option, but they are not attached to anyting widgets yet

  str_map_obj.clear();
  str_map_label.clear();
  for( map<string, char*>::const_iterator i = str_map->begin();
       i!=str_map->end();
       i++){
    QLabel* tlabel = new QLabel(i->first.c_str());
    str_map_label[i->first] = tlabel;
    QLineEdit* tline = new QLineEdit();
    tline->setText(i->second);
    str_map_obj[i->first] = tline;

  }
  

  stringstream numtochar;
  int_map_obj.clear();
  int_map_label.clear();
  for( map<string, unsigned int>::const_iterator i = int_map->begin();
       i!=int_map->end();
       i++){
    QLabel* tlabel = new QLabel(i->first.c_str());
    int_map_label[i->first] = tlabel;
    QLineEdit* tline = new QLineEdit();
    numtochar<<i->second;
    tline->setText(numtochar.str().c_str());
    numtochar.str("");
    int_map_obj[i->first] = tline;

  }

  float_map_obj.clear();
  float_map_label.clear();
  for( map<string, double>::const_iterator i = float_map->begin();
       i!=float_map->end();
       i++){
    QLabel* tlabel = new QLabel(i->first.c_str());
    float_map_label[i->first] = tlabel;
    QLineEdit* tline = new QLineEdit();
    numtochar<<i->second;
    tline->setText(numtochar.str().c_str());
      numtochar.str("");
    float_map_obj[i->first] = tline;
  }

  ///////Attach the widgets of important variables

  for(int i = 0; i<important_map->size(); i++){
    QLabel* theader = new QLabel((*important_map)[i].first.c_str());
    theader->setFont(QFont("Times", 18, QFont::Bold));
    configLayout->addWidget(theader, grid_vindex++, grid_hindex,1,2,Qt::AlignHCenter );

    for(int j = 0; j<(*important_map)[i].second.size(); j++){
      QLabel* tlabel = 0;
      QLineEdit* tline = 0;
      if(str_map_obj.find((*important_map)[i].second[j])!=str_map_obj.end()){
	tlabel = (str_map_label.find((*important_map)[i].second[j]))->second;
	tline = (str_map_obj.find((*important_map)[i].second[j]))->second;
      } else if(int_map_obj.find((*important_map)[i].second[j])!=int_map_obj.end()){
      	tlabel = (int_map_label.find((*important_map)[i].second[j]))->second;
	tline = (int_map_obj.find((*important_map)[i].second[j]))->second;

      } else if(float_map_obj.find((*important_map)[i].second[j])!=float_map_obj.end()){
	tlabel = (float_map_label.find((*important_map)[i].second[j]))->second;
	tline = (float_map_obj.find((*important_map)[i].second[j]))->second;
      } else {
	cout<<"Probably a typo: "<<(*important_map)[i].second[j]<<endl;
      }
      not_advanced_list[(*important_map)[i].second[j]] = 1;

      assert(tlabel!=0 && tline !=0);
      configLayout->addWidget(tlabel, grid_vindex,grid_hindex,Qt::AlignRight );
      configLayout->addWidget(tline, grid_vindex++,grid_hindex+1);
    }
    grid_hindex+=2;
    grid_vindex=0;
    if((*important_map)[i].second.size()>vmax){
      vmax = (*important_map)[i].second.size();
    }
  }
  hmax =   grid_hindex-1;
  grid_hindex = 0;
  
  //Silly way to add dividers
  QFrame* divider_frame1  = new QFrame();
  divider_frame1->setFrameShape(QFrame::HLine);
  divider_frame1->setFrameShadow(QFrame::Sunken);
  configLayout->addWidget(divider_frame1, vmax+1, 0, 1, hmax+1);


  

  QPushButton *advanced_button = new QPushButton("Advanced Settings");
  connect(advanced_button, SIGNAL(clicked()), this, SLOT(toggleadvanced()));
  QPushButton *save_button = new QPushButton("Save Settings");
  connect(save_button, SIGNAL(clicked()), this, SLOT(saveconfig()));
  write_edit = new QLineEdit();
  QPushButton *write_button = new QPushButton("Write Config to File: ");
  connect(write_button, SIGNAL(clicked()), this, SLOT(writeconfig()));


  //can't think of any other way of adding a layoutmanager inside layout manager
  //so we add a frame to the configlayout and then add a layout to the frame
  advanced_frame  = new QFrame();
  advanced_frame->setFrameShadow(QFrame::Sunken);
  advanced_frame->hide();

  QFrame* divider_frame2  = new QFrame();
  divider_frame2->setFrameShape(QFrame::HLine);
  divider_frame2->setFrameShadow(QFrame::Sunken);
  configLayout->addWidget(divider_frame2, vmax+3, 0, 1, hmax+1);

  configLayout->addWidget(advanced_button, vmax+4, grid_hindex++);
  configLayout->addWidget(save_button, vmax+4, grid_hindex++);
  configLayout->addWidget(write_button, vmax+4, hmax-1);
  configLayout->addWidget(write_edit, vmax+4, hmax);


  QGridLayout* advancedLayout = new QGridLayout; 
  advanced_frame->setLayout(advancedLayout);


  //all the misc variables are added to the advanced frame/layoutmanager
  grid_vindex=0;
  grid_hindex=0;
  for( map<string, QLineEdit*>::const_iterator i = str_map_obj.begin();
       i!=str_map_obj.end();
       i++){
    if(not_advanced_list.find(i->first) == not_advanced_list.end()){
      QLabel* tlabel = 0;
      QLineEdit* tline = 0;
      tlabel = str_map_label.find(i->first)->second;
      tline = str_map_obj.find(i->first)->second;
      advancedLayout->addWidget(tlabel, grid_vindex,grid_hindex,Qt::AlignRight );
      advancedLayout->addWidget(tline, grid_vindex++,grid_hindex+1);
      if(grid_vindex>10){
	grid_vindex = 0;
	grid_hindex +=2;
      }
    }
  }
 
  for( map<string, QLineEdit*>::const_iterator i = int_map_obj.begin();
       i!=int_map_obj.end();
       i++){
    if(not_advanced_list.find(i->first) == not_advanced_list.end()){
      QLabel* tlabel = 0;
      QLineEdit* tline = 0;
      tlabel = int_map_label.find(i->first)->second;
      tline = int_map_obj.find(i->first)->second;
      advancedLayout->addWidget(tlabel, grid_vindex,grid_hindex,Qt::AlignRight );
      advancedLayout->addWidget(tline, grid_vindex++,grid_hindex+1);
      if(grid_vindex>10){
	grid_vindex = 0;
	grid_hindex +=2;
      }
    }
  }
  for( map<string, QLineEdit*>::const_iterator i = float_map_obj.begin();
       i!=float_map_obj.end();
       i++){
        if(not_advanced_list.find(i->first) == not_advanced_list.end()){
      QLabel* tlabel = 0;
      QLineEdit* tline = 0;
      tlabel = float_map_label.find(i->first)->second;
      tline = float_map_obj.find(i->first)->second;
      advancedLayout->addWidget(tlabel, grid_vindex,grid_hindex,Qt::AlignRight );
      advancedLayout->addWidget(tline, grid_vindex++,grid_hindex+1);
      if(grid_vindex>10){
	grid_vindex = 0;
	grid_hindex +=2;
      }
    }
  }
}

//show or not show the other hunder optoins
//the frame does not interact well with the layout manager when I "hide" it
void configTab::toggleadvanced(){
  show_advanced = !show_advanced;
  cout<<show_advanced<<endl;
  if(show_advanced){
    advanced_frame->show();
  } else {
    advanced_frame->hide();
  }
}

void configTab::writeconfig(){
  assert(config!=0);
  //the only file name check that I do, no spaces
  size_t found;
  string filename = write_edit->text().toStdString();
  found = filename.find(' ');
  if(filename.substr(0,found).length()!=0){
    cout<<"Creating Config file: "<<filename.substr(0,found)<<endl;
   config->WriteFile(filename.substr(0,found));
  } else {
    cout<<"No file name provided\n";
  } 
}


//changes to the gui are not commited and will not have an effect without
//clicking the save buttono
void configTab::saveconfig(){
  for( map<string, QLineEdit*>::const_iterator i = str_map_obj.begin();
       i!=str_map_obj.end();
       i++){
    config->Assign(i->first, i->second->text().toStdString());
  }
  
  for( map<string, QLineEdit*>::const_iterator i = int_map_obj.begin();
       i!=int_map_obj.end();
       i++){

    config->Assign(i->first, i->second->text().toUInt());
  }
  for( map<string, QLineEdit*>::const_iterator i = float_map_obj.begin();
       i!=float_map_obj.end();
       i++){ 
    config->Assign(i->first, i->second->text().toDouble());
  }
}




simulationTab::simulationTab(QWidget *parent)
  :QWidget(parent){
  simulationLayout = new QGridLayout;
  setLayout(simulationLayout); 
}
