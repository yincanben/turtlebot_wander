/*
 * Copyright (c) 2011, Willow Garage, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the Willow Garage, Inc. nor the names of its
 *       contributors may be used to endorse or promote products derived from
 *       this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <ros/ros.h>
#include <pluginlib/class_list_macros.h>
#include <nodelet/nodelet.h>
#include <geometry_msgs/Twist.h>
#include <pcl_ros/point_cloud.h>
#include <pcl/point_types.h>
#include <visualization_msgs/Marker.h>
#include <turtlebot_msgs/SetFollowState.h>
#include <kobuki_msgs/BumperEvent.h>
#include "dynamic_reconfigure/server.h"
#include "turtlebot_follower/FollowerConfig.h"
#include <time.h>
#include <stdlib.h>
#define MAX 10



namespace turtlebot_follower
{
typedef pcl::PointCloud<pcl::PointXYZ> PointCloud;

//* The turtlebot follower nodelet.
/**
 * The turtlebot follower nodelet. Subscribes to point clouds
 * from the 3dsensor, processes them, and publishes command vel
 * messages.
 */
class TurtlebotFollower : public nodelet::Nodelet
{
public:
  /*!
   * @brief The constructor for the follower.
   * Constructor for the follower.
   */
  TurtlebotFollower() : min_y_(0.1), max_y_(0.5),
                        min_x_(-0.2), max_x_(0.2),
                        max_z_(0.8), goal_z_(0.6),
                        z_scale_(1.0), x_scale_(5.0),
                        bumper_left_pressed_(false),
                        bumper_center_pressed_(false),
                        bumper_right_pressed_(false),
                        change_direction_(false)
  {

  }

  ~TurtlebotFollower()
  {
    delete config_srv_;
  }

private:
  double min_y_; /**< The minimum y position of the points in the box. */
  double max_y_; /**< The maximum y position of the points in the box. */
  double min_x_; /**< The minimum x position of the points in the box. */
  double max_x_; /**< The maximum x position of the points in the box. */
  double max_z_; /**< The maximum z position of the points in the box. */
  double goal_z_; /**< The distance away from the robot to hold the centroid */
  double z_scale_; /**< The scaling factor for translational robot speed */
  double x_scale_; /**< The scaling factor for rotational robot speed */
  bool   enabled_; /**< Enable/disable following; just prevents motor commands */
  /// Flag for bumper's state
  bool bumper_left_pressed_;
  bool bumper_center_pressed_;
  bool bumper_right_pressed_;
  bool change_direction_ ;

  // Service for start/stop following
  ros::ServiceServer switch_srv_;

  // Dynamic reconfigure server
  dynamic_reconfigure::Server<turtlebot_follower::FollowerConfig>* config_srv_;

  /*!
   * @brief OnInit method from node handle.
   * OnInit method from node handle. Sets up the parameters
   * and topics.
   */
  virtual void onInit()
  {
    ros::NodeHandle& nh = getNodeHandle();
    //ros::NodeHandle& nh_ = getNodeHandle() ;
    ros::NodeHandle& private_nh = getPrivateNodeHandle();

    private_nh.getParam("min_y", min_y_);
    private_nh.getParam("max_y", max_y_);
    private_nh.getParam("min_x", min_x_);
    private_nh.getParam("max_x", max_x_);
    private_nh.getParam("max_z", max_z_);
    private_nh.getParam("goal_z", goal_z_);
    private_nh.getParam("z_scale", z_scale_);
    private_nh.getParam("x_scale", x_scale_);
    private_nh.getParam("enabled", enabled_);

    cmdpub_ = private_nh.advertise<geometry_msgs::Twist> ("cmd_vel", 1);
    markerpub_ = private_nh.advertise<visualization_msgs::Marker>("marker",1);
    bboxpub_ = private_nh.advertise<visualization_msgs::Marker>("bbox",1);
    sub_= nh.subscribe<PointCloud>("depth/points", 1, &TurtlebotFollower::cloudcb, this);
    //subscribe bumper_eventu
    bumper_event_sub_ = private_nh.subscribe<kobuki_msgs::BumperEventConstPtr>("events/bumper",10,&TurtlebotFollower::bumperEventCB ,this);

    switch_srv_ = private_nh.advertiseService("change_state", &TurtlebotFollower::changeModeSrvCb, this);

    config_srv_ = new dynamic_reconfigure::Server<turtlebot_follower::FollowerConfig>(private_nh);
    dynamic_reconfigure::Server<turtlebot_follower::FollowerConfig>::CallbackType f =
        boost::bind(&TurtlebotFollower::reconfigure, this, _1, _2);
    config_srv_->setCallback(f);
  }

