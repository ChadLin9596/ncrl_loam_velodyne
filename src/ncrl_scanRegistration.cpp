 /*Copyright 2013, Ji Zhang, Carnegie Mellon University
 Further contributions copyright (c) 2016, Southwest Research Institute
 All rights reserved.

 Redistribution and use in source and binary forms, with or without
 modification, are permitted provided that the following conditions are met:

 1. Redistributions of source code must retain the above copyright notice,
    this list of conditions and the following disclaimer.
 2. Redistributions in binary form must reproduce the above copyright notice,
    this list of conditions and the following disclaimer in the documentation
    and/or other materials provided with the distribution.
 3. Neither the name of the copyright holder nor the names of its
    contributors may be used to endorse or promote products derived from this
    software without specific prior written permission.

 THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 POSSIBILITY OF SUCH DAMAGE.

 This is an implementation of the algorithm described in the following paper:
   J. Zhang and S. Singh. LOAM: Lidar Odometry and Mapping in Real-time.
     Robotics: Science and Systems Conference (RSS). Berkeley, CA, July 2014.
*/
#include <ros/ros.h>
#include <loam_velodyne/common.h>
#include <vector>
#include <opencv/cv.h>
#include <eigen3/Eigen/Dense>
#include <string>

#include <nav_msgs/Odometry.h>
#include <geometry_msgs/PoseStamped.h>
#include <geometry_msgs/Point.h>
#include <sensor_msgs/Imu.h>

#include <pcl_conversions/pcl_conversions.h>
#include <pcl/point_cloud.h>
#include <pcl/point_types.h>
#include <pcl/filters/voxel_grid.h>
#include <pcl/kdtree/kdtree_flann.h>

#include <sensor_msgs/Imu.h>
#include <sensor_msgs/PointCloud2.h>

#include <tf/transform_datatypes.h>
#include <tf/transform_broadcaster.h>

#define GRAVITY 9.81

using std::sin;
using std::cos;
using std::atan2;

const double scanPeriod = 0.1; // need to adjust by setting lidar

const int systemDelay = 20;
int systemInitCount = 0;
bool systemInited = false;

// declare imu calibrate
bool state = true;
float bias_x,bias_y,bias_z;
sensor_msgs::Imu imu_data;
int n = 501;
float sum_x_imu = 0, sum_y_imu = 0, sum_z_imu = 0;
float avg_x_imu = 0, avg_y_imu = 0, avg_z_imu = 0;
int count_imu = 0;
// declare imu calibrate

const int N_SCANS = 16;

float cloudCurvature[40000];
int cloudSortInd[40000];
int cloudNeighborPicked[40000];
int cloudLabel[40000];

int imuPointerFront = 0;
int imuPointerLast = -1;
const int imuQueLength = 200;

float imuRollStart = 0, imuPitchStart = 0, imuYawStart = 0;
float imuRollCur = 0, imuPitchCur = 0, imuYawCur = 0;

float imuVeloXStart = 0, imuVeloYStart = 0, imuVeloZStart = 0;
float imuShiftXStart = 0, imuShiftYStart = 0, imuShiftZStart = 0;

float imuVeloXCur = 0, imuVeloYCur = 0, imuVeloZCur = 0;
float imuShiftXCur = 0, imuShiftYCur = 0, imuShiftZCur = 0;

float imuShiftFromStartXCur = 0, imuShiftFromStartYCur = 0, imuShiftFromStartZCur = 0;
float imuVeloFromStartXCur = 0, imuVeloFromStartYCur = 0, imuVeloFromStartZCur = 0;

double imuTime[imuQueLength] = {0};
float imuRoll[imuQueLength] = {0};
float imuPitch[imuQueLength] = {0};
float imuYaw[imuQueLength] = {0};

float imuAccX[imuQueLength] = {0};
float imuAccY[imuQueLength] = {0};
float imuAccZ[imuQueLength] = {0};

float imuVeloX[imuQueLength] = {0};
float imuVeloY[imuQueLength] = {0};
float imuVeloZ[imuQueLength] = {0};

float imuShiftX[imuQueLength] = {0};
float imuShiftY[imuQueLength] = {0};
float imuShiftZ[imuQueLength] = {0};

ros::Publisher pubLaserCloud;
ros::Publisher pubCornerPointsSharp;
ros::Publisher pubCornerPointsLessSharp;
ros::Publisher pubSurfPointsFlat;
ros::Publisher pubSurfPointsLessFlat;
ros::Publisher pubImuTrans;

