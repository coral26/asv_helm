// Roland Arsenault
// Center for Coastal and Ocean Mapping
// University of New Hampshire
// Copyright 2017, All rights reserved.

#include "ros/ros.h"
#include "std_msgs/Bool.h"
#include "asv_msgs/HeadingHold.h"
#include "asv_msgs/HeadingStamped.h"
#include "asv_msgs/BasicPositionStamped.h"
#include "asv_srvs/VehicleState.h"
#include "asv_srvs/PilotControl.h"
#include "geometry_msgs/TwistStamped.h"
#include "geographic_msgs/GeoPointStamped.h"
#include "mission_plan/NavEulerStamped.h"

ros::Publisher asv_helm_pub;
ros::Publisher asv_inhibit_pub;
ros::Publisher position_pub;
ros::Publisher heading_pub;
ros::Publisher speed_pub;

double heading;
double rudder;
double throttle;
ros::Time last_time;

double last_boat_heading;

double desired_speed;
ros::Time desired_speed_time;
double desired_heading;
ros::Time desired_heading_time;

void twistCallback(const geometry_msgs::TwistStamped::ConstPtr& msg)
{
    throttle = msg->twist.linear.x;
    rudder = -msg->twist.angular.z;
    
    last_time = msg->header.stamp;
}

void positionCallback(const asv_msgs::BasicPositionStamped::ConstPtr& inmsg)
{
    geographic_msgs::GeoPointStamped gps;
    gps.header = inmsg->header;
    gps.position.latitude = inmsg->basic_position.position.latitude;
    gps.position.longitude = inmsg->basic_position.position.longitude;
    position_pub.publish(gps);
    
    geometry_msgs::TwistStamped ts;
    ts.header = inmsg->header;
    ts.twist.linear.x = inmsg->basic_position.sog;
    speed_pub.publish(ts);
}

void headingCallback(const asv_msgs::HeadingStamped::ConstPtr& msg)
{
    last_boat_heading = msg->heading.heading;
    mission_plan::NavEulerStamped nes;
    nes.header = msg->header;
    nes.orientation.heading = msg->heading.heading*180.0/M_PI;
    heading_pub.publish(nes);
}

void desiredSpeedCallback(const geometry_msgs::TwistStamped::ConstPtr& inmsg)
{
    desired_speed = inmsg->twist.linear.x;
    desired_speed_time = inmsg->header.stamp;
}

void desiredHeadingCallback(const mission_plan::NavEulerStamped::ConstPtr& inmsg)
{
    desired_heading = inmsg->orientation.heading;
    desired_heading_time = inmsg->header.stamp;
}

void sendHeadingHold(const ros::TimerEvent event)
{
    asv_msgs::HeadingHold asvMsg;
    bool doDesired = true;
    if (!last_time.isZero())
    {
        if(event.last_real-last_time>ros::Duration(.5))
        {
            throttle = 0.0;
            rudder = 0.0;
        }
        else
            doDesired = false;
    }

    ros::Duration delta_t = event.current_real-event.last_real;
    heading = last_boat_heading + rudder; //*delta_t.toSec();
    heading = fmod(heading,M_PI*2.0);
    if(heading < 0.0)
        heading += M_PI*2.0;
    
    asvMsg.heading.heading = heading;
    if(doDesired)
    {
        asvMsg.thrust.type = asv_msgs::Thrust::THRUST_SPEED;
    }
    else
    {
        asvMsg.thrust.type = asv_msgs::Thrust::THRUST_THROTTLE;
    }
    //asvMsg.thrust.type = asv_msgs::Thrust::THRUST_SPEED;
    asvMsg.thrust.value = throttle*100.0;
    asvMsg.header.stamp = event.current_real;
    if(doDesired)
    {
        if (event.current_real - desired_heading_time < ros::Duration(.5) && event.current_real - desired_speed_time < ros::Duration(.5))
        {
            asvMsg.header.stamp = desired_heading_time;
            asvMsg.heading.heading = desired_heading*M_PI/180.0;
            asvMsg.thrust.value = desired_speed;
        }
    }
    asv_helm_pub.publish(asvMsg);
}

void activeCallback(const std_msgs::Bool::ConstPtr& inmsg)
{
    if(inmsg->data)
    {
        asv_srvs::VehicleState vs;
        vs.request.desired_state = asv_srvs::VehicleStateRequest::VP_STATE_ACTIVE;
        ros::service::call("/control/vehicle/state",vs);
        
        asv_srvs::PilotControl pc;
        pc.request.control_request = true;
        ros::service::call("/control/vehicle/pilot",pc);
        
        std_msgs::Bool inhibit;
        inhibit.data = false;
        asv_inhibit_pub.publish(inhibit);
    }
    else
    {
        asv_srvs::PilotControl pc;
        pc.request.control_request = false;
        ros::service::call("/control/vehicle/pilot",pc);
    }
}


int main(int argc, char **argv)
{
    heading = 0.0;
    throttle = 0.0;
    rudder = 0.0;
    last_boat_heading = 0.0;
    
    ros::init(argc, argv, "asv_helm");
    ros::NodeHandle n;
    
    asv_helm_pub = n.advertise<asv_msgs::HeadingHold>("/control/drive/heading_hold",1);
    asv_inhibit_pub = n.advertise<std_msgs::Bool>("/control/drive/inhibit",1,true);
    heading_pub = n.advertise<mission_plan::NavEulerStamped>("/heading",1);
    position_pub = n.advertise<geographic_msgs::GeoPointStamped>("/position",1);
    speed_pub = n.advertise<geometry_msgs::TwistStamped>("/sog",1);

    ros::Subscriber asv_helm_sub = n.subscribe("/cmd_vel",5,twistCallback);
    ros::Subscriber asv_position_sub = n.subscribe("/sensor/vehicle/position",10,positionCallback);
    ros::Subscriber asv_heading_sub = n.subscribe("/sensor/vehicle/heading",5,headingCallback);
    ros::Subscriber activesub = n.subscribe("/active",10,activeCallback);
    ros::Subscriber dspeed_sub = n.subscribe("/moos/desired_speed",10,desiredSpeedCallback);
    ros::Subscriber dheading_sub = n.subscribe("/moos/desired_heading",10,desiredHeadingCallback);
    
    ros::Timer timer = n.createTimer(ros::Duration(0.1),sendHeadingHold);
    
    ros::spin();
    
    return 0;
}
