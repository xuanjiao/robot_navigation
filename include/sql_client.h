#pragma once

#include <stdlib.h>
#include <ros/ros.h>
#include <vector>
#include <iostream>
#include "mysql_connection.h"
#include <cppconn/driver.h>
#include <cppconn/exception.h>
#include <cppconn/resultset.h>
#include <cppconn/statement.h>
#include "general_task.h"
#include "util.h"

#define   DATABASE_NAME                                   "sensor_db"
#define   URI                                             "tcp://127.0.0.1"

using namespace std;

class SQLClient{
  public:
    SQLClient(string user_name, string pass):user_name(user_name),pass(pass){
      ConnectToDatabase();
    }
      
    void ConnectToDatabase(){
      driver = get_driver_instance();
      con = driver->connect(URI,user_name,pass);
      if(con->isValid()){
        ROS_INFO_STREAM("Connected to "<< DATABASE_NAME);
        con->setSchema(DATABASE_NAME);
      }else{
        ROS_INFO_STREAM("Connected to "<< DATABASE_NAME<<" failed");
      }
      stmt = con->createStatement();
    }

    // truncate table
    void TruncateTable(string name){
      stmt->execute("TRUNCATE "+name);
    }

    // Print table include columns and data
    void PrintTable(string table_name){
      
      sql::ResultSet *res;
      stringstream ss;
      list<sql::SQLString> column_names;
      try{         
          ss << "\nPrint table " << table_name << ":\n-----------------------------------------------------------------------\n";
            res = stmt->executeQuery("SHOW COLUMNS FROM " + table_name);
            while (res->next()){
              column_names.push_back(res->getString("Field"));   
              ss << res->getString("Field")<< " ";
            }
            ss << "\n---------------------------------------------------------------------\n";
            res = stmt->executeQuery("SELECT * FROM " + table_name);
            if(res->rowsCount()!=0){
              while(res->next()){
                for(sql::SQLString c : column_names){
                    ss << res->getString(c)<<"  ";
                }
                ss<< "\n";
              }
            }
      }catch(const sql::SQLException &e){
        ROS_INFO_STREAM(e.what());
        exit(1);
      }    
      delete res;
      ROS_INFO_STREAM(ss.str());
    }

    vector<tuple<int,geometry_msgs::Pose,long double>>
    QueryTargetPositionAndOpenPossibilities(string time){
      sql::ResultSet* res;
      vector<tuple<int,geometry_msgs::Pose,long double>> v;
      try{
        res = stmt->executeQuery("select t.target_id,t.position_x, t.position_y, t.orientation_w, t.orientation_z, o.open_pos \
                                      from targets t \
                                      inner join open_possibilities o \
                    where t.target_id = o.door_id  and o.day_of_week = dayofweek('" + time +  "') and time('" + time +  "') between o.start_time and o.end_time; ");
      }catch(sql::SQLException e){
        ROS_INFO_STREAM( e.what() );
      }
      if(res->rowsCount()){
        while(res->next()){
          geometry_msgs::Pose pose;
          pose.position.x = res->getDouble("position_x");
          pose.position.y = res->getDouble("position_y");
          pose.orientation.z = res->getDouble("orientation_z");
          pose.orientation.w = res->getDouble("orientation_w");
          v.push_back(
            tuple<int,geometry_msgs::Pose,long double>(
              res->getInt("target_id"), pose, res->getDouble("open_pos")
            )
          );
        }
      }

      delete res;
      return v;
    }
    
        // get task info to calculate cost
    vector<TaskInTable>
    QueryRunableExecuteTasks(){
      sql::ResultSet* res;
      vector<TaskInTable> v;
      res = stmt->executeQuery(
       "SELECT tasks.dependency, tasks.priority, tasks.target_id, tasks.task_id, tasks.task_type, tasks.start_time, \
        tg.position_x, tg.position_y, tg.orientation_z, tg.orientation_w FROM targets tg \
        INNER JOIN tasks ON tasks.target_id = tg.target_id \
        AND tasks.cur_status IN ('Created','ToReRun') \
        AND tasks.task_type = 'ExecuteTask'"
      );
      if(res->rowsCount()!=0){
        while(res->next()){
          TaskInTable t;
          t.priority = res->getInt("priority");
          t.targetId = res->getInt("target_id");
          t.taskId = res->getInt("task_id");
          t.taskType = res->getString("task_type");
          t.dependency = res->getInt("dependency");

          t.goal.header.frame_id = "map";
          t.goal.header.stamp = Util::str_ros_time(res->getString("start_time"));
          t.goal.pose.position.x = res->getDouble("position_x");
          t.goal.pose.position.y = res->getDouble("position_y");
          t.goal.pose.orientation.z = res->getDouble("orientation_z");
          t.goal.pose.orientation.w = res->getDouble("orientation_w");
          v.push_back(t);
        } 
      }

      delete res;
      return v;
    }