  void reconfigure(turtlebot_follower::FollowerConfig &config, uint32_t level)
  {
    min_y_ = config.min_y;
    max_y_ = config.max_y;
    min_x_ = config.min_x;
    max_x_ = config.max_x;
    max_z_ = config.max_z;
    goal_z_ = config.goal_z;
    z_scale_ = config.z_scale;
    x_scale_ = config.x_scale;
  }
  
  void bumperEventCB(const kobuki_msgs::BumperEventConstPtr msg){
  	if (msg->state == kobuki_msgs::BumperEvent::PRESSED){
  		switch (msg->bumper){
  			case kobuki_msgs::BumperEvent::LEFT:
          		if (!bumper_left_pressed_){
            		bumper_left_pressed_ = true;
            		ROS_INFO("bumper_left_pressed_,bumperEventCB") ;
            		change_direction_ = true;
          		}
          		break;
        	case kobuki_msgs::BumperEvent::CENTER:
          		if (!bumper_center_pressed_){
            		bumper_center_pressed_ = true;
            		ROS_INFO("bumper_center_pressed_,bumperEventCB") ;
            		change_direction_ = true;
          		}
          		break;
        	case kobuki_msgs::BumperEvent::RIGHT:
          		if (!bumper_right_pressed_){
            		bumper_right_pressed_ = true;
            		change_direction_ = true;
            		ROS_INFO("bumper_right_pressed_") ;
            		
          		}
          		break;
      	}
    }else{ // kobuki_msgs::BumperEvent::RELEASED
    	switch (msg->bumper){
        	case kobuki_msgs::BumperEvent::LEFT:    
        		bumper_left_pressed_ = false;
        		break;
        	case kobuki_msgs::BumperEvent::CENTER: 
        		bumper_center_pressed_ = false;
        		break;
        	case kobuki_msgs::BumperEvent::RIGHT:
        		bumper_right_pressed_ = false;
        		break;
      }
  }
}