void ShiftToStartIMU(float pointTime)
{
  imuShiftFromStartXCur = imuShiftXCur - imuShiftXStart - imuVeloXStart * pointTime;
  imuShiftFromStartYCur = imuShiftYCur - imuShiftYStart - imuVeloYStart * pointTime;
  imuShiftFromStartZCur = imuShiftZCur - imuShiftZStart - imuVeloZStart * pointTime;
 /*
  float x1 = cos(imuYawStart) * imuShiftFromStartXCur - sin(imuYawStart) * imuShiftFromStartZCur;
  float y1 = imuShiftFromStartYCur;
  float z1 = sin(imuYawStart) * imuShiftFromStartXCur + cos(imuYawStart) * imuShiftFromStartZCur;

  float x2 = x1;
  float y2 = cos(imuPitchStart) * y1 + sin(imuPitchStart) * z1;
  float z2 = -sin(imuPitchStart) * y1 + cos(imuPitchStart) * z1;

  imuShiftFromStartXCur = cos(imuRollStart) * x2 + sin(imuRollStart) * y2;
  imuShiftFromStartYCur = -sin(imuRollStart) * x2 + cos(imuRollStart) * y2;
  imuShiftFromStartZCur = z2;
  */
  float x1 = cos(imuYawStart) * imuShiftFromStartXCur - sin(imuYawStart) * imuShiftFromStartYCur;
  float y1 = sin(imuYawStart) * imuShiftFromStartXCur + cos(imuYawStart) * imuShiftFromStartYCur;
  float z1 = imuShiftFromStartZCur;

  float x2 = cos(imuPitchStart) * x1 + sin(imuPitchStart) * z1;
  float y2 = y1;
  float z2 = -sin(imuPitchStart) * x1 + cos(imuPitchStart) * z1;

  imuShiftFromStartXCur = x2;
  imuShiftFromStartYCur = cos(imuRollStart) * y2 - sin(imuRollStart) * z2;
  imuShiftFromStartZCur = sin(imuRollStart) * y2 + cos(imuRollStart) * z2;

}

void VeloToStartIMU()
{
  imuVeloFromStartXCur = imuVeloXCur - imuVeloXStart;
  imuVeloFromStartYCur = imuVeloYCur - imuVeloYStart;
  imuVeloFromStartZCur = imuVeloZCur - imuVeloZStart;
  /*
  float x1 = cos(imuYawStart) * imuVeloFromStartXCur - sin(imuYawStart) * imuVeloFromStartZCur;
  float y1 = imuVeloFromStartYCur;
  float z1 = sin(imuYawStart) * imuVeloFromStartXCur + cos(imuYawStart) * imuVeloFromStartZCur;

  float x2 = x1;
  float y2 = cos(imuPitchStart) * y1 + sin(imuPitchStart) * z1;
  float z2 = -sin(imuPitchStart) * y1 + cos(imuPitchStart) * z1;

  imuVeloFromStartXCur = cos(imuRollStart) * x2 + sin(imuRollStart) * y2;
  imuVeloFromStartYCur = -sin(imuRollStart) * x2 + cos(imuRollStart) * y2;
  imuVeloFromStartZCur = z2;
  */
  float x1 = cos(imuYawStart) * imuVeloFromStartXCur - sin(imuYawStart) * imuVeloFromStartYCur;
  float y1 = sin(imuYawStart) * imuVeloFromStartXCur + cos(imuYawStart) * imuVeloFromStartYCur;
  float z1 = imuVeloFromStartZCur;

  float x2 = cos(imuPitchStart) * x1 + sin(imuPitchStart) * z1;
  float y2 = y1;
  float z2 = -sin(imuPitchStart) * x1 + cos(imuPitchStart) * z1;

  imuVeloFromStartXCur = x2;
  imuVeloFromStartYCur = -sin(imuRollStart) * z2 + cos(imuRollStart) * y2;
  imuVeloFromStartZCur = cos(imuRollStart) * z2 + sin(imuRollStart) * y2;
}

void TransformToStartIMU(PointType *p)
{
  /*
  float x1 = cos(imuRollCur) * p->x - sin(imuRollCur) * p->y;
  float y1 = sin(imuRollCur) * p->x + cos(imuRollCur) * p->y;
  float z1 = p->z;

  float x2 = x1;
  float y2 = cos(imuPitchCur) * y1 - sin(imuPitchCur) * z1;
  float z2 = sin(imuPitchCur) * y1 + cos(imuPitchCur) * z1;

  float x3 = cos(imuYawCur) * x2 + sin(imuYawCur) * z2;
  float y3 = y2;
  float z3 = -sin(imuYawCur) * x2 + cos(imuYawCur) * z2;

  float x4 = cos(imuYawStart) * x3 - sin(imuYawStart) * z3;
  float y4 = y3;
  float z4 = sin(imuYawStart) * x3 + cos(imuYawStart) * z3;

  float x5 = x4;
  float y5 = cos(imuPitchStart) * y4 + sin(imuPitchStart) * z4;
  float z5 = -sin(imuPitchStart) * y4 + cos(imuPitchStart) * z4;

  p->x = cos(imuRollStart) * x5 + sin(imuRollStart) * y5 + imuShiftFromStartXCur;
  p->y = -sin(imuRollStart) * x5 + cos(imuRollStart) * y5 + imuShiftFromStartYCur;
  p->z = z5 + imuShiftFromStartZCur;
  */
  float x1 = p->x;
  float y1 = cos(imuRollCur) * p->y - sin(imuRollCur) * p->z;
  float z1 = sin(imuRollCur) * p->y + cos(imuRollCur) * p->z;

  float x2 = sin(imuPitchCur) * z1 + cos(imuPitchCur) * x1;
  float y2 = y1;
  float z2 = cos(imuPitchCur) * z1 - sin(imuPitchCur) * x1;

  float x3 = cos(imuYawCur) * x2 - sin(imuYawCur) * y2;
  float y3 = sin(imuYawCur) * x2 + cos(imuYawCur) * y2;
  float z3 = z2;

  float x4 = cos(imuYawStart) * x3 - sin(imuYawStart) * y3;
  float y4 = sin(imuYawStart) * x3 + cos(imuYawStart) * y3;
  float z4 = z3;

  float x5 = sin(imuPitchStart) * z4 + cos(imuPitchStart) * x4;
  float y5 = y4;
  float z5 = cos(imuPitchStart) * z4 - sin(imuPitchStart) * x4;

  p->x = x5 + imuShiftFromStartXCur;
  p->y = cos(imuRollStart) * y5 - sin(imuRollStart) * z5 + imuShiftFromStartYCur;
  p->z = sin(imuRollStart) * y5 + cos(imuRollStart) * z5 + imuShiftFromStartZCur;
}