    // get task info to calculate cost
    vector<TaskInTable>
    QueryRunableGatherEnviromentInfoTasks(){
      sql::ResultSet* res;
      vector<TaskInTable> v;
      res = stmt->executeQuery(
       "SELECT tasks.dependency, tasks.priority, o.open_pos_st, tasks.target_id, tasks.task_id, tasks.task_type, tasks.start_time, \
        tg.position_x, tg.position_y, tg.orientation_z, tg.orientation_w FROM targets tg \
        INNER JOIN tasks ON tasks.target_id = tg.target_id \
        INNER JOIN open_possibilities o WHERE tg.target_id = o.door_id \
        AND DAYOFWEEK(tasks.start_time) = o.day_of_week \
        AND TIME(tasks.start_time) BETWEEN o.start_time AND o.end_time \
        AND tasks.cur_status IN ('Created' ,'ToReRun') \
        AND tasks.task_type = 'GatherEnviromentInfo'"
        
      );
      if(res->rowsCount()!=0){
        while(res->next()){
          TaskInTable t;
          t.priority = res->getInt("priority");
          t.openPossibility = res->getDouble("open_pos_st");
          t.targetId = res->getInt("target_id");
          t.taskId = res->getInt("task_id");
          t.taskType = res->getString("task_type");
          t.dependency = res->getInt("dependency");

          t.goal.header.frame_id = "map";
          t.goal.header.stamp = Util::str_ros_time(res->getString("start_time"));
          t.goal.pose.position.x = res->getDouble("position_x");
          t.goal.pose.position.y = res->getDouble("position_y");
          t.goal.pose.orientation.z = res->getDouble("orientation_z");
          t.goal.pose.orientation.w = res->getDouble("orientation_w");
          v.push_back(t);
        } 
      }

      delete res;
      return v;
    }


    // Create new enter room tasks
    int InsertMultipleGatherInfoTasks(int num, ros::Time start, ros::Duration interval){
      sql::ResultSet* res;
      vector<int> doors  = QueryDoorId();
      int id,cnt;
      for(int i = 0; i < num; i++){
        id = doors[rand()%doors.size() + 1];  
        stmt->execute(
            "INSERT INTO tasks(task_type, start_time, target_id, priority) VALUES('GatherEnviromentInfo','" + Util::time_str(start + interval *i) + "','" + to_string(id) + "'," + to_string(1) +")"
        );
        cnt += stmt->getUpdateCount();
      }
      delete res;
      return cnt;
    }

        // Create new enter room tasks
    vector<int> QueryDoorId(){
      sql::ResultSet* res;
      vector<int> doors;
      res = stmt->executeQuery("SELECT target_id  FROM targets WHERE target_type = 'Door'");
      while(res->next()){
        doors.push_back(res->getInt("target_id")); // find available door id
      }
      delete res;
      return doors;
    }


    
    // Create new task and return its task id
    int InsertATaskAssignId(TaskInTable& t){
        sql::ResultSet* res;        
        stmt->execute(
          "INSERT INTO tasks(dependency,task_type, priority, target_id, start_time) VALUES('"
          +to_string(t.dependency) +"','" + t.taskType +"','" + to_string(t.priority) +"','" + to_string(t.targetId) + "','" + Util::time_str(t.goal.header.stamp)+"')"
         
          );
        res = stmt->executeQuery("SELECT last_insert_id() as id");
        res->next();
        t.taskId = res->getInt("id");
        delete res;
        return t.taskId;
    }

    int InsertATargetAssignId(geometry_msgs::PoseStamped target, string targetType){
        sql::ResultSet* res;        
        string x = to_string(target.pose.position.x);
        string y = to_string(target.pose.position.y);
        string z = to_string(target.pose.orientation.z);
        string w = to_string(target.pose.orientation.w);
        
        ROS_INFO_STREAM("Check target exist ");
          res = stmt->executeQuery(
            "SELECT * FROM targets WHERE position_x = " + x + " AND position_y =" + y
          );
          if(res->rowsCount() == 0){
              ROS_INFO_STREAM("Adding new target (" << x << "," <<y << ")");
              stmt->execute(
                "INSERT INTO targets(target_type, position_x, position_y, orientation_z, orientation_w) \
                  VALUES('"+ targetType + "'," + x + "," + y + "," + z +","+ w +")" 
              );
              res = stmt->executeQuery("SELECT last_insert_id() as target_id");
              res->next();
              if(res->rowsCount() == 0){
                ROS_INFO_STREAM("Failed to insert target");
                return 0;
              }else{
                return res->getInt("target_id");
              }
          }else{
            ROS_INFO_STREAM("This target is already in targets table");
            res->next();
            return res->getInt("target_id");
          }
        
    }