  /*!
   * @brief Callback for point clouds.
   * Callback for point clouds. Uses PCL to find the centroid
   * of the points in a box in the center of the point cloud.
   * Publishes cmd_vel messages with the goal from the cloud.
   * @param cloud The point cloud message.
   */
  void cloudcb(const PointCloud::ConstPtr&  cloud)
  {
    //X,Y,Z of the centroid
    float x = 0.0;
    float y = 0.0;
    float z = 1e6;
    //Number of points observed
    unsigned int n = 0;
    bool direction = 0 ;
    double rand_angular;
    int count = 0;
    ros::Rate loop_rate(10) ;
    //Iterate through all the points in the region and find the average of the position
    BOOST_FOREACH (const pcl::PointXYZ& pt, cloud->points)
    {
      //First, ensure that the point's position is valid. This must be done in a seperate
      //if because we do not want to perform comparison on a nan value.
      if (!std::isnan(x) && !std::isnan(y) && !std::isnan(z))
      {
        //Test to ensure the point is within the aceptable box.
        if (-pt.y > min_y_ && -pt.y < max_y_ && pt.x < max_x_ && pt.x > min_x_ && pt.z < max_z_)
        {
          //Add the point to the totals
          x += pt.x;
          y += pt.y;
          z = std::min(z, pt.z);
          n++;
        }
      }
    }

    //If there are points, find the centroid and calculate the command goal.
    //If there are no points, simply publish a stop goal.
    if (n>4000)
    {
    	x /= n;
    	y /= n;
		if (z-goal_z_ > 0){
			
			ROS_INFO("near goal %f %f %f with %d points", x, y, z, n);
			geometry_msgs::TwistPtr cmd(new geometry_msgs::Twist());
				//ROS_INFO("Don't change direction!'") ;
			if(bumper_left_pressed_){
       	 		ROS_INFO("Bumper_left_ is pressing!");
		   	 	while(ros::ok()&&change_direction_){
		   	 		count ++;
		   	 		cmd->angular.z = -0.4 ;
		   	 		cmd->linear.x = -0.2 ;
		   	 		cmdpub_.publish(cmd);
		   	 		loop_rate.sleep();
		   	 		if (count > 15){
		   	 			change_direction_ = false ;
		   	 			count = 0 ;
		   	 		}
		   	 	}
        	
		    }else if(bumper_center_pressed_){
		    	while(ros::ok()&&change_direction_){
		   	 		count ++;
		   	 		//ROS_INFO("I am changing!") ;
		   	 		cmd->angular.z = -0.5 ;
		   	 		cmd->linear.x = -0.2 ;
		   	 		cmdpub_.publish(cmd);
		   	 		loop_rate.sleep();
		   	 		if (count > 20){
		   	 			change_direction_ = false ;
		   	 			count = 0 ;
		   	 		}
		   	 	}
        	}else if(bumper_right_pressed_){
		    	while(ros::ok()&&change_direction_){
		   	 		count ++;
		   	 		ROS_INFO("I am changing!") ;
		   	 		cmd->angular.z = 0.4 ;
		   	 		cmd->linear.x = -0.2 ;
		   	 		cmdpub_.publish(cmd);
		   	 		loop_rate.sleep();
		   	 		if (count > 15){
		   	 			change_direction_ = false ;
		   	 			count = 0 ;
		   	 		}
		   	 	}
		    }else{
		    	ROS_INFO("**************");
				cmd->linear.x = 0.2 ;
				if(x > 0.2 ){
					direction = 1 ;
                    srand((unsigned)time(NULL)) ;
					rand_angular = rand()%7 ;
					rand_angular = rand_angular/7.0 ;
					ROS_INFO(" x > 0.2  , rand_angular %f ", rand_angular);
					if(rand_angular > 0.7 && rand_angular <= 1.0 ){
						cmd->angular.z = 0.4 ;
                        ROS_INFO("rand_angular = 0.7~1.0, cmd->angular.z = %f ", cmd->angular.z );
					}else if(rand_angular > 0.4 && rand_angular  <= 0.7){
						cmd->angular.z = rand_angular ;
                        ROS_INFO("rand_angular = 0.4~0.7, cmd->angular.z = %f ", cmd->angular.z );
					}else{
						cmd->angular.z = 0.3 ;
                        ROS_INFO("rand_angular = 0.0~0.4, cmd->angular.z = %f ", cmd->angular.z );
					}
				}else if(x <- 0.2){
					direction = 0 ;
                    srand((unsigned)time(NULL)) ;
					rand_angular = rand()%7 ;
					rand_angular = double(rand_angular)/7.0 - 1.0 ;
					ROS_INFO(" x < -0.2  , rand_angular %f ", rand_angular);
					if(rand_angular >= -1.0 && rand_angular < -0.7 ){
						cmd->angular.z = -0.36 ;
                        ROS_INFO("rand_angular = -1.0~-0.7, cmd->angular.z = %f ", cmd->angular.z );
					}else if(rand_angular >= -0.7 && rand_angular  < -0.5){
						cmd->angular.z = rand_angular ;
                        ROS_INFO("rand_angular = -0.7~-0.4, cmd->angular.z = %f ", cmd->angular.z );
					}else {
						 cmd->angular.z = - 0.2 ;
                        ROS_INFO("rand_angular = -0.4~-0.0, cmd->angular.z = %f ", cmd->angular.z );
					}
						
				}else{
					if(direction){
						cmd->angular.z = 0.3 ;
					}else{
						if(!change_direction_)
							cmd->angular.z = -0.3 ;
					}
				}
				cmdpub_.publish(cmd) ;
			  } 
		}else{
			ROS_INFO("goal is bingo %f %f %f with %d points", x, y, z, n);
			geometry_msgs::TwistPtr cmd(new geometry_msgs::Twist());
			if(bumper_left_pressed_){
       	 		ROS_INFO("Bumper_left_ is pressing!");
		   	 	while(ros::ok()&&change_direction_){
		   	 		count ++;
		   	 		cmd->angular.z = -0.4 ;
		   	 		cmd->linear.x = -0.2 ;
		   	 		cmdpub_.publish(cmd);
		   	 		loop_rate.sleep();
		   	 		if (count > 15){
		   	 			change_direction_ = false ;
		   	 			count = 0 ;
		   	 		}
		   	 	}
        	
		    }else if(bumper_center_pressed_){
		    	while(ros::ok()&&change_direction_){
		   	 		count ++;
		   	 		cmd->angular.z = -0.4 ;
		   	 		cmd->linear.x = -0.2 ;
		   	 		cmdpub_.publish(cmd);
		   	 		loop_rate.sleep();
		   	 		if (count > 20){
		   	 			change_direction_ = false ;
		   	 			count = 0 ;
		   	 		}
		   	 	}
        	}else if(bumper_right_pressed_){
		    	while(ros::ok()&&change_direction_){
		   	 		count ++;
		   	 		ROS_INFO("I am changing!") ;
		   	 		cmd->angular.z = 0.4 ;
		   	 		cmd->linear.x = -0.2 ;
		   	 		cmdpub_.publish(cmd);
		   	 		loop_rate.sleep();
		   	 		if (count > 15){
		   	 			change_direction_ = false ;
		   	 			count = 0 ;
		   	 		}
		   	 	}
		    }else{
				cmd->linear.x = 0 ;
				int num = 0;
		        double ang = 0.0 ;
				if(x > 0.2){
		            srand((unsigned)time(NULL));
		            num = (rand() % MAX);
		            ang = double(num) / MAX ;
		            ROS_INFO(" x > 0.2  , ang %f ", ang);
		            direction = 1 ;
					if(ang > 0.7 && ang <= 1 ){
						cmd->angular.z = 0.4 ;
		                ROS_INFO("ang=0.7~1.0, ancmd->angular.z = %f ", cmd->angular.z );
					}else if(ang > 0.4 && ang  <= 0.7){
						cmd->angular.z = ang ;
		                ROS_INFO("ang=0.4~0.7, cmd->angular.z = %f ", cmd->angular.z );
					}else{
						cmd->angular.z = 0.3 ;
		                ROS_INFO("ang=0.0~0.4,cmd->angular.z = %f ", cmd->angular.z );
					}
				}else if(x < -0.2){
		            srand((unsigned)time(NULL));
		            num = (rand() % MAX);
		            ang = double(num) / MAX ;
		            ang = ang - 1.0 ;
		            direction = 0 ;
		            ROS_INFO(" x < -0.2  , ang %f ", ang);
		            if(ang >= -1.0 && ang < -0.7 ){
		                cmd->angular.z = -0.2 ;
		                ROS_INFO("ang=-1.0~-0.7, ancmd->angular.z = %f ", cmd->angular.z );
		            }else if(ang >= -0.7 && ang  < -0.4){
		                cmd->angular.z = ang ;
		                ROS_INFO("ang=-0.7~-0.4, ancmd->angular.z = %f ", cmd->angular.z );
		            }else {
		                cmd->angular.z = - 0.2 ;
		                ROS_INFO("ang=-0.4~-0.0, ancmd->angular.z = %f ", cmd->angular.z );
		            }
				}else{
					ROS_INFO(" direction is  %d ", direction);
					if(direction){
						cmd->angular.z = 0.3 ;
					}else{
						cmd->angular.z = -0.3 ;
					}
				}
				cmdpub_.publish(cmd) ;
			}
		}
    }else{
      ROS_DEBUG("No points detected, stopping the robot");
      publishMarker(x, y, z);
      if (enabled_)
      {	
      	geometry_msgs::TwistPtr cmd(new geometry_msgs::Twist());
        ROS_INFO("There are no points!x=%f , y=%f , z=%f,points=%d",x,y,z,n);
        if(bumper_left_pressed_){
       	 ROS_INFO("Bumper_left_ is pressing!");
       	 	while(ros::ok()&&change_direction_){
       	 		count ++;
       	 		//ROS_INFO("I am changing!") ;
       	 		cmd->angular.z = -0.4 ;
       	 		cmd->linear.x = -0.2 ;
       	 		cmdpub_.publish(cmd);
       	 		loop_rate.sleep();
       	 		if (count > 15){
       	 			change_direction_ = false ;
       	 			count = 0 ;
       	 		}
       	 			
       	 		
       	 	}
        	
        }else if(bumper_center_pressed_){
        	while(ros::ok()&&change_direction_){
       	 		count ++;
       	 		ROS_INFO("I am changing!") ;
       	 		cmd->angular.z = -0.5 ;
       	 		cmd->linear.x = -0.2 ;
       	 		cmdpub_.publish(cmd);
       	 		loop_rate.sleep();
       	 		if (count > 20){
       	 			change_direction_ = false ;
       	 			count = 0 ;
       	 		}
       	 	}
        }else if(bumper_right_pressed_){
        	while(ros::ok()&&change_direction_){
       	 		count ++;
       	 		ROS_INFO("I am changing!") ;
       	 		cmd->angular.z = 0.4 ;
       	 		cmd->linear.x = -0.2 ;
       	 		cmdpub_.publish(cmd);
       	 		loop_rate.sleep();
       	 		if (count > 15){
       	 			change_direction_ = false ;
       	 			count = 0 ;
       	 		}
       	 	}
        }else{
        	 cmd->linear.x = 0.2 ;
        	 cmdpub_.publish(cmd);
        }
      }
    }

    publishBbox();
  }

