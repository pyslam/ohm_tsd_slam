/*
 * SlamNode.cpp
 *
 *  Created on: 05.05.2014
 *      Author: phil
 */

#include "SlamNode.h"
#include "Localization.h"
#include "ThreadMapping.h"
#include "ThreadGrid.h"
#include "obcore/math/mathbase.h"

namespace ohm_tsd_slam
{
SlamNode::SlamNode(void)
{
  ros::NodeHandle prvNh("~");
  std::string strVar;
  int octaveFactor = 0;
  double cellside = 0.0;
  double dVar      = 0;
  int iVar         = 0;
  double truncationRadius = 0.0;
  prvNh.param("laser_topic", strVar, std::string("simon/scan"));
  _laserSubs=_nh.subscribe(strVar, 1, &SlamNode::laserScanCallBack, this);
  prvNh.param<double>("x_off_factor", _xOffFactor, 0.2);
  prvNh.param<double>("y_off_factor", _yOffFactor, 0.5);
  prvNh.param<double>("yaw_start_offset", _yawOffset, 0.0);
  prvNh.param<int>("cell_octave_factor", octaveFactor, 10);
  prvNh.param<double>("cellsize", cellside, 0.025);
  prvNh.param<int>("truncation_radius", iVar, 3);
  truncationRadius = static_cast<double>(iVar);
  prvNh.param<bool>("range_filter", _rangeFilter, false);
  prvNh.param<double>("min_range", dVar, 0.01);
  _minRange = static_cast<float>(dVar);
  prvNh.param<double>("max_range", dVar, 30.0);
  _maxRange = static_cast<float>(dVar);
  prvNh.param<double>("low_reflectivity_range", dVar, 2.0);
  _lowReflectivityRange = static_cast<float>(dVar);
  prvNh.param<double>("occ_grid_time_interval", _gridPublishInterval, 2.0);
  prvNh.param<double>("loop_rate", _loopRate, 40.0);

  unsigned int uiVar = static_cast<unsigned int>(octaveFactor);
  if(uiVar > 15)
  {
    std::cout << __PRETTY_FUNCTION__ << " error! Unknown cell_octave_factor -> set to default!" << std::endl;
    uiVar = 10;
  }
  _initialized=false;

  _grid=new obvious::TsdGrid(cellside, obvious::LAYOUT_32x32, static_cast<obvious::EnumTsdGridLayout>(uiVar));  //obvious::LAYOUT_8192x8192
  _grid->setMaxTruncation(truncationRadius * cellside);

  unsigned int cellsPerSide = pow(2, uiVar);
  std::cout << __PRETTY_FUNCTION__ << " creating representation with " << cellsPerSide << "x" << cellsPerSide;
  double sideLength = static_cast<double>(cellsPerSide) * cellside;
  std::cout << " cells, representating "<< sideLength << "x" << sideLength << "m^2" << std::endl;

  _sensor=NULL;
  _localizer=NULL;

  _threadMapping=NULL;
  _threadGrid=NULL;
}

SlamNode::~SlamNode()
{
  if(_initialized)
  {
    _threadMapping->terminateThread();
    _threadGrid->terminateThread();
    delete _threadGrid;
    delete _threadMapping;
  }
  if(_localizer) delete _localizer;
  if(_grid) delete _grid;
  if(_sensor) delete _sensor;
}

void SlamNode::start(void)
{
  this->run();
}

double SlamNode::xOffFactor(void)const
{
  return _xOffFactor;
}

double SlamNode::yOffFactor(void)const
{
  return _yOffFactor;
}

void SlamNode::initialize(const sensor_msgs::LaserScan& initScan)
{
  _sensor=new obvious::SensorPolar2D(initScan.ranges.size(), initScan.angle_increment, initScan.angle_min, static_cast<double>(_maxRange), static_cast<double>(_minRange), static_cast<double>(_lowReflectivityRange));
  _sensor->setRealMeasurementData(initScan.ranges, 1.0);

  double phi       = _yawOffset;
  double gridWidth =_grid->getCellsX()*_grid->getCellSize();
  double gridHeight=_grid->getCellsY()*_grid->getCellSize();
  double tf[9]     ={cos(phi), -sin(phi), gridWidth*_xOffFactor,
      sin(phi),  cos(phi), gridHeight*_yOffFactor,
      0,         0,               1};
  obvious::Matrix Tinit(3, 3);
  Tinit.setData(tf);
  _sensor->transform(&Tinit);

  _threadMapping=new ThreadMapping(_grid);
  for(int i=0; i<INIT_PSHS; i++)
    _threadMapping->queuePush(_sensor);

  _localizer=new Localization(_grid, _threadMapping, _nh, &_pubMutex, *this);

  _threadGrid=new ThreadGrid(_grid, _nh, &_pubMutex, *this);

  _initialized=true;
}

void SlamNode::run(void)
{
  ros::Time lastMap=ros::Time::now();
  ros::Duration durLastMap=ros::Duration(_gridPublishInterval);
  ros::Rate rate(_loopRate);
  std::cout << __PRETTY_FUNCTION__ << " waiting for first laser scan to initialize node...\n";
  while(ros::ok())
  {
    ros::spinOnce();
    if(_initialized)
    {
      ros::Time curTime=ros::Time::now();
      if((curTime-lastMap).toSec()>durLastMap.toSec())
      {
        _threadGrid->unblock();
        lastMap=ros::Time::now();
      }
    }
    rate.sleep();
  }
}

void SlamNode::laserScanCallBack(const sensor_msgs::LaserScan& scan)
{
  if(!_initialized)
  {
    std::cout << __PRETTY_FUNCTION__ << " received first scan. Initialize node...\n";
    this->initialize(scan);
    std::cout << __PRETTY_FUNCTION__ << " initialized -> running...\n";
    return;
  }

  _sensor->setRealMeasurementData(scan.ranges, 1.0);
  _sensor->resetMask();
  _sensor->maskDepthDiscontinuity(obvious::deg2rad(3.0));
  _localizer->localize(_sensor);
}

} /* namespace ohm_tsdSlam2 */
