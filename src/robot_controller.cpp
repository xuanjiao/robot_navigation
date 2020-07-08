#include "ros/ros.h"
#include "robot_navigation/sensor_data.h"
#include "robot_navigation/make_task.h"
#include "robot_navigation/RequestTask.h"
#include "robot_navigation/GoToTargetAction.h"
#include <cmath>
#include <sstream>
#include <move_base_msgs/MoveBaseAction.h>
#include <actionlib/client/simple_action_client.h>

#include <geometry_msgs/PoseWithCovarianceStamped.h>
#include <geometry_msgs/PoseStamped.h>
#include <nav_msgs/Path.h>
#include <string> 
#include "util.h"

#define SENSOR_RANGE 1

enum MODE{
    SLEEP = 0,
    REQUEST = 1,
    RUN = 2
};

typedef actionlib::SimpleActionClient<move_base_msgs::MoveBaseAction> MoveBaseClient;
typedef actionlib::SimpleActionClient<move_base_msgs::MoveBaseAction> MoveBaseClient;

class RobotController{

public:
    RobotController():
    _mbc("move_base", true)
    
    {
        mode = 1;
        battery_level = 100;
	    ros::Duration(1).sleep();

        // subscribe to door sensor node
        _ss = 
            _nh.subscribe<robot_navigation::sensor_data>("sensor_data",100,&RobotController::sensor_callback,this);      
        // _tc = nh.serviceClient<robot_navigation::make_task>("make_task");
        _tc = _nh.serviceClient<robot_navigation::RequestTask>("RequestTask");


        _mbc.waitForServer();
        request_current_pose();
        
        // request a best task from centralized pool and do this task
        next_mode(MODE::REQUEST,true);

        ros::spin();
    }


    void request_current_pose(){
        // try to get its current location
        boost::shared_ptr<geometry_msgs::PoseWithCovarianceStamped const> sharedPtr =
             ros::topic::waitForMessage<geometry_msgs::PoseWithCovarianceStamped>("amcl_pose",_nh);
        if(sharedPtr == NULL){
           ROS_DEBUG("Failed to get current position");
           return;
        }     
        current_pos = sharedPtr->pose.pose;
        ROS_INFO_STREAM("Robot get current position "<<Util::pose_str(current_pos));       
    }

    void next_mode(MODE mode,bool result, int param = 0){
        this->mode = mode;
        switch(mode){
            case MODE::SLEEP:
               go_to_sleep(param);
               break;
            case MODE::RUN:
                run_task();
                break;
            case MODE::REQUEST:
                talk_to_centralized_pool(result);
                break;
        }
    }

    void go_to_sleep(int sec = 0){
        ros::Time wake_up = current_task.goal.header.stamp - ros::Duration(0.1);
        if( wake_up < ros::Time::now()){
            ROS_INFO_STREAM("Task is expired");
            next_mode(MODE::REQUEST,false);
            return;
        }
        ROS_INFO_STREAM(
                        "\nSimulation time: " <<ros::Time::now().sec <<
                        "\nSleep until "<< Util::time_str(wake_up) <<
                        "\nSimulation time: " <<wake_up.sec
        );
        ros::Time::sleepUntil(wake_up - ros::Duration(0.1));                
        ROS_INFO_STREAM("** Wake up.Simulation time: " <<ros::Time::now().sec );
        next_mode(MODE::RUN,true);
    }


    void talk_to_centralized_pool(bool is_complete){
        // request a best task
        robot_navigation::make_task srv;
        srv.request.battery_level = battery_level;
        srv.request.pose = current_pos;
        srv.request.last_task = current_task;
        srv.request.last_task.m_time = ros::Time::now();
        srv.request.last_task.is_completed = is_complete;

        srv.request.last_task.door_status = current_task.door_status;
        ROS_INFO_STREAM("send task request. Robot position: "<<Util::pose_str(srv.request.pose));

        if(!_tc.call(srv)){
            ROS_INFO_STREAM("Failed to send request");
            return;
        }
        current_task = srv.response.best_task;
        ROS_INFO_STREAM("receive response\n"<<current_task);
        
        // next_mode(MODE::SLEEP,true);
    }

    // request a task from centralized pool
    bool request_task(){
        robot_navigation::RequestTask srv;
        srv.request.battery_level =  battery_level;
        srv.request.last_task_id = current_task.task_id;
        srv.request.pose = current_pos;

        if(!_tc.call(srv)){
            ROS_INFO_STREAM("Failed to send request");
            return;
        }
        ROS_INFO_STREAM("receive response: "<<srv.response.has_task?"has task":"no task");

        next_mode(MODE::SLEEP,true);
    }

    void report_task_result(){

    }
    void run_task(){
        move_base_msgs::MoveBaseGoal goal;
        goal.target_pose = current_task.goal;
        _mbc.sendGoal(goal,
                boost::bind(&RobotController::move_complete_callback,this, _1, _2),
                MoveBaseClient::SimpleActiveCallback(),
                boost::bind(&RobotController::move_position_feedback,this, _1));        
    }

    void move_position_feedback(const move_base_msgs::MoveBaseFeedbackConstPtr &feedback){
            double distance = sqrt(pow((feedback->base_position.pose.position.x - current_pos.position.x),2) + 
                                    pow((feedback->base_position.pose.position.y - current_pos.position.y),2));
            current_pos = feedback->base_position.pose;

            double angle = 2 * acos(feedback->base_position.pose.orientation.w);
            battery_level=  battery_level - 0.01 * distance - 0.001 * angle;
            ROS_INFO_STREAM("angle "<<angle << "distance" << distance << "battery_level"<<battery_level);
     }
     
    void move_complete_callback(const actionlib::SimpleClientGoalState& state,
           const move_base_msgs::MoveBaseResult::ConstPtr& result ){
            ROS_INFO_STREAM(state.toString());
            next_mode(MODE::REQUEST,(state == actionlib::SimpleClientGoalState::SUCCEEDED )?true:false); //if robot reach the door task complete
    }

    void sensor_callback(const robot_navigation::sensor_data::ConstPtr& message){

        double distance = sqrt(pow(current_pos.position.x - message->pose.x,2) + pow(current_pos.position.y - message->pose.y,2));

        if(distance <= SENSOR_RANGE){
                std::string status = message->door_status?"open":"closed";
            ROS_INFO_STREAM( "Distance "<< distance<<" room " << message->id <<" door "<<status<<" position ("<<message->pose.x<<", "<<message->pose.y<<", "<<message->pose.z<<")");
        
            if(message->id == current_task.room_id)
                current_task.door_status = message->door_status;
            }
    }


private:
    
    // Note handle
    ros::NodeHandle _nh;
    
    // Publisher
    ros::Publisher _mbp;

    // Subscriber 
    ros::Subscriber _ps;
    ros::Subscriber _ss;

    // client
    ros::ServiceClient _tc;
    MoveBaseClient _mbc;

    geometry_msgs::Pose current_pos;
    robot_navigation::task_info current_task;
    double battery_level;

    int mode;
};

int main(int argc, char **argv){
	ROS_INFO("Main function start");
        ros::init(argc,argv,"move_base_simple"); // Initializes Node Name
        
    ROS_INFO("RobotController run");
        RobotController demo;

        ros::spin();      

        return 0;
}