void AccumulateIMUShift()
{
  // imuPointerLast means number n-1 when n data received
  // the data were estimated from imu and magnetometer
  float roll = imuRoll[imuPointerLast];
  float pitch = imuPitch[imuPointerLast];
  float yaw = imuYaw[imuPointerLast];
  float accX = imuAccX[imuPointerLast];
  float accY = imuAccY[imuPointerLast];
  float accZ = imuAccZ[imuPointerLast];

  // trans from imu frame to global frame

//  float x1 = cos(roll)*accX - sin(roll)*accY;
//  float y1 = sin(roll)*accX + cos(roll)*accY;
//  float z1 = accZ;

//  float x2 = x1;
//  float y2 = cos(pitch)*y1 - sin(pitch)*z1;
//  float z2 = sin(pitch)*y1 + cos(pitch)*z1;

//  accX     = cos(yaw)*x2 + sin(yaw)*z2;
//  accY     = y2;
//  accZ     = -sin(yaw)*x2 + cos(yaw)*z2;

  float x1 = accX;
  float y1 = cos(roll)*accY - sin(roll)*accZ;
  float z1 = sin(roll)*accY + cos(roll)*accZ;

  float x2 = sin(pitch)*z1 + cos(pitch)*x1;
  float y2 = y1;
  float z2 = cos(pitch)*z1 - sin(pitch)*x1;

  accX     = cos(yaw)*x2 - sin(yaw)*y2;
  accY     = sin(yaw)*x2 + cos(yaw)*y2;
  accZ     = z2;

  printf ("world frame : x : %f\t y : %f\t z : %f\n",accX,accY,accZ);

  // imuPointerBack means number n-2 when n data received
  int imuPointerBack = (imuPointerLast + imuQueLength - 1) % imuQueLength;
  double timeDiff = imuTime[imuPointerLast] - imuTime[imuPointerBack];
  // scanPeriod = 0.1 if imu update faster than point cloud
  if (timeDiff < scanPeriod) {
    // delta distance = distance_before + velocity * t + 0.5 * acceleration + t^2 [world frame]
    imuShiftX[imuPointerLast] = imuShiftX[imuPointerBack] + imuVeloX[imuPointerBack] * timeDiff
                              + accX * pow(timeDiff,2) / 2;
    imuShiftY[imuPointerLast] = imuShiftY[imuPointerBack] + imuVeloY[imuPointerBack] * timeDiff
                              + accY * pow(timeDiff,2) / 2;
    imuShiftZ[imuPointerLast] = imuShiftZ[imuPointerBack] + imuVeloZ[imuPointerBack] * timeDiff
                              + accZ * pow(timeDiff,2) / 2;
    // update velocity v = v0 + at [world frame]
    imuVeloX[imuPointerLast] = imuVeloX[imuPointerBack] + accX * timeDiff;
    imuVeloY[imuPointerLast] = imuVeloY[imuPointerBack] + accY * timeDiff;
    imuVeloZ[imuPointerLast] = imuVeloZ[imuPointerBack] + accZ * timeDiff;
  }
}

