#pragma once
#include "ros/ros.h"
#include <geometry_msgs/PoseStamped.h>
#include "../include/task_manager.h"
#include "../include/sql_client.h"
#include "../include/task_type.h"

using namespace std;

class TaskManagerTest :public ::testing::Test {
    public:
        TaskManagerTest()
        {
            sc= new SQLClient("centralized_pool","pass");
            cc = new CostCalculator(nh);
            tm = new TaskManager(*sc,*cc);
        }
        ~TaskManagerTest(){
            delete sc;
            delete cc;
            delete tm;
        }
    SQLClient *sc;
    TaskManager * tm;
     CostCalculator* cc;
    ros::NodeHandle nh;
};

TEST_F(TaskManagerTest,example){
    ASSERT_EQ(1,1);
}


TEST_F(TaskManagerTest,createLargeTask){
    std::vector<SmallExecuteTask> tasks;
    SmallExecuteTask t1;
    t1.taskId = 1;
    t1.point.goal.pose.position.x = -24;
    t1.point.goal.pose.position.y = 12;
    t1.point.goal.pose.orientation.w = 2;
    t1.point.depDoorId = 3; 
    t1.dependency = 0;
    SmallExecuteTask t2;
    t2.taskId = 2;
    t2.point.goal.pose.position.x = 4;
    t2.dependency = 1;
    SmallExecuteTask t3;
    t3.taskId = 3;
    t3.point.goal.pose.position.x = 8;
    t3.dependency = 0;
    tasks.push_back(t1);
    tasks.push_back(t2);
    tasks.push_back(t3);
    auto lts = LargeExecuteTask::MakeLargeTasks(tasks);
    ASSERT_EQ(lts.size(),2);
    ASSERT_EQ(lts[0].smallTasks.size(),2);
    ASSERT_EQ(lts[1].smallTasks.size(),1);

    ASSERT_EQ(lts[0].smallTasks[1].point.goal.pose.position.x,t1.point.goal.pose.position.x);
    ASSERT_EQ(lts[0].smallTasks[2].point.goal.pose.position.x,t2.point.goal.pose.position.x);
    ASSERT_EQ(lts[1].smallTasks[3].point.goal.pose.position.x,t3.point.goal.pose.position.x);
}

TEST_F(TaskManagerTest,HandleTaskResult){
    TaskResult r;
    r.isCompleted = false;
    r.taskIds.push_back(1);
    r.description = "test";
    r.taskType = "ExecuteTask";
    tm->HandleTaskResult(r);
}

// TEST_F(TaskManagerTest,calculateBattery){
//     geometry_msgs::Pose p1,p2,p3;
//     geometry_msgs::PoseStamped ps2,ps3;
//     p1.position.x = 0.0; p1.position.y = 5.0; p1.orientation.w = 1.0;
//     p2.position.x = 2.0; p2.position.y = 5.0; p2.orientation.w = 1.0;
//     p3.position.x = 5.0; p3.position.y = 5.0; p3.orientation.w = 1.0;
//     ps2.pose = p2;ps3.pose = p3;
//     std::map<int,geometry_msgs::PoseStamped> map;
//     map.insert(make_pair(2,ps2));
//     map.insert(make_pair(3,ps3));
//     double d12 = tm->CalculateSimpleBatteryConsumption(p1,p2);
//     double d23 = tm->CalculateSimpleBatteryConsumption(p2,p3);
//     double d13 = tm->CalculateSimpleBatteryConsumption(p1,p3);
//     ASSERT_EQ(d13,d12 + d23);
//     double d = tm->CalculateComplexTrajectoryBatteryConsumption(p1,map);
//     ASSERT_EQ(d13,d);

// }