  bool changeModeSrvCb(turtlebot_msgs::SetFollowState::Request& request,
                       turtlebot_msgs::SetFollowState::Response& response)
  {
    if ((enabled_ == true) && (request.state == request.STOPPED))
    {
      ROS_INFO("Change mode service request: following stopped");
      cmdpub_.publish(geometry_msgs::TwistPtr(new geometry_msgs::Twist()));
      enabled_ = false;
    }
    else if ((enabled_ == false) && (request.state == request.FOLLOW))
    {
      ROS_INFO("Change mode service request: following (re)started");
      enabled_ = true;
    }

    response.result = response.OK;
    return true;
  }

  void publishMarker(double x,double y,double z)
  {
    visualization_msgs::Marker marker;
    marker.header.frame_id = "/camera_rgb_optical_frame";
    marker.header.stamp = ros::Time();
    marker.ns = "my_namespace";
    marker.id = 0;
    marker.type = visualization_msgs::Marker::SPHERE;
    marker.action = visualization_msgs::Marker::ADD;
    marker.pose.position.x = x;
    marker.pose.position.y = y;
    marker.pose.position.z = z;
    marker.pose.orientation.x = 0.0;
    marker.pose.orientation.y = 0.0;
    marker.pose.orientation.z = 0.0;
    marker.pose.orientation.w = 1.0;
    marker.scale.x = 0.2;
    marker.scale.y = 0.2;
    marker.scale.z = 0.2;
    marker.color.a = 1.0;
    marker.color.r = 1.0;
    marker.color.g = 0.0;
    marker.color.b = 0.0;
    //only if using a MESH_RESOURCE marker type:
    markerpub_.publish( marker );
  }