void cb_laserCloud(const sensor_msgs::PointCloud2ConstPtr& laserCloudMsg)
{
  // initial state is true to check that imu bias
  if (!state){
    if (!systemInited) {
      systemInitCount++;
      // systemDelay is 20
      if (systemInitCount >= systemDelay) {
        systemInited = true;
      }
      return;
    }

    std::vector<int> scanStartInd(N_SCANS, 0);

    std::vector<int> scanEndInd(N_SCANS, 0);

    // trans ros msg to pcl msg and remove useless point
    double timeScanCur = laserCloudMsg->header.stamp.toSec();
    pcl::PointCloud<pcl::PointXYZ> laserCloudIn;
    pcl::fromROSMsg(*laserCloudMsg, laserCloudIn);
    std::vector<int> indices;
    pcl::removeNaNFromPointCloud(laserCloudIn, laserCloudIn, indices);
    int cloudSize = laserCloudIn.points.size();

    // caculate the start & end orientation ; atan2 count -pi to pi
    float startOri = -atan2(laserCloudIn.points[0].y, laserCloudIn.points[0].x);
    float endOri = -atan2(laserCloudIn.points[cloudSize - 1].y,laserCloudIn.points[cloudSize - 1].x) + 2 * M_PI;
    // keep endOri - startOri is between 2pi to pi
    if (endOri - startOri > 3 * M_PI) {
      endOri -= 2 * M_PI;
    } else if (endOri - startOri < M_PI) {
      endOri += 2 * M_PI;
    }

    bool halfPassed = false;
    int count = cloudSize;
    PointType point;
    std::vector<pcl::PointCloud<PointType> > laserCloudScans(N_SCANS); // 16 vector of pointcloud type

    //===================================
    for (int i = 0; i < cloudSize; i++) {
      // project lidar on camera
  //    point.x = laserCloudIn.points[i].y;
  //    point.y = laserCloudIn.points[i].z;
  //    point.z = laserCloudIn.points[i].x;

      point.x = laserCloudIn.points[i].x;
      point.y = laserCloudIn.points[i].y;
      point.z = laserCloudIn.points[i].z;

      // classfy each point into 360 degree
      float angle = rad2deg( atan( point.z/ sqrt(pow(point.y,2)+pow(point.x,2)) ) );
      int scanID;
      //rounded angle should between -15 to 15
      int roundedAngle = int(angle + (angle<0.0?-0.5:+0.5)); // means if angle <0 then -0.5 else 0.5 -90-90

      // chad question (each ring is 1.875 degree)
      if (roundedAngle > 0){
        scanID = roundedAngle;
      }
      else {
        scanID = roundedAngle + (N_SCANS-1);
      }
      if (scanID > (N_SCANS-1) || scanID<0 ){
        count--;
        continue;
      }

      // declare ori on body frame
      float ori = -atan2(point.y, point.x);
      if (!halfPassed) {
        if (ori < startOri - M_PI / 2) {
          ori += 2 * M_PI;
        }
        else if (ori > startOri + M_PI * 3 / 2) {
          ori -= 2 * M_PI;
        }
        if (ori - startOri > M_PI) {
          halfPassed = true;
        }
      }
      else {
        ori += 2 * M_PI;
        if (ori < endOri - M_PI * 3 / 2) {
          ori += 2 * M_PI;
        }
        else if (ori > endOri + M_PI / 2) {
          ori -= 2 * M_PI;
        }
      }

      // caculate relative scan time based on point orientation
      float relTime = (ori - startOri) /(endOri - startOri);
      // scanPeriod = 0.1 and means scanPeriod * relTime won't exceed 0.1
      point.intensity = scanID + scanPeriod * relTime; // integer = scanID float = scan time

      // interact with imu
      //===================================
      if (imuPointerLast >= 0) {
        float pointTime = relTime * scanPeriod;
       //imuPointerFront = the time brfore point i
        while (imuPointerFront != imuPointerLast) {
          //timeScanCur =lidar point current time
          if (timeScanCur + pointTime < imuTime[imuPointerFront]) {
            break;
          }
          imuPointerFront = (imuPointerFront + 1) % imuQueLength;
        }

        // purpose : we want time of point i is larger than imu time so we can compare the motion
        if (timeScanCur + pointTime > imuTime[imuPointerFront]) {
          imuRollCur = imuRoll[imuPointerFront];
          imuPitchCur = imuPitch[imuPointerFront];
          imuYawCur = imuYaw[imuPointerFront];

          imuVeloXCur = imuVeloX[imuPointerFront];
          imuVeloYCur = imuVeloY[imuPointerFront];
          imuVeloZCur = imuVeloZ[imuPointerFront];

          imuShiftXCur = imuShiftX[imuPointerFront];
          imuShiftYCur = imuShiftY[imuPointerFront];
          imuShiftZCur = imuShiftZ[imuPointerFront];
        } else {
          // imuPointerBack  = imuPointerFront - 1  linear interpolation to cpmpute the angle, offset, velocity of imu
          int imuPointerBack = (imuPointerFront + imuQueLength - 1) % imuQueLength;
          float ratioFront = (timeScanCur + pointTime - imuTime[imuPointerBack])
                           / (imuTime[imuPointerFront] - imuTime[imuPointerBack]);
          float ratioBack = (imuTime[imuPointerFront] - timeScanCur - pointTime)
                          / (imuTime[imuPointerFront] - imuTime[imuPointerBack]);

          // interpolation to compute roll, pitch & yaw
          imuRollCur = imuRoll[imuPointerFront] * ratioFront + imuRoll[imuPointerBack] * ratioBack;
          imuPitchCur = imuPitch[imuPointerFront] * ratioFront + imuPitch[imuPointerBack] * ratioBack;
          if (imuYaw[imuPointerFront] - imuYaw[imuPointerBack] > M_PI) {
            imuYawCur = imuYaw[imuPointerFront] * ratioFront + (imuYaw[imuPointerBack] + 2 * M_PI) * ratioBack;
          } else if (imuYaw[imuPointerFront] - imuYaw[imuPointerBack] < -M_PI) {
            imuYawCur = imuYaw[imuPointerFront] * ratioFront + (imuYaw[imuPointerBack] - 2 * M_PI) * ratioBack;
          } else {
            imuYawCur = imuYaw[imuPointerFront] * ratioFront + imuYaw[imuPointerBack] * ratioBack;
          }

          // interpolation to compute velocity
          imuVeloXCur = imuVeloX[imuPointerFront] * ratioFront + imuVeloX[imuPointerBack] * ratioBack;
          imuVeloYCur = imuVeloY[imuPointerFront] * ratioFront + imuVeloY[imuPointerBack] * ratioBack;
          imuVeloZCur = imuVeloZ[imuPointerFront] * ratioFront + imuVeloZ[imuPointerBack] * ratioBack;

          // interpolation to compute shift
          imuShiftXCur = imuShiftX[imuPointerFront] * ratioFront + imuShiftX[imuPointerBack] * ratioBack;
          imuShiftYCur = imuShiftY[imuPointerFront] * ratioFront + imuShiftY[imuPointerBack] * ratioBack;
          imuShiftZCur = imuShiftZ[imuPointerFront] * ratioFront + imuShiftZ[imuPointerBack] * ratioBack;
        }
        if (i == 0) {
          imuRollStart = imuRollCur;
          imuPitchStart = imuPitchCur;
          imuYawStart = imuYawCur;

          imuVeloXStart = imuVeloXCur;
          imuVeloYStart = imuVeloYCur;
          imuVeloZStart = imuVeloZCur;

          imuShiftXStart = imuShiftXCur;
          imuShiftYStart = imuShiftYCur;
          imuShiftZStart = imuShiftZCur;
        } else {
          // trans lidar offset to imu initial frame
          ShiftToStartIMU(pointTime);
          // trans lidar velocity to imu initial frame
          VeloToStartIMU();
          // trans point to imu initial frame
          TransformToStartIMU(&point);
        }
        //===================================
      }
      // classify each point into 16 scan
      laserCloudScans[scanID].push_back(point);
    }
    //===================================

    cloudSize = count;

    pcl::PointCloud<PointType>::Ptr laserCloud(new pcl::PointCloud<PointType>());
    // assign each ring into Point Cloud
    for (int i = 0; i < N_SCANS; i++) {
      *laserCloud += laserCloudScans[i];
    }

    int scanCount = -1;
    for (int i = 5; i < cloudSize - 5; i++) {
      // compare point i and other 10 points to caculate the smooth
      float diffX = laserCloud->points[i - 5].x + laserCloud->points[i - 4].x
                  + laserCloud->points[i - 3].x + laserCloud->points[i - 2].x
                  + laserCloud->points[i - 1].x - 10 * laserCloud->points[i].x
                  + laserCloud->points[i + 1].x + laserCloud->points[i + 2].x
                  + laserCloud->points[i + 3].x + laserCloud->points[i + 4].x
                  + laserCloud->points[i + 5].x;
      float diffY = laserCloud->points[i - 5].y + laserCloud->points[i - 4].y
                  + laserCloud->points[i - 3].y + laserCloud->points[i - 2].y
                  + laserCloud->points[i - 1].y - 10 * laserCloud->points[i].y
                  + laserCloud->points[i + 1].y + laserCloud->points[i + 2].y
                  + laserCloud->points[i + 3].y + laserCloud->points[i + 4].y
                  + laserCloud->points[i + 5].y;
      float diffZ = laserCloud->points[i - 5].z + laserCloud->points[i - 4].z
                  + laserCloud->points[i - 3].z + laserCloud->points[i - 2].z
                  + laserCloud->points[i - 1].z - 10 * laserCloud->points[i].z
                  + laserCloud->points[i + 1].z + laserCloud->points[i + 2].z
                  + laserCloud->points[i + 3].z + laserCloud->points[i + 4].z
                  + laserCloud->points[i + 5].z;

      // all are vector [40000];
      cloudCurvature[i] = pow(diffX,2) + pow(diffY,2) + pow(diffZ,2);
      cloudSortInd[i] = i;
      cloudNeighborPicked[i] = 0;
      cloudLabel[i] = 0;

      if (int(laserCloud->points[i].intensity) != scanCount) {
        scanCount = int(laserCloud->points[i].intensity);

        if (scanCount > 0 && scanCount < N_SCANS) {
          scanStartInd[scanCount] = i + 5;
          scanEndInd[scanCount - 1] = i - 5;
        }
      }
    }
    scanStartInd[0] = 5;
    scanEndInd.back() = cloudSize - 5;

    // compare the nearst point and target point's diff
    for (int i = 5; i < cloudSize - 6; i++) {
      float diffX = laserCloud->points[i + 1].x - laserCloud->points[i].x;
      float diffY = laserCloud->points[i + 1].y - laserCloud->points[i].y;
      float diffZ = laserCloud->points[i + 1].z - laserCloud->points[i].z;
      float diff = pow(diffX,2) + pow(diffY,2) + pow(diffZ,2);


      if (diff > 0.1) {
        // find the distance of each point away from lidar
        float depth1 = sqrt(pow(laserCloud->points[i].x,2)+
                            pow(laserCloud->points[i].y,2)+
                            pow(laserCloud->points[i].z,2));

        float depth2 = sqrt(pow(laserCloud->points[i + 1].x,2)+
                            pow(laserCloud->points[i + 1].y,2)+
                            pow(laserCloud->points[i + 1].z,2));
        // set the confine of mapping of (b)
        if (depth1 > depth2) {
          diffX = laserCloud->points[i + 1].x - laserCloud->points[i].x * depth2 / depth1;
          diffY = laserCloud->points[i + 1].y - laserCloud->points[i].y * depth2 / depth1;
          diffZ = laserCloud->points[i + 1].z - laserCloud->points[i].z * depth2 / depth1;

          if (sqrt(pow(diffX,2) + pow(diffY,2) + pow(diffZ,2)) / depth2 < 0.1) {
            cloudNeighborPicked[i - 5] = 1;
            cloudNeighborPicked[i - 4] = 1;
            cloudNeighborPicked[i - 3] = 1;
            cloudNeighborPicked[i - 2] = 1;
            cloudNeighborPicked[i - 1] = 1;
            cloudNeighborPicked[i] = 1;
          }
        } else {
          diffX = laserCloud->points[i + 1].x * depth1 / depth2 - laserCloud->points[i].x;
          diffY = laserCloud->points[i + 1].y * depth1 / depth2 - laserCloud->points[i].y;
          diffZ = laserCloud->points[i + 1].z * depth1 / depth2 - laserCloud->points[i].z;

          if (sqrt(diffX * diffX + diffY * diffY + diffZ * diffZ) / depth1 < 0.1) {
            cloudNeighborPicked[i + 1] = 1;
            cloudNeighborPicked[i + 2] = 1;
            cloudNeighborPicked[i + 3] = 1;
            cloudNeighborPicked[i + 4] = 1;
            cloudNeighborPicked[i + 5] = 1;
            cloudNeighborPicked[i + 6] = 1;
          }
        }
      }

      // set the confine of mapping of (a)
      float diffX2 = laserCloud->points[i].x - laserCloud->points[i - 1].x;
      float diffY2 = laserCloud->points[i].y - laserCloud->points[i - 1].y;
      float diffZ2 = laserCloud->points[i].z - laserCloud->points[i - 1].z;
      float diff2 = pow(diffX2,2) + pow(diffY2,2) + pow(diffZ2,2);

      float dis = pow(laserCloud->points[i].x,2)+ pow(laserCloud->points[i].y,2)+ pow(laserCloud->points[i].z,2);

      if (diff > 0.0002 * dis && diff2 > 0.0002 * dis) {
        cloudNeighborPicked[i] = 1;
      }
    }


    pcl::PointCloud<PointType> cornerPointsSharp;
    pcl::PointCloud<PointType> cornerPointsLessSharp;
    pcl::PointCloud<PointType> surfPointsFlat;
    pcl::PointCloud<PointType> surfPointsLessFlat;

    for (int i = 0; i < N_SCANS; i++) {
      pcl::PointCloud<PointType>::Ptr surfPointsLessFlatScan(new pcl::PointCloud<PointType>);
      for (int j = 0; j < 6; j++) {
        int sp = (scanStartInd[i] * (6 - j)  + scanEndInd[i] * j) / 6;
        int ep = (scanStartInd[i] * (5 - j)  + scanEndInd[i] * (j + 1)) / 6 - 1;

        for (int k = sp + 1; k <= ep; k++) {
          for (int l = k; l >= sp + 1; l--) {
            if (cloudCurvature[cloudSortInd[l]] < cloudCurvature[cloudSortInd[l - 1]]) {
              int temp = cloudSortInd[l - 1];
              cloudSortInd[l - 1] = cloudSortInd[l];
              cloudSortInd[l] = temp;
            }
          }
        }

        int largestPickedNum = 0;
        for (int k = ep; k >= sp; k--) {
          int ind = cloudSortInd[k];
          if (cloudNeighborPicked[ind] == 0 &&
              cloudCurvature[ind] > 0.1) {

            largestPickedNum++;
            if (largestPickedNum <= 2) {
              cloudLabel[ind] = 2;
              cornerPointsSharp.push_back(laserCloud->points[ind]);
              cornerPointsLessSharp.push_back(laserCloud->points[ind]);
            } else if (largestPickedNum <= 20) {
              cloudLabel[ind] = 1;
              cornerPointsLessSharp.push_back(laserCloud->points[ind]);
            } else {
              break;
            }

            cloudNeighborPicked[ind] = 1;
            for (int l = 1; l <= 5; l++) {
              float diffX = laserCloud->points[ind + l].x
                          - laserCloud->points[ind + l - 1].x;
              float diffY = laserCloud->points[ind + l].y
                          - laserCloud->points[ind + l - 1].y;
              float diffZ = laserCloud->points[ind + l].z
                          - laserCloud->points[ind + l - 1].z;
              if (pow(diffX,2) + pow(diffY,2) + pow(diffZ,2) > 0.05) {
                break;
              }

              cloudNeighborPicked[ind + l] = 1;
            }
            for (int l = -1; l >= -5; l--) {
              float diffX = laserCloud->points[ind + l].x - laserCloud->points[ind + l + 1].x;
              float diffY = laserCloud->points[ind + l].y - laserCloud->points[ind + l + 1].y;
              float diffZ = laserCloud->points[ind + l].z - laserCloud->points[ind + l + 1].z;
              if (pow(diffX,2)+pow(diffY,2)+pow(diffZ,2) > 0.05) {
                break;
              }

              cloudNeighborPicked[ind + l] = 1;
            }
          }
        }

        int smallestPickedNum = 0;
        for (int k = sp; k <= ep; k++) {
          int ind = cloudSortInd[k];
          if (cloudNeighborPicked[ind] == 0 &&
              cloudCurvature[ind] < 0.1) {

            cloudLabel[ind] = -1;
            surfPointsFlat.push_back(laserCloud->points[ind]);

            smallestPickedNum++;
            if (smallestPickedNum >= 4) {
              break;
            }

            cloudNeighborPicked[ind] = 1;
            for (int l = 1; l <= 5; l++) {
              float diffX = laserCloud->points[ind + l].x
                          - laserCloud->points[ind + l - 1].x;
              float diffY = laserCloud->points[ind + l].y
                          - laserCloud->points[ind + l - 1].y;
              float diffZ = laserCloud->points[ind + l].z
                          - laserCloud->points[ind + l - 1].z;
              if (diffX * diffX + diffY * diffY + diffZ * diffZ > 0.05) {
                break;
              }

              cloudNeighborPicked[ind + l] = 1;
            }
            for (int l = -1; l >= -5; l--) {
              float diffX = laserCloud->points[ind + l].x
                          - laserCloud->points[ind + l + 1].x;
              float diffY = laserCloud->points[ind + l].y
                          - laserCloud->points[ind + l + 1].y;
              float diffZ = laserCloud->points[ind + l].z
                          - laserCloud->points[ind + l + 1].z;
              if (diffX * diffX + diffY * diffY + diffZ * diffZ > 0.05) {
                break;
              }

              cloudNeighborPicked[ind + l] = 1;
            }
          }
        }

        for (int k = sp; k <= ep; k++) {
          if (cloudLabel[k] <= 0) {
            surfPointsLessFlatScan->push_back(laserCloud->points[k]);
          }
        }
      }

      pcl::PointCloud<PointType> surfPointsLessFlatScanDS;
      pcl::VoxelGrid<PointType> downSizeFilter;
      downSizeFilter.setInputCloud(surfPointsLessFlatScan);
      downSizeFilter.setLeafSize(0.2, 0.2, 0.2);
      downSizeFilter.filter(surfPointsLessFlatScanDS);

      surfPointsLessFlat += surfPointsLessFlatScanDS;
    }

    sensor_msgs::PointCloud2 laserCloudOutMsg;
    pcl::toROSMsg(*laserCloud, laserCloudOutMsg);
    laserCloudOutMsg.header.stamp = laserCloudMsg->header.stamp;
    laserCloudOutMsg.header.frame_id = "/velodyne";
    pubLaserCloud.publish(laserCloudOutMsg);

    sensor_msgs::PointCloud2 cornerPointsSharpMsg;
    pcl::toROSMsg(cornerPointsSharp, cornerPointsSharpMsg);
    cornerPointsSharpMsg.header.stamp = laserCloudMsg->header.stamp;
    cornerPointsSharpMsg.header.frame_id = "/velodyne";
    pubCornerPointsSharp.publish(cornerPointsSharpMsg);

    sensor_msgs::PointCloud2 cornerPointsLessSharpMsg;
    pcl::toROSMsg(cornerPointsLessSharp, cornerPointsLessSharpMsg);
    cornerPointsLessSharpMsg.header.stamp = laserCloudMsg->header.stamp;
    cornerPointsLessSharpMsg.header.frame_id = "/velodyne";
    pubCornerPointsLessSharp.publish(cornerPointsLessSharpMsg);

    sensor_msgs::PointCloud2 surfPointsFlat2;
    pcl::toROSMsg(surfPointsFlat, surfPointsFlat2);
    surfPointsFlat2.header.stamp = laserCloudMsg->header.stamp;
    surfPointsFlat2.header.frame_id = "/velodyne";
    pubSurfPointsFlat.publish(surfPointsFlat2);

    sensor_msgs::PointCloud2 surfPointsLessFlat2;
    pcl::toROSMsg(surfPointsLessFlat, surfPointsLessFlat2);
    surfPointsLessFlat2.header.stamp = laserCloudMsg->header.stamp;
    surfPointsLessFlat2.header.frame_id = "/velodyne";
    pubSurfPointsLessFlat.publish(surfPointsLessFlat2);

    pcl::PointCloud<pcl::PointXYZ> imuTrans(4, 1);
    imuTrans.points[0].x = imuPitchStart;
    imuTrans.points[0].y = imuYawStart;
    imuTrans.points[0].z = imuRollStart;

    imuTrans.points[1].x = imuPitchCur;
    imuTrans.points[1].y = imuYawCur;
    imuTrans.points[1].z = imuRollCur;

    imuTrans.points[2].x = imuShiftFromStartXCur;
    imuTrans.points[2].y = imuShiftFromStartYCur;
    imuTrans.points[2].z = imuShiftFromStartZCur;

    imuTrans.points[3].x = imuVeloFromStartXCur;
    imuTrans.points[3].y = imuVeloFromStartYCur;
    imuTrans.points[3].z = imuVeloFromStartZCur;

    sensor_msgs::PointCloud2 imuTransMsg;
    pcl::toROSMsg(imuTrans, imuTransMsg);
    imuTransMsg.header.stamp = laserCloudMsg->header.stamp;
    imuTransMsg.header.frame_id = "/velodyne";
    pubImuTrans.publish(imuTransMsg);
  }
}

