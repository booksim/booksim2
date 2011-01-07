
#include <sstream>
#include "globals.hpp"
#include "booksim.hpp"
#include "bgui.hpp"
#include "stats.hpp"

//pthread function
void* launchbooksim(void* j){
  bsjob* job =(bsjob*)j;
  job->status=1; //lol this is definitley not atomic
  job->return_status = job->bs(*(job->config)); 
  job->status=0;
}

BooksimGUI::BooksimGUI(QWidget *parent)
  : QWidget(parent)
{

  simulationTimer = new QTimer(this);
  connect(simulationTimer, SIGNAL(timeout()), this, SLOT(checksimulation()));
  arbeit.bs = 0;
  arbeit.config = 0;
  arbeit.status = 0;
  arbeit.return_status = false;

  //tab declaration
  mainLayout = new QGridLayout;
  QTabWidget *tabWidget = new QTabWidget; 
  mainLayout->addWidget(tabWidget,0,0);
  configtab = new configTab(this);
  simulationtab = new simulationTab;
  tabWidget->addTab(configtab, tr("Configuration"));
  tabWidget->addTab(simulationtab, tr("Simulation"));
  connect(this, SIGNAL(updatestats(bool)),simulationtab, SLOT(readystats(bool)));
  connect(this, SIGNAL(simulationstatus(bool)), simulationtab, SLOT(readystats(bool)));

  //run button
  QPushButton *go_button = new QPushButton("Run Simulation");
  connect(go_button, SIGNAL(clicked()), this, SLOT(run()));
  mainLayout->addWidget(go_button, 1, 0);  
  mainLayout->setRowStretch(1,0);
  mainLayout->setRowStretch(0,1);

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
    emit simulationstatus(false);
    cout<<"Starting booksim run"<<endl;
    assert(arbeit.bs!=0 && arbeit.config!=0);
    pthread_create(&bsthread, NULL,launchbooksim,(void *)&arbeit);
    simulationTimer->start(500);
  } else {
    cout<<"simulation already running\n";
  }
}

//chekc booksim thread
//definitely not atomic lol
void BooksimGUI::checksimulation(){
  //done
  if(arbeit.status==0){
    emit updatestats(true);
    simulationTimer->stop();
  } else { //not done

  }
}

configTab::configTab(QWidget *parent)
  :QWidget(parent){
  configLayout = new QGridLayout;
  config = 0;
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
      configLayout->setRowStretch(grid_vindex, 0);
      configLayout->addWidget(tlabel, grid_vindex,grid_hindex,Qt::AlignRight );
      configLayout->addWidget(tline, grid_vindex++,grid_hindex+1);

    }
    grid_hindex+=2;
    grid_vindex=0;
    if((*important_map)[i].second.size()>vmax){
      vmax = (*important_map)[i].second.size()+1;
    }
  }
  hmax =   grid_hindex-1;
  grid_hindex = 0;
  
  //Silly way to add dividers
  QFrame* divider_frame1  = new QFrame();
  divider_frame1->setFrameShape(QFrame::HLine);
  divider_frame1->setFrameShadow(QFrame::Sunken);
  configLayout->addWidget(divider_frame1, vmax, 0, 1, hmax+1);
  configLayout->setRowStretch(vmax, 10);

  

  QPushButton *advanced_button = new QPushButton("Advanced Settings");
  connect(advanced_button, SIGNAL(clicked()), this, SLOT(toggleadvanced()));
  QPushButton *save_button = new QPushButton("Save Settings");
  connect(save_button, SIGNAL(clicked()), this, SLOT(saveconfig()));
  write_edit = new QLineEdit();
  QPushButton *write_button = new QPushButton("Write Config to File: ");
  connect(write_button, SIGNAL(clicked()), this, SLOT(writeconfig()));

  configLayout->addWidget(advanced_button, vmax+1, grid_hindex++);
  configLayout->addWidget(save_button, vmax+1, grid_hindex++);
  configLayout->addWidget(write_button, vmax+1, hmax-1);
  configLayout->addWidget(write_edit, vmax+1, hmax);
  configLayout->setRowStretch(vmax, 0);

  //can't think of any other way of adding a layoutmanager inside layout manager
  //so we add a frame to the configlayout and then add a layout to the frame
  advanced_frame  = new QFrame();
  advanced_frame->setFrameShadow(QFrame::Sunken);
  advanced_frame->hide();
  advanced_frame->setGeometry(200, 200, 1300, 600);

  QGridLayout* advancedLayout = new QGridLayout; 
  advanced_frame->setLayout(advancedLayout);
  
  int advanced_rows = 15;
  

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
      if(grid_vindex>advanced_rows){
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
      if(grid_vindex>advanced_rows){
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
      if(grid_vindex>advanced_rows){
	grid_vindex = 0;
	grid_hindex +=2;
      }
    }
  }
}