    // Create charging task
    // vector<pair<int,geometry_msgs::Pose>>
    map<int,geometry_msgs::Pose>
    QueryAvailableChargingStations(){
      // vector<pair<int,geometry_msgs::Pose>> v;
      map<int,geometry_msgs::Pose> map;
      sql::ResultSet* res;
      res = stmt->executeQuery("SELECT * FROM targets tg INNER JOIN charging_stations cs ON tg.target_id = cs.station_id WHERE target_type = 'ChargingStation' AND is_free = 1");
      while(res->next()){
        geometry_msgs::Pose pose;
        pose.position.x = res->getDouble("position_x");
        pose.position.y = res->getDouble("position_y");
        pose.orientation.z = res->getDouble("orientation_z");
        pose.orientation.w = res->getDouble("orientation_w");
        map.insert(make_pair(res->getInt("target_id"),pose));
      }
      delete res;
      return map;
    } 

    // Change time and Priority of a returned task
    int UpdateReturnedTask(int task_id, int priority_inc, ros::Duration time_inc){
      return stmt->executeUpdate("UPDATE tasks \
              SET priority = IF((priority + " + to_string(priority_inc) +  ")>5,5,priority + " + to_string(priority_inc) + 
        "), start_time = TIMESTAMPADD(SECOND," + to_string(time_inc.sec) + 
        ",start_time), cur_status = 'ToReRun' WHERE task_id = "+ to_string(task_id)
        );
    }

    // Insert a record in door status list
    int InsertDoorStatusRecord(int door_id, ros::Time measure_time,bool door_status){
        string mst = Util::time_str(measure_time);
        // ROS_INFO_STREAM(" insert "<<to_string(door_id)<<" "<<measure_time<<" "<<door_status);
        stmt->execute("REPLACE INTO door_status(door_id,door_status,date_time) VALUES('" + to_string(door_id) + "', " +to_string(door_status)+", '"+ mst+"')");
        return stmt->getUpdateCount();
    }

    // Find all records from this time and day of weeks, and calculate new open possibilities
    int UpdateOpenPossibilities(int door_id, ros::Time measure_time){
      auto t = QueryStartTimeEndTimeDayFromOpenPossibilitiesTable(door_id,measure_time);

      // update open possibility table
      return stmt->executeUpdate(
        "UPDATE open_possibilities o \
          SET  o.open_pos_st = (SELECT SUM(door_status) / COUNT(door_status)  FROM  door_status ds \
              WHERE ds.door_id = '" + to_string(door_id) + "' AND DAYOFWEEK(ds.date_time) = '" + to_string(get<2>(t)) + 
              "' AND TIME(ds.date_time) BETWEEN '" + get<0>(t) + "' AND '"+ get<1>(t) +
          "') WHERE o.door_id = '" + to_string(door_id) + "' AND o.day_of_week = '" + to_string(get<2>(t)) + "' AND o.start_time =' " + get<0>(t) + "' AND o.end_time = '"+ get<1>(t) +"'"       
      );
    }

    // Change task status in tasks table
    int UpdateTaskStatus(int taskId,string status){
      return stmt->executeUpdate("UPDATE tasks set cur_status = '"+ status + "' WHERE task_id = " + to_string(taskId));
    }

    int UpdateTaskRobotId(int taskId, int robotId){
      return stmt->executeUpdate("UPDATE tasks set robot_id = '"+to_string(robotId) + "' WHERE task_id = " + to_string(taskId));
    }

    // Change expired "Created", "WaitingToRun" getEnviromentInfo task status to 'Canceled'
    // Change expired and uncompleted Execute TaskInTable status to a new time
    int UpdateExpiredTask(ros::Time newTime){
        int cnt = 0;
        cnt += stmt->executeUpdate("UPDATE tasks set cur_status = 'Canceled' WHERE start_time < '" + Util::time_str(newTime)+"' AND task_type = 'GatherEnviromentInfo' AND cur_status IN ('Created','WaitingToRun','ToReRun')");
        cnt += stmt->executeUpdate("UPDATE tasks set start_time = '"+Util::time_str(newTime)+"' WHERE start_time < '" + Util::time_str(newTime)+"' AND task_type = 'ExecuteTask' AND cur_status NOT IN ('RanToCompletion')");
        return cnt;
    }

    tuple<string,string,int> QueryStartTimeEndTimeDayFromOpenPossibilitiesTable(int door_id, ros::Time measure_time){
      sql::ResultSet* res;
      string st,et,mst = Util::time_str(measure_time);
      int dw;

      // select start time, end time, day of week from open possibility table
      res = stmt->executeQuery(
        "SELECT start_time, end_time, day_of_week FROM open_possibilities WHERE door_id = '" + to_string( door_id)+
        "' AND DAYOFWEEK('" + mst + "') = day_of_week AND TIME('" + mst + "') BETWEEN start_time AND end_time"
      );

      res->next();
      st = res->getString("start_time");
      et = res->getString("end_time");
      dw = res->getInt("day_of_week");
      delete res;
      return make_tuple(st,et,dw);
    }


    ~SQLClient(){
       delete stmt;
    }
    
   private:
    string user_name;
    string pass;
    sql::Driver* driver;
    sql::Connection* con;
    sql::Statement* stmt;
};