void cb_imu(const sensor_msgs::Imu::ConstPtr& imuIn)
{
  // assume the roll and pitch are all zero
  // compute the system error without gravity
  // imu frame is NED
  if (state){
    imu_data = *imuIn;
    if (imu_data.linear_acceleration.x != 0 && imu_data.linear_acceleration.y != 0){

      sum_x_imu += imu_data.linear_acceleration.x;
      sum_y_imu += imu_data.linear_acceleration.y;
      sum_z_imu += imu_data.linear_acceleration.z;

      count_imu += 1;

      avg_x_imu = sum_x_imu / count_imu;
      avg_y_imu = sum_y_imu / count_imu;
      avg_z_imu = sum_z_imu / count_imu;
      ROS_INFO("computing bias ... %d",count_imu);
    }
    if(count_imu == n-1){
      ROS_INFO("---Start calculate---");
      bias_x = sum_x_imu / (count_imu);
      bias_y = sum_y_imu / (count_imu);
      bias_z = (sum_z_imu / (count_imu))-9.81;
      ROS_INFO("bias_x = %f, bias_y = %f, bias_z = %f", bias_x, bias_y, bias_z);
      count_imu += 1;
      ROS_INFO("---Finish---");
      state = false;
    }
  }

  // start
  if (state != true){
    //ROS_INFO("scan receive");
    double roll, pitch, yaw;
    tf::Quaternion orientation;
    tf::quaternionMsgToTF(imuIn->orientation, orientation);
    tf::Matrix3x3(orientation).getRPY(roll, pitch, yaw);

    printf("Roll : %f\t Pitch : %f\t Yaw : %f\n",roll,pitch,yaw);

    // delete the gravity effect
    float accX = imuIn->linear_acceleration.x + sin(pitch)*cos(roll)*9.81 - bias_x;
    float accY = imuIn->linear_acceleration.y - sin(roll)*cos(pitch)*9.81 - bias_y;
    float accZ = imuIn->linear_acceleration.z - cos(roll)*cos(pitch)*9.81 - bias_z;

    printf("acc x  : %f\t y : %f\t z : %f\n",accX,accY,accZ);

    // initial imuPointerLast is -1  imuQueLength = 200
    imuPointerLast = (imuPointerLast + 1) % imuQueLength;

    // ENU imu frame
    imuTime [imuPointerLast] = imuIn->header.stamp.toSec();
    imuRoll [imuPointerLast] = roll;
    imuPitch[imuPointerLast] = pitch;
    imuYaw  [imuPointerLast] = yaw;
    imuAccX [imuPointerLast] = accX;
    imuAccY [imuPointerLast] = accY;
    imuAccZ [imuPointerLast] = accZ;

    AccumulateIMUShift();
  }
}