//show or not show the other hunder optoins
//the frame does not interact well with the layout manager when I "hide" it
void configTab::toggleadvanced(){
  advanced_frame->show();
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


Histogram::Histogram(QWidget *parent)
  :QWidget(parent){
  plot_data = 0;
  bin_min = 0;
  bin_max = 100;
  
}

void Histogram::paintEvent(QPaintEvent * /* event */)
{
  QPainter painter(this);
  painter.setPen(Qt::NoPen);
  painter.setBrush(Qt::black);
  int height_max = 0;


 
  for(int i = bin_min; i<bin_max; i++){
    if(plot_data->GetBin(i) > height_max){
      height_max = plot_data->GetBin(i);
    }
  }
  painter.translate(0, rect().height());
  painter.scale(float(rect().width())/float(bin_max-bin_min), float(rect().height())/float(height_max));
  for(int i = bin_min; i<bin_max; i++){
    painter.drawRect(i-bin_min, 0,1,-plot_data->GetBin(i)); 
  }

}

Heatmap::Heatmap(QWidget *parent)
  :QWidget(parent){
  min = 0.0;
  max = 1.0;


}


QColor Heatmap::GetColor(float map){
  // by Ged Larsen
  // map a float to 4 of 6 segments of a color progression
  //
  // -1 to -0.5 <0,0,1> to <0,1,1> dark blue to light blue
  // -0.5 to 0 <0,1,1> to <0,1,0> light blue to green
  // 0 to 0.5 <0,1,0> to <1,1,0> green to yellow
  // 0.5 to 1 <1,1,0> to <1,0,0> yellow to red
  
  int fraction;
  if (map < min) {
    fraction = 0; 
  }
  else if (map > max) { 
    fraction = 255; 
  }
  else { 
    fraction = 255*map/(max-min)+127 - (max / (max-min))*255; 
  }
  
  if ( fraction <=63 ) { return QColor(0, 4*fraction, 255); }
  else if ( fraction <=127 ) { return QColor(0, 255, 256 - 4*(fraction-63)); }
  else if ( fraction <=191 ) { return QColor((fraction-127)*4-1, 255, 0); }
  else { return QColor(255, 256 - 4*(fraction - 191), 0); }
}

void Heatmap::paintEvent(QPaintEvent * /* event */)
{
  QPainter painter(this);
  painter.setPen(Qt::NoPen);
  painter.setBrush(Qt::black);


  //next

}


simulationTab::simulationTab(QWidget *parent)
  :QWidget(parent){
  simulationLayout = new QGridLayout;
  setLayout(simulationLayout); 
  curr_mode = simulationTab::GENERAL;
  stats_ready = false;

  generalFrame = new QTextEdit();
  generalFrame->setReadOnly(true);
  generalFrame->setFrameShape(QFrame::Box);
  simulationLayout->addWidget(generalFrame,1,0);



  pgraph = new Histogram();
  pmin = new QLineEdit("0");
  pmax = new QLineEdit("100");
  pset = new QPushButton("Change Scale");
  connect(pset, SIGNAL(clicked()),this,SLOT(changescale()));
  connect(this, SIGNAL(redrawhist(int)), this, SLOT(getstats(int)));
  packetFrame = new QFrame();
  packetFrame->setFrameShape(QFrame::Box);
  packetLayout = new QGridLayout;
  packetFrame->setLayout(packetLayout);
  packetLayout->addWidget(pgraph, 0,0,1,5);
  packetLayout->addWidget(new QLabel("Min"), 1,0);
  packetLayout->addWidget(pmin, 1,1);
  packetLayout->addWidget(new QLabel("Max"), 1,2);
  packetLayout->addWidget(pmax, 1,3);
  packetLayout->addWidget(pset, 1,4);
  packetLayout->setRowStretch(0,10);
  packetLayout->setRowStretch(1,0);
  simulationLayout->addWidget(packetFrame,1,0);
  packetFrame->hide();

  ngraph = new Heatmap();
  nodeFrame = new QFrame();
  nodeFrame ->setFrameShape(QFrame::Box);
  nodeLayout = new QGridLayout;
  nodeLayout->addWidget(ngraph, 0,0,1,5);
  nodeFrame->setLayout(nodeLayout);
  simulationLayout->addWidget(nodeFrame,1,0);
  nodeFrame->hide();

  channelFrame = new QFrame();
  channelFrame ->setFrameShape(QFrame::Box);
  channelLayout = new QGridLayout;
  channelFrame->setLayout(channelLayout);
  simulationLayout->addWidget(channelFrame,1,0);
  channelFrame->hide();

  mode_selector = new QComboBox(this);
  mode_selector->addItem("General");
  mode_selector->addItem("Packet Latency");
  mode_selector->addItem("Node Throughput");
  mode_selector->addItem("Channel Load");

  simulationLayout->addWidget(mode_selector, 0,0);
  connect(mode_selector, SIGNAL(activated(int)), this, SLOT(getstats(int)));
  connect(this, SIGNAL(gotostats(int)), this, SLOT(getstats(int)));
}

void simulationTab::getstats(int m){
  curr_mode = (simulationTab::StatModes)m;
  generalFrame->hide();
  packetFrame->hide();
  nodeFrame->hide();
  channelFrame->hide();
  stringstream infotext;
  if(stats_ready){
    
    switch(curr_mode){
    case simulationTab::GENERAL :
      //copied over from trafficmanager
      infotext.str("");
      infotext << "Overall minimum latency = " << GetStats("overall_min_latency_stat_0")->Average( )
	       << " (" << GetStats("overall_min_latency_stat_0")->NumSamples( ) << " samples)" << endl;
      infotext << "Overall average latency = " <<GetStats("overall_avg_latency_stat_0")->Average( )
	       << " (" <<GetStats("overall_avg_latency_stat_0")->NumSamples( ) << " samples)" << endl;
      infotext << "Overall maximum latency = " <<GetStats("overall_max_latency_stat_0")->Average( )
	       << " (" <<GetStats("overall_max_latency_stat_0")->NumSamples( ) << " samples)" << endl;
      infotext << "Overall minimum transaction latency = " <<GetStats("overall_min_tlat_stat_0")->Average( )
	       << " (" <<GetStats("overall_min_tlat_stat_0")->NumSamples( ) << " samples)" << endl;
      infotext << "Overall average transaction latency = " <<GetStats("overall_avg_tlat_stat_0")->Average( )
	       << " (" <<GetStats("overall_avg_tlat_stat_0")->NumSamples( ) << " samples)" << endl;
      infotext << "Overall maximum transaction latency = " <<GetStats("overall_max_tlat_stat_0")->Average( )
	       << " (" <<GetStats("overall_max_tlat_stat_0")->NumSamples( ) << " samples)" << endl;
      
      infotext << "Overall minimum fragmentation = " <<GetStats("overall_min_frag_stat_0")->Average( )
	       << " (" <<GetStats("overall_min_frag_stat_0")->NumSamples( ) << " samples)" << endl;
      infotext << "Overall average fragmentation = " <<GetStats("overall_avg_frag_stat_0")->Average( )
	       << " (" <<GetStats("overall_avg_frag_stat_0")->NumSamples( ) << " samples)" << endl;
      infotext << "Overall maximum fragmentation = " <<GetStats("overall_max_frag_stat_0")->Average( )
	       << " (" <<GetStats("overall_max_frag_stat_0")->NumSamples( ) << " samples)" << endl;
      infotext << "Overall average accepted rate = " <<GetStats("overall_acceptance")->Average( )
	       << " (" <<GetStats("overall_acceptance")->NumSamples( ) << " samples)" << endl;
      
      infotext << "Overall min accepted rate = " <<GetStats("overall_min_acceptance")->Average( )
	       << " (" <<GetStats("overall_min_acceptance")->NumSamples( ) << " samples)" << endl;
      
      infotext << "Average hops = " <<GetStats("hop_stats")->Average( )
	       << " (" <<GetStats("hop_stats")->NumSamples( ) << " samples)" << endl;
      generalFrame->setText(infotext.str().c_str());
      generalFrame->show();
      break;
    case simulationTab::PACKET_LATENCY   :


      pgraph->plot_data = GetStats("latency_stat_0");
      packetFrame->show();
      break;
    case simulationTab::NODE_THROUGHPUT:
      nodeFrame->show();
      break;
    case simulationTab::CHANNEL_LOAD  :
      channelFrame->show();
      break;
    default:
      break;
    }

  } else {
    generalFrame->setText("No simulation data available");
    generalFrame->show();

  }

}

void simulationTab::readystats(bool sim_status){
  stats_ready = sim_status;
  emit gotostats(int(curr_mode));
}