  void publishBbox()
  {
    double x = (min_x_ + max_x_)/2;
    double y = (min_y_ + max_y_)/2;
    double z = (0 + max_z_)/2;

    double scale_x = (max_x_ - x)*2;
    double scale_y = (max_y_ - y)*2;
    double scale_z = (max_z_ - z)*2;

    visualization_msgs::Marker marker;
    marker.header.frame_id = "/camera_rgb_optical_frame";
    marker.header.stamp = ros::Time();
    marker.ns = "my_namespace";
    marker.id = 1;
    marker.type = visualization_msgs::Marker::CUBE;
    marker.action = visualization_msgs::Marker::ADD;
    marker.pose.position.x = x;
    marker.pose.position.y = -y;
    marker.pose.position.z = z;
    marker.pose.orientation.x = 0.0;
    marker.pose.orientation.y = 0.0;
    marker.pose.orientation.z = 0.0;
    marker.pose.orientation.w = 1.0;
    marker.scale.x = scale_x;
    marker.scale.y = scale_y;
    marker.scale.z = scale_z;
    marker.color.a = 0.5;
    marker.color.r = 0.0;
    marker.color.g = 1.0;
    marker.color.b = 0.0;
    //only if using a MESH_RESOURCE marker type:
    bboxpub_.publish( marker );
  }

  ros::Subscriber sub_;
  ros::Subscriber bumper_event_sub_ ;
  ros::Publisher cmdpub_;
  ros::Publisher markerpub_;
  ros::Publisher bboxpub_;
};

PLUGINLIB_DECLARE_CLASS(turtlebot_follower, TurtlebotFollower, turtlebot_follower::TurtlebotFollower, nodelet::Nodelet);

}