int main(int argc, char** argv)
{
  //ros::init(argc, argv, "scanRegistration");
  ros::init(argc, argv, "ncrl_scanRegistration");
  ros::NodeHandle nh;
  // declare subscriber
  ros::Subscriber subLaserCloud = nh.subscribe<sensor_msgs::PointCloud2> ("/velodyne_points", 2, cb_laserCloud);
  ros::Subscriber subImu = nh.subscribe<sensor_msgs::Imu> ("/imu/data", 50, cb_imu);
  // declare publisher
  pubLaserCloud = nh.advertise<sensor_msgs::PointCloud2> ("/velodyne_cloud_2", 2);
  pubCornerPointsSharp = nh.advertise<sensor_msgs::PointCloud2> ("/laser_cloud_sharp", 2);
  pubCornerPointsLessSharp = nh.advertise<sensor_msgs::PointCloud2> ("/laser_cloud_less_sharp", 2);
  pubSurfPointsFlat = nh.advertise<sensor_msgs::PointCloud2> ("/laser_cloud_flat", 2);
  pubSurfPointsLessFlat = nh.advertise<sensor_msgs::PointCloud2> ("/laser_cloud_less_flat", 2);
  pubImuTrans = nh.advertise<sensor_msgs::PointCloud2> ("/imu_trans", 5);

  ros::spin();

  return 0;
}

