/*! @file mission_control.cpp
 *  @version 3.3
 *  @date Jun 05 2017
 *
 *  @brief
 *  Flight Control API usage in a Linux environment.
 *  Provides a number of helpful additions to core API calls,
 *  especially for position control, attitude control, takeoff,
 *  landing.
 *
 *  @copyright
 *  2016-17 DJI. All rights reserved.
 * */

#include "mission_control.hpp"
#include <amqp.h>

#include <boost/array.hpp>

#include <algorithm>
#include <iostream>
#include <thread>
#include <unistd.h>

using namespace DJI::OSDK;
using namespace DJI::OSDK::Telemetry;

bool monitor_switch = true;

std::string getCurrentTime()
{
  time_t rawtime;
  struct tm * timeinfo;
  char buffer[80];

  time (&rawtime);
  timeinfo = localtime(&rawtime);

  strftime(buffer,sizeof(buffer),"%Y-%m-%d %H:%M:%S",timeinfo);
  std::string str(buffer);

  return str;
}

std::string getHostname()
{
  char hostname[32];
  int result;
  result = gethostname(hostname, 32);
  if (result == 0) {
    std::string str(hostname);
    return str;
  } else {
    return "_hostname_";
  }

}

void channelSend(DJI::OSDK::Vehicle* vehicle, const std::string host, const std::string user, const std::string passwd)
{
  bool running = true;
  int syncDataIntervalInMs = 1000000;
  std::string hostname = getHostname();
  while(running) {
    try {
      std::string exchangeName = getHostname() + "-uplink";
      AmqpClient::Channel::ptr_t channel = AmqpClient::Channel::Create(host, 5672, user, passwd, "/", 4096);
      channel->DeclareExchange(exchangeName, AmqpClient::Channel::EXCHANGE_TYPE_FANOUT);
      AmqpClient::BasicMessage::ptr_t message = AmqpClient::BasicMessage::Create("== messageStart ==");
      channel->BasicPublish(exchangeName, "", message);

      // We will listen to five broadcast data sets:
      // 1. Flight Status
      // 2. Global Position
      // 3. RC Channels
      // 4. Velocity
      // 5. Quaternion

      // Please make sure your drone is in simulation mode. You can
      // fly the drone with your RC to get different values.

      Telemetry::Status         status;
      Telemetry::GlobalPosition globalPosition;
      Telemetry::RC             rc;
      Telemetry::Vector3f       velocity;
      Telemetry::Quaternion     quaternion;

      const int TIMEOUT = 20;

      // Re-set Broadcast frequencies to their default values
      ACK::ErrorCode ack = vehicle->broadcast->setBroadcastFreqDefaults(TIMEOUT);
      //Json Writer;
      Json::Value root;
      Json::FastWriter writer;
      Json::Value flightData;
      std::string json_str;
      std::string currentTime;

      while (monitor_switch) {
        // Get data of flight status
        currentTime = getCurrentTime();
        status         = vehicle->broadcast->getStatus();
        globalPosition = vehicle->broadcast->getGlobalPosition();
        rc             = vehicle->broadcast->getRC();
        velocity       = vehicle->broadcast->getVelocity();
        quaternion     = vehicle->broadcast->getQuaternion();


        flightData["flight_status"] = (unsigned)status.flight;
        flightData["position_latitude"] = getDegFromRad(globalPosition.latitude);
        flightData["position_longitude"] = getDegFromRad(globalPosition.longitude);
        flightData["position_altitude"] = globalPosition.altitude;
        flightData["position_height"] = globalPosition.height;
        flightData["gps_signal"] = globalPosition.health;
        flightData["rc_roll"] = rc.roll;
        flightData["rc_pitch"] = rc.pitch;
        flightData["rc_yaw"] = rc.yaw;
        flightData["rc_throttle"] = rc.throttle;
        flightData["velocity_vx"] = velocity.x;
        flightData["velocity_vy"] = velocity.y;
        flightData["velocity_vz"] = velocity.z;
        flightData["quaternion_w"] = quaternion.q0;
        flightData["quaternion_x"] = quaternion.q1;
        flightData["quaternion_y"] = quaternion.q2;
        flightData["quaternion_z"] = quaternion.q3;
        root["message_type"] = "monitor";
        root["basic_data"] = flightData;
        root["machine_id"] = hostname;
        root["timestamp"] = currentTime;
        json_str = "";
        json_str = writer.write(root);

        message = AmqpClient::BasicMessage::Create(json_str);
        channel->BasicPublish(exchangeName, "", message);
        std::cout << "data sent: " << getDegFromRad(globalPosition.longitude) << "," << getDegFromRad(globalPosition.latitude) << ","
                  << globalPosition.altitude << "  " << currentTime << std::endl;
        usleep(syncDataIntervalInMs);
      }
    }
    catch (const std::exception & ex) {
      std::exception_ptr p = std::current_exception();
      std::cerr << "channel send exception" << std::endl;
      std::cerr << ex.what() << std::endl;
    }
    std::cout << "restaring ... " + getCurrentTime() << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(3));
  }
}

void channelReceive(DJI::OSDK::Vehicle* vehicle, const std::string host, const std::string user, const std::string passwd)
{
  bool running = true;
  bool inprogress = false;
  //0.5 second
  int syncDataIntervalInMs = 500000;
  int responseTimeout = 10;
  while(running) {
    try {
      std::string exchangeName = getHostname() + "-downlink";
      AmqpClient::Channel::ptr_t channel = AmqpClient::Channel::Create(host, 5672, user, passwd, "/", 4096);
      channel->DeclareExchange(exchangeName, AmqpClient::Channel::EXCHANGE_TYPE_FANOUT);
      std::string queueName = channel->DeclareQueue("");
      channel->BindQueue(queueName, exchangeName);
      std::string consumer = channel->BasicConsume(queueName, "", true, false);
      AmqpClient::Envelope::ptr_t envelope;

      //Json Reader
      Json::Value root;
      Json::Reader reader;
      std::string data;
      std::string msg_type;

      while(running) {
        channel->BasicConsumeMessage(consumer, envelope, -1);
        std::cout << envelope->Message()->Body() << std::endl;
        data = envelope->Message()->Body();
        channel->BasicAck(envelope);

        if(!reader.parse(data, root, false)) {
          std::cout << "no data" << std::endl;
          continue;
        }
        msg_type = root.getMemberNames().begin()[0];
        std::cout << "CMD:" << msg_type << std::endl;
        if (msg_type == "EngineStartRequest"){
          Json::Value v = root["EngineStartRequest"];
          int time_out = v["time_out"].asInt();
          std::cout << "Engine Start Request ..." << std::endl;
          vehicle->control->armMotors(time_out);
          continue;
        }
        if (msg_type == "EngineStopRequest"){
          Json::Value v = root["EngineStopRequest"];
          int time_out = v["time_out"].asInt();
          std::cout << "Engine Stop Request ..." << std::endl;
          vehicle->control->disArmMotors(time_out);
          continue;
        }
        if (msg_type == "GohomeRequest"){
          Json::Value v = root["GohomeRequest"];
          int time_out = v["time_out"].asInt();
          std::cout << "Going Home ..." << std::endl;
          vehicle->control->goHome(time_out);
          continue;
        }
        if (msg_type == "TakeOffRequest"){
          Json::Value v = root["TakeOffRequest"];
          int time_out = v["time_out"].asInt();
          std::cout << "Monitor take off ..." << std::endl;
          monitoredTakeoff(vehicle, time_out);
          continue;
        }
        if (msg_type == "LandingRequest"){
          Json::Value v = root["LandingRequest"];
          int time_out = v["time_out"].asInt();
          monitoredLanding(vehicle, time_out);
          std::cout << "Monitor landing ..." << std::endl;
          continue;
        }
        if (msg_type == "AttitudeMoveRequest"){
          Json::Value data = root["AttitudeMoveRequest"];
          float32_t roll = data["Roll"].asFloat(); //in degree
          float32_t pitch = data["Pitch"].asFloat(); //in degree
          float32_t height = data["Height"].asFloat(); //in z axis of ground frame(m)
          float32_t yaw = data["Yaw"].asFloat(); //in z axis of ground frame (NED) (deg)
          printf("AttitudeMove:(roll, pitch, height, yaw): %f, %f, %f, %f\n",roll,pitch,height,yaw);
          vehicle->control->attitudeAndVertPosCtrl(roll, pitch, height, yaw);
          continue;
        }
        if (msg_type == "WayPointStartRequest"){
          std::cout << "Start WayPoint Mission..." << std::endl;
          Json::Value data = root["WayPointStartRequest"];
          Json::Value wp_array = data["WayPoints"];
          float32_t cruise_speed = data["CruiseSpeed"].asFloat();

          // starting height of vehicle
          float32_t start_alt = data["StartAlt"].asFloat();
          int numWaypoints = wp_array.size();

          if (vehicle->getFwVersion() != Version::M100_31)
          {
            if (!setUpSubscription(vehicle, responseTimeout))
            {
              std::cout << "Failed to set up Subscription!" << std::endl;
              continue;
            }
            sleep(1);
          }

          // Waypoint Mission : Initialization
          WayPointInitSettings fdata;
          setWaypointInitDefaults(&fdata);
          if (cruise_speed > 0 && cruise_speed <= fdata.maxVelocity) {
            fdata.idleVelocity = cruise_speed;
          }
          printf("Cruise Speed: %f\n", fdata.idleVelocity);
          fdata.indexNumber = (uint8_t) numWaypoints + 1;

          ACK::ErrorCode initAck = vehicle->missionManager->init(
                  DJI_MISSION_TYPE::WAYPOINT, responseTimeout, &fdata);
          if (ACK::getError(initAck))
          {
            ACK::getErrorCodeMessage(initAck, __func__);
          }

          vehicle->missionManager->printInfo();
          std::cout << "Initializing Waypoint Mission..\n";
          usleep(100000);

          // Waypoint Mission: Create Waypoints
          std::vector<DJI::OSDK::WayPointSettings> generatedWaypts = createWaypoints(vehicle,wp_array, start_alt);
          std::cout << "Creating Waypoints..\n";

          // Waypoint Mission: Upload the waypoints
          uploadWaypoints(vehicle, generatedWaypts, responseTimeout);
          std::cout << "Uploading Waypoints..\n";

          // Waypoint Mission: Start
          ACK::ErrorCode startAck =
                  vehicle->missionManager->wpMission->start(responseTimeout);
          if (ACK::getError(startAck))
          {
            ACK::getErrorCodeMessage(startAck, __func__);
          }
          else
          {
            std::cout << "Starting Waypoint Mission.\n";
          }
          if (vehicle->getFwVersion() != Version::M100_31)
          {
            ACK::ErrorCode ack =
                    vehicle->subscribe->removePackage(1, responseTimeout);
          }
          continue;
        }
        if (msg_type == "WayPointStopRequest") {
          std::cout << "Stop WayPoints Mission..." << std::endl;
          Json::Value data = root["WayPointStopRequest"];
          int time_out = data["time_out"].asInt();

          // Waypoint Mission: Stop
          ACK::ErrorCode stopAck =
                  vehicle->missionManager->wpMission->stop(time_out);
          if (ACK::getError(stopAck))
          {
            ACK::getErrorCodeMessage(stopAck, __func__);
          }
          else
          {
            std::cout << "Stopping Waypoint Mission.\n";
          }
        }
        if (msg_type == "WayPointPauseRequest") {
          std::cout << "Pause WayPoints Mission..." << std::endl;
          Json::Value data = root["WayPointPauseRequest"];
          int time_out = data["time_out"].asInt();

          // Waypoint Mission: Pause
          ACK::ErrorCode pauseAck =
                  vehicle->missionManager->wpMission->pause(time_out);
          if (ACK::getError(pauseAck))
          {
            ACK::getErrorCodeMessage(pauseAck, __func__);
          }
          else
          {
            std::cout << "Pausing Waypoint Mission.\n";
          }
        }
        if (msg_type == "WayPointResumeRequest") {
          std::cout << "Resume WayPoints Mission..." << std::endl;
          Json::Value data = root["WayPointResumeRequest"];
          int time_out = data["time_out"].asInt();

          // Waypoint Mission: Resume
          ACK::ErrorCode resumeAck =
                  vehicle->missionManager->wpMission->resume(time_out);
          if (ACK::getError(resumeAck))
          {
            ACK::getErrorCodeMessage(resumeAck, __func__);
          }
          else
          {
            std::cout << "Resuming Waypoint Mission.\n";
          }
        }
        if (msg_type == "MoveOffsetRequest"){
          std::cout << "Move Offset ..." << std::endl;
          Json::Value v = root["MoveOffsetRequest"];
          int xOffset = v["xOffset"].asInt();
          int yOffset = v["yOffset"].asInt();
          int zOffset = v["zOffset"].asInt();
          int yawDesired = v["yawDesired"].asInt();
          //int posThresholdInM = v["posThresholdInM"].asInt();
          //int yawThresholdInDeg = v["yawThresholdInDeg"].asInt();

          if (!inprogress) {
              inprogress = true;
              bool result = moveByPositionOffset(vehicle, xOffset, yOffset, zOffset, yawDesired);
              if (result) {
                  std::cout << "Move Offset successful!" << std::endl;
              } else {
                  std::cerr << "Move Offset failed!" << std::endl;
              }
              inprogress = false;
          }
          continue;
        }
        if (msg_type == "TelemetryRequest"){
          std::cout << "Telemetry Request ..." << std::endl;
          continue;
        }
        if (msg_type == "Monitoring"){
          std::cout << "Monitoring Request ..." << std::endl;
          monitor_switch = root["Monitoring"].asBool();
          std::cout << "Monitoring:" <<  monitor_switch << std::endl;
          continue;
        }

      }

    }
    catch (const std::exception & ex) {
      std::exception_ptr p = std::current_exception();
      std::cerr << "channel receive exception" << std::endl;
      std::cerr << ex.what() << std::endl;
    }
    std::cout << "restaring ... " + getCurrentTime() << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(3));

  }}


/*! main
 *
 */
int
main(int argc, char** argv)
{
  // Initialize variables
  int functionTimeout = 1;
  int retryConnectInterval = 2;
  std::string remoteHost;
  std::string user;
  std::string passwd;

  // Setup OSDK.
  Vehicle* vehicle = setupOSDK(argc, argv);
  if (vehicle == NULL)
  {
    std::cout << "Vehicle not initialized, exiting.\n";
    return -1;
  }

  // Obtain Control Authority
  vehicle->obtainCtrlAuthority(functionTimeout);

  // Config file loading
  std::string config_file_path;
  if (argc > 1)
  {
    config_file_path = argv[1];
    DJI_Environment* environment = new DJI_Environment(config_file_path);
    if (!environment->getConfigResult())
    {
      // We were unable to read the config file. Exit.
      return -1;
    }
    remoteHost = environment->getRemoteHost().c_str();
    user = environment->getUser().c_str();
    passwd = environment->getPasswd().c_str();
    if (remoteHost == "" || user == "" || passwd == "")
    {
      std::cerr << "message server config not found" << std::endl;
      return -1;

    }
  }

  try {
      //create message send thread
      std::cout << "starting message tx channel thread ...\n";
      std::thread msgtx_thread(channelSend, vehicle, remoteHost, user, passwd);
      std::cout << "starting message rx channel thread ...\n";
      std::thread msgrx_thread(channelReceive, vehicle, remoteHost, user, passwd);

      msgtx_thread.join();
      msgrx_thread.join();
  }
  catch (const std::exception & ex) {
      std::exception_ptr p = std::current_exception();
      std::cerr << "main routine exception" << std::endl;
      std::cerr << ex.what() << std::endl;
  }







/*
  // Display interactive prompt
  std::cout
    << "| Available commands:                                            |"
    << std::endl;
  std::cout
    << "| [a] Monitored Takeoff + Landing                                |"
    << std::endl;
  std::cout
    << "| [b] Monitored Takeoff + Position Control + Landing             |"
    << std::endl;
  char inputChar;
  std::cin >> inputChar;

  switch (inputChar)
  {
    case 'a':
      monitoredTakeoff(vehicle);
      monitoredLanding(vehicle);
      break;
    case 'b':
      monitoredTakeoff(vehicle);
      moveByPositionOffset(vehicle, 0, 6, 6, 30);
      moveByPositionOffset(vehicle, 6, 0, -3, -30);
      moveByPositionOffset(vehicle, -6, -6, 0, 0);
      monitoredLanding(vehicle);
      break;
    default:
      break;
  }
*/
  delete (vehicle);
  return 0;
}




/*! Monitored Takeoff (Blocking API call). Return status as well as ack.
    This version of takeoff makes sure your aircraft actually took off
    and only returns when takeoff is complete.
    Use unless you want to do other stuff during takeoff - this will block
    the main thread.
!*/
bool
monitoredTakeoff(Vehicle* vehicle, int timeout)
{
  //@todo: remove this once the getErrorCode function signature changes
  char func[50];
  int  pkgIndex;

  if (vehicle->getFwVersion() != Version::M100_31)
  {
    // Telemetry: Verify the subscription
    ACK::ErrorCode subscribeStatus;
    subscribeStatus = vehicle->subscribe->verify(timeout);
    if (ACK::getError(subscribeStatus) != ACK::SUCCESS)
    {
      ACK::getErrorCodeMessage(subscribeStatus, func);
      return false;
    }

    // Telemetry: Subscribe to flight status and mode at freq 10 Hz
    pkgIndex		      = 0;
    int       freq            = 10;
    TopicName topicList10Hz[] = { TOPIC_STATUS_FLIGHT,
                                  TOPIC_STATUS_DISPLAYMODE };
    int  numTopic        = sizeof(topicList10Hz) / sizeof(topicList10Hz[0]);
    bool enableTimestamp = false;

    bool pkgStatus = vehicle->subscribe->initPackageFromTopicList(
      pkgIndex, numTopic, topicList10Hz, enableTimestamp, freq);
    if (!(pkgStatus))
    {
      // Cleanup before return
      vehicle->subscribe->removePackage(pkgIndex, timeout);
      return pkgStatus;
    }
    subscribeStatus = vehicle->subscribe->startPackage(pkgIndex, timeout);
    if (ACK::getError(subscribeStatus) != ACK::SUCCESS)
    {
      ACK::getErrorCodeMessage(subscribeStatus, func);
      // Cleanup before return
      vehicle->subscribe->removePackage(pkgIndex, timeout);
      return false;
    }
  }

  // Start takeoff
  ACK::ErrorCode takeoffStatus = vehicle->control->takeoff(timeout);
  if (ACK::getError(takeoffStatus) != ACK::SUCCESS)
  {
    ACK::getErrorCodeMessage(takeoffStatus, func);
    return false;
  }

  // First check: Motors started
  int motorsNotStarted = 0;
  int timeoutCycles    = 20;

  if (vehicle->getFwVersion() != Version::M100_31)
  {
    while (vehicle->subscribe->getValue<TOPIC_STATUS_FLIGHT>() !=
             VehicleStatus::FlightStatus::ON_GROUND &&
           vehicle->subscribe->getValue<TOPIC_STATUS_DISPLAYMODE>() !=
             VehicleStatus::DisplayMode::MODE_ENGINE_START &&
           motorsNotStarted < timeoutCycles)
    {
      motorsNotStarted++;
      usleep(100000);
    }

    if (motorsNotStarted == timeoutCycles)
    {
      std::cout << "Takeoff failed. Motors are not spinning." << std::endl;
      // Cleanup
      if (vehicle->getFwVersion() != Version::M100_31)
      {
	vehicle->subscribe->removePackage(0, timeout);
      }
      return false;
    }
    else
    {
      std::cout << "Motors spinning...\n";
    }
  }
  else
  {
    while ((vehicle->broadcast->getStatus().flight !=
            DJI::OSDK::VehicleStatus::M100FlightStatus::TAKEOFF) &&
           motorsNotStarted < timeoutCycles)
    {
      motorsNotStarted++;
      usleep(100000);
    }

    if(motorsNotStarted == timeoutCycles)
    {
      std::cout << "Successful TakeOff!" << std::endl;
    }
  }

  // Second check: In air
  int stillOnGround = 0;
  timeoutCycles     = 110;

  if (vehicle->getFwVersion() != Version::M100_31)
  {
    while (vehicle->subscribe->getValue<TOPIC_STATUS_FLIGHT>() !=
             VehicleStatus::FlightStatus::IN_AIR &&
           (vehicle->subscribe->getValue<TOPIC_STATUS_DISPLAYMODE>() !=
              VehicleStatus::DisplayMode::MODE_ASSISTED_TAKEOFF ||
            vehicle->subscribe->getValue<TOPIC_STATUS_DISPLAYMODE>() !=
              VehicleStatus::DisplayMode::MODE_AUTO_TAKEOFF) &&
           stillOnGround < timeoutCycles)
    {
      stillOnGround++;
      usleep(100000);
    }

    if (stillOnGround == timeoutCycles)
    {
     std::cout << "Takeoff failed. Aircraft is still on the ground, but the "
		  "motors are spinning."
	       << std::endl;
     // Cleanup
     if (vehicle->getFwVersion() != Version::M100_31)
     {
       vehicle->subscribe->removePackage(0, timeout);
     }
     return false;
    }
    else
    {
     std::cout << "Ascending...\n";
    }
  }
  else
  {
    while ((vehicle->broadcast->getStatus().flight !=
            DJI::OSDK::VehicleStatus::M100FlightStatus::IN_AIR_STANDBY) &&
           stillOnGround < timeoutCycles)
    {
      stillOnGround++;
      usleep(100000);
    }

    if(stillOnGround == timeoutCycles)
    {
      std::cout << "Aircraft in air!" << std::endl;
    }
  }

  // Final check: Finished takeoff
  if (vehicle->getFwVersion() != Version::M100_31)
  {
    while (vehicle->subscribe->getValue<TOPIC_STATUS_DISPLAYMODE>() ==
             VehicleStatus::DisplayMode::MODE_ASSISTED_TAKEOFF ||
           vehicle->subscribe->getValue<TOPIC_STATUS_DISPLAYMODE>() ==
             VehicleStatus::DisplayMode::MODE_AUTO_TAKEOFF)
    {
      sleep(1);
    }

    if (vehicle->getFwVersion() != Version::M100_31)
    {
      if (vehicle->subscribe->getValue<TOPIC_STATUS_DISPLAYMODE>() !=
	    VehicleStatus::DisplayMode::MODE_P_GPS ||
	  vehicle->subscribe->getValue<TOPIC_STATUS_DISPLAYMODE>() !=
	    VehicleStatus::DisplayMode::MODE_ATTITUDE)
      {
	std::cout << "Successful takeoff!\n";
      }
      else
      {
	std::cout
	  << "Takeoff finished, but the aircraft is in an unexpected mode. "
	     "Please connect DJI GO.\n";
	vehicle->subscribe->removePackage(0, timeout);
	return false;
      }
    }
  }
  else
  {
    float32_t delta;
    Telemetry::GlobalPosition currentHeight;
    Telemetry::GlobalPosition deltaHeight = vehicle->broadcast->getGlobalPosition();

    do
    {
      sleep(3);
      currentHeight = vehicle->broadcast->getGlobalPosition();
      delta         = fabs(currentHeight.altitude - deltaHeight.altitude);
      deltaHeight.altitude   = currentHeight.altitude;
    } while (delta >= 0.009);

    std::cout << "Aircraft hovering at " << currentHeight.altitude << "m!\n";
  }

  // Cleanup
  if (vehicle->getFwVersion() != Version::M100_31)
  {
    ACK::ErrorCode ack = vehicle->subscribe->removePackage(pkgIndex, timeout);
    if (ACK::getError(ack))
    {
      std::cout
        << "Error unsubscribing; please restart the drone/FC to get back "
           "to a clean state.\n";
    }
  }

  return true;
}

/*! Position Control. Allows you to set an offset from your current
    location. The aircraft will move to that position and stay there.
    Typical use would be as a building block in an outer loop that does not
    require many fast changes, perhaps a few-waypoint trajectory. For smoother
    transition and response you should convert your trajectory to attitude
    setpoints and use attitude control or convert to velocity setpoints
    and use velocity control.
!*/
int
moveByPositionOffset(Vehicle* vehicle, float xOffsetDesired,
                     float yOffsetDesired, float zOffsetDesired,
                     float yawDesired, float posThresholdInM,
                     float yawThresholdInDeg)
{
  // Set timeout: this timeout is the time you allow the drone to take to finish
  // the
  // mission
  int responseTimeout              = 1;
  int timeoutInMilSec              = 10000;
  int controlFreqInHz              = 50; // Hz
  int cycleTimeInMs                = 1000 / controlFreqInHz;
  int outOfControlBoundsTimeLimit  = 10 * cycleTimeInMs; // 10 cycles
  int withinControlBoundsTimeReqmt = 50 * cycleTimeInMs; // 50 cycles
  int pkgIndex;

  //@todo: remove this once the getErrorCode function signature changes
  char func[50];

  if (vehicle->getFwVersion() != Version::M100_31)
  {
    // Telemetry: Verify the subscription
    ACK::ErrorCode subscribeStatus;
    subscribeStatus = vehicle->subscribe->verify(responseTimeout);
    if (ACK::getError(subscribeStatus) != ACK::SUCCESS)
    {
      ACK::getErrorCodeMessage(subscribeStatus, func);
      return false;
    }

    // Telemetry: Subscribe to quaternion, fused lat/lon and altitude at freq 50
    // Hz
    pkgIndex                  = 0;
    int       freq            = 50;
    TopicName topicList50Hz[] = { TOPIC_QUATERNION, TOPIC_GPS_FUSED };
    int       numTopic = sizeof(topicList50Hz) / sizeof(topicList50Hz[0]);
    bool      enableTimestamp = false;

    bool pkgStatus = vehicle->subscribe->initPackageFromTopicList(
      pkgIndex, numTopic, topicList50Hz, enableTimestamp, freq);
    if (!(pkgStatus))
    {
      // Cleanup before return
      vehicle->subscribe->removePackage(pkgIndex, 5);
      return pkgStatus;
    }
    subscribeStatus =
      vehicle->subscribe->startPackage(pkgIndex, responseTimeout);
    if (ACK::getError(subscribeStatus) != ACK::SUCCESS)
    {
      ACK::getErrorCodeMessage(subscribeStatus, func);
      // Cleanup before return
      vehicle->subscribe->removePackage(pkgIndex, responseTimeout);
      return false;
    }
  }

  // Wait for data to come in
  sleep(1);

  // Get data

  // Global position retrieved via subscription
  Telemetry::TypeMap<TOPIC_GPS_FUSED>::type currentSubscriptionGPS;
  Telemetry::TypeMap<TOPIC_GPS_FUSED>::type originSubscriptionGPS;
  // Global position retrieved via broadcast
  Telemetry::GlobalPosition currentBroadcastGP;
  Telemetry::GlobalPosition originBroadcastGP;

  // Convert position offset from first position to local coordinates
  Telemetry::Vector3f localOffset;

  if (vehicle->getFwVersion() != Version::M100_31)
  {
    currentSubscriptionGPS = vehicle->subscribe->getValue<TOPIC_GPS_FUSED>();
    originSubscriptionGPS  = currentSubscriptionGPS;
    localOffsetFromGpsOffset(vehicle, localOffset,
                             static_cast<void*>(&currentSubscriptionGPS),
                             static_cast<void*>(&originSubscriptionGPS));
  }
  else
  {
    currentBroadcastGP = vehicle->broadcast->getGlobalPosition();
    originBroadcastGP  = currentBroadcastGP;
    localOffsetFromGpsOffset(vehicle, localOffset,
                             static_cast<void*>(&currentBroadcastGP),
                             static_cast<void*>(&originBroadcastGP));
  }

  // Get initial offset. We will update this in a loop later.
  double xOffsetRemaining = xOffsetDesired - localOffset.x;
  double yOffsetRemaining = yOffsetDesired - localOffset.y;
  double zOffsetRemaining = zOffsetDesired - (-localOffset.z);

  // Conversions
  double yawDesiredRad     = DEG2RAD * yawDesired;
  double yawThresholdInRad = DEG2RAD * yawThresholdInDeg;

  //! Get Euler angle

  // Quaternion retrieved via subscription
  Telemetry::TypeMap<TOPIC_QUATERNION>::type subscriptionQ;
  // Quaternion retrieved via broadcast
  Telemetry::Quaternion broadcastQ;

  double yawInRad;
  if (vehicle->getFwVersion() != Version::M100_31)
  {
    subscriptionQ = vehicle->subscribe->getValue<TOPIC_QUATERNION>();
    yawInRad = toEulerAngle((static_cast<void*>(&subscriptionQ))).z / DEG2RAD;
  }
  else
  {
    broadcastQ = vehicle->broadcast->getQuaternion();
    yawInRad   = toEulerAngle((static_cast<void*>(&broadcastQ))).z / DEG2RAD;
  }

  int   elapsedTimeInMs     = 0;
  int   withinBoundsCounter = 0;
  int   outOfBounds         = 0;
  int   brakeCounter        = 0;
  int   speedFactor         = 2;
  float xCmd, yCmd, zCmd;
  // There is a deadband in position control
  // the z cmd is absolute height
  // while x and y are in relative
  float zDeadband = 0.12;

  if(vehicle->getFwVersion() == Version::M100_31)
  {
    zDeadband = 0.12 * 10;
  }

  /*! Calculate the inputs to send the position controller. We implement basic
   *  receding setpoint position control and the setpoint is always 1 m away
   *  from the current position - until we get within a threshold of the goal.
   *  From that point on, we send the remaining distance as the setpoint.
   */
  if (xOffsetDesired > 0)
    xCmd = (xOffsetDesired < speedFactor) ? xOffsetDesired : speedFactor;
  else if (xOffsetDesired < 0)
    xCmd =
      (xOffsetDesired > -1 * speedFactor) ? xOffsetDesired : -1 * speedFactor;
  else
    xCmd = 0;

  if (yOffsetDesired > 0)
    yCmd = (yOffsetDesired < speedFactor) ? yOffsetDesired : speedFactor;
  else if (yOffsetDesired < 0)
    yCmd =
      (yOffsetDesired > -1 * speedFactor) ? yOffsetDesired : -1 * speedFactor;
  else
    yCmd = 0;

  if (vehicle->getFwVersion() != Version::M100_31)
  {
    zCmd = currentSubscriptionGPS.altitude + zOffsetDesired;
  }
  else
  {
    zCmd = currentBroadcastGP.altitude + zOffsetDesired;
  }

  //! Main closed-loop receding setpoint position control
  while (elapsedTimeInMs < timeoutInMilSec)
  {

    vehicle->control->positionAndYawCtrl(xCmd, yCmd, zCmd,
                                         yawDesiredRad / DEG2RAD);

    usleep(cycleTimeInMs * 1000);
    elapsedTimeInMs += cycleTimeInMs;

    //! Get current position in required coordinates and units
    if (vehicle->getFwVersion() != Version::M100_31)
    {
      subscriptionQ = vehicle->subscribe->getValue<TOPIC_QUATERNION>();
      yawInRad      = toEulerAngle((static_cast<void*>(&subscriptionQ))).z;
      currentSubscriptionGPS = vehicle->subscribe->getValue<TOPIC_GPS_FUSED>();
      localOffsetFromGpsOffset(vehicle, localOffset,
                               static_cast<void*>(&currentSubscriptionGPS),
                               static_cast<void*>(&originSubscriptionGPS));
    }
    else
    {
      broadcastQ         = vehicle->broadcast->getQuaternion();
      yawInRad           = toEulerAngle((static_cast<void*>(&broadcastQ))).z;
      currentBroadcastGP = vehicle->broadcast->getGlobalPosition();
      localOffsetFromGpsOffset(vehicle, localOffset,
                               static_cast<void*>(&currentBroadcastGP),
                               static_cast<void*>(&originBroadcastGP));
    }

    //! See how much farther we have to go
    xOffsetRemaining = xOffsetDesired - localOffset.x;
    yOffsetRemaining = yOffsetDesired - localOffset.y;
    zOffsetRemaining = zOffsetDesired - (-localOffset.z);

    //! See if we need to modify the setpoint
    if (std::abs(xOffsetRemaining) < speedFactor)
      xCmd = xOffsetRemaining;
    if (std::abs(yOffsetRemaining) < speedFactor)
      yCmd = yOffsetRemaining;

    if(vehicle->getFwVersion() == Version::M100_31 &&
       std::abs(xOffsetRemaining) < posThresholdInM &&
       std::abs(yOffsetRemaining) < posThresholdInM &&
       std::abs(yawInRad - yawDesiredRad) < yawThresholdInRad)
    {
      //! 1. We are within bounds; start incrementing our in-bound counter
      withinBoundsCounter += cycleTimeInMs;
    }
    else if(std::abs(xOffsetRemaining) < posThresholdInM &&
	   std::abs(yOffsetRemaining) < posThresholdInM &&
	   std::abs(zOffsetRemaining) < zDeadband &&
	   std::abs(yawInRad - yawDesiredRad) < yawThresholdInRad)
    {
      //! 1. We are within bounds; start incrementing our in-bound counter
      withinBoundsCounter += cycleTimeInMs;
    }
    else
    {
      if (withinBoundsCounter != 0)
      {
        //! 2. Start incrementing an out-of-bounds counter
        outOfBounds += cycleTimeInMs;
      }
    }
    //! 3. Reset withinBoundsCounter if necessary
    if (outOfBounds > outOfControlBoundsTimeLimit)
    {
      withinBoundsCounter = 0;
      outOfBounds         = 0;
    }
    //! 4. If within bounds, set flag and break
    if (withinBoundsCounter >= withinControlBoundsTimeReqmt)
    {
      break;
    }
  }

  //! Set velocity to zero, to prevent any residual velocity from position
  //! command
  if (vehicle->getFwVersion() != Version::M100_31)
  {
    while (brakeCounter < withinControlBoundsTimeReqmt)
    {
      vehicle->control->emergencyBrake();
      usleep(cycleTimeInMs);
      brakeCounter += cycleTimeInMs;
    }
  }

  if (elapsedTimeInMs >= timeoutInMilSec)
  {
    std::cout << "Task timeout!\n";
    if (vehicle->getFwVersion() != Version::M100_31)
    {
      ACK::ErrorCode ack =
        vehicle->subscribe->removePackage(pkgIndex, responseTimeout);
      if (ACK::getError(ack))
      {
        std::cout << "Error unsubscribing; please restart the drone/FC to get "
                     "back to a clean state.\n";
      }
    }
    return ACK::FAIL;
  }

  if (vehicle->getFwVersion() != Version::M100_31)
  {
    ACK::ErrorCode ack =
      vehicle->subscribe->removePackage(pkgIndex, responseTimeout);
    if (ACK::getError(ack))
    {
      std::cout
        << "Error unsubscribing; please restart the drone/FC to get back "
           "to a clean state.\n";
    }
  }

  return ACK::SUCCESS;
}

/*! Monitored Takeoff (Blocking API call). Return status as well as ack.
    This version of takeoff makes sure your aircraft actually took off
    and only returns when takeoff is complete.
    Use unless you want to do other stuff during takeoff - this will block
    the main thread.
!*/
bool
monitoredLanding(Vehicle* vehicle, int timeout)
{
  //@todo: remove this once the getErrorCode function signature changes
  char func[50];
  int  pkgIndex;

  if (vehicle->getFwVersion() != Version::M100_31)
  {
    // Telemetry: Verify the subscription
    ACK::ErrorCode subscribeStatus;
    subscribeStatus = vehicle->subscribe->verify(timeout);
    if (ACK::getError(subscribeStatus) != ACK::SUCCESS)
    {
      ACK::getErrorCodeMessage(subscribeStatus, func);
      return false;
    }

    // Telemetry: Subscribe to flight status and mode at freq 10 Hz
    pkgIndex                  = 0;
    int       freq            = 10;
    TopicName topicList10Hz[] = { TOPIC_STATUS_FLIGHT,
                                  TOPIC_STATUS_DISPLAYMODE };
    int  numTopic        = sizeof(topicList10Hz) / sizeof(topicList10Hz[0]);
    bool enableTimestamp = false;

    bool pkgStatus = vehicle->subscribe->initPackageFromTopicList(
      pkgIndex, numTopic, topicList10Hz, enableTimestamp, freq);
    if (!(pkgStatus))
    {
      // Cleanup before return
      vehicle->subscribe->removePackage(pkgIndex, timeout);
      return pkgStatus;
    }
    subscribeStatus = vehicle->subscribe->startPackage(pkgIndex, timeout);
    if (ACK::getError(subscribeStatus) != ACK::SUCCESS)
    {
      ACK::getErrorCodeMessage(subscribeStatus, func);
      // Cleanup before return
      vehicle->subscribe->removePackage(pkgIndex, timeout);
      return false;
    }
  }

  // Start landing
  ACK::ErrorCode landingStatus = vehicle->control->land(timeout);
  if (ACK::getError(landingStatus) != ACK::SUCCESS)
  {
    ACK::getErrorCodeMessage(landingStatus, func);
    return false;
  }

  // First check: Landing started
  int landingNotStarted = 0;
  int timeoutCycles     = 20;

  if (vehicle->getFwVersion() != Version::M100_31)
  {
    while (vehicle->subscribe->getValue<TOPIC_STATUS_DISPLAYMODE>() !=
             VehicleStatus::DisplayMode::MODE_AUTO_LANDING &&
           landingNotStarted < timeoutCycles)
    {
      landingNotStarted++;
      usleep(100000);
    }
  }
  else
  {
    while (vehicle->broadcast->getStatus().flight !=
             DJI::OSDK::VehicleStatus::M100FlightStatus::LANDING &&
           landingNotStarted < timeoutCycles)
    {
      landingNotStarted++;
      usleep(100000);
    }
  }

  if (landingNotStarted == timeoutCycles)
  {
    std::cout << "Landing failed. Aircraft is still in the air." << std::endl;
    // Cleanup before return
    ACK::ErrorCode ack = vehicle->subscribe->removePackage(pkgIndex, timeout);
    if (ACK::getError(ack))
    {
      std::cout << "Error unsubscribing; please restart the drone/FC to get "
                   "back to a clean state.\n";
    }
    return false;
  }
  else
  {
    std::cout << "Landing...\n";
  }

  // Second check: Finished landing
  if (vehicle->getFwVersion() != Version::M100_31)
  {
    while (vehicle->subscribe->getValue<TOPIC_STATUS_DISPLAYMODE>() ==
             VehicleStatus::DisplayMode::MODE_AUTO_LANDING &&
           vehicle->subscribe->getValue<TOPIC_STATUS_FLIGHT>() ==
             VehicleStatus::FlightStatus::IN_AIR)
    {
      sleep(1);
    }

    if (vehicle->subscribe->getValue<TOPIC_STATUS_DISPLAYMODE>() !=
          VehicleStatus::DisplayMode::MODE_P_GPS ||
        vehicle->subscribe->getValue<TOPIC_STATUS_DISPLAYMODE>() !=
          VehicleStatus::DisplayMode::MODE_ATTITUDE)
    {
      std::cout << "Successful landing!\n";
    }
    else
    {
      std::cout
        << "Landing finished, but the aircraft is in an unexpected mode. "
           "Please connect DJI GO.\n";
      ACK::ErrorCode ack = vehicle->subscribe->removePackage(pkgIndex, timeout);
      if (ACK::getError(ack))
      {
        std::cout << "Error unsubscribing; please restart the drone/FC to get "
                     "back to a clean state.\n";
      }
      return false;
    }
  }
  else
  {
    while (vehicle->broadcast->getStatus().flight ==
           DJI::OSDK::VehicleStatus::M100FlightStatus::FINISHING_LANDING)
    {
      sleep(1);
    }

    Telemetry::GlobalPosition gp;
    do
    {
      sleep(2);
      gp = vehicle->broadcast->getGlobalPosition();
    } while (gp.altitude != 0);

    if(gp.altitude != 0)
    {
      std::cout
	<< "Landing finished, but the aircraft is in an unexpected mode. "
	   "Please connect DJI GO.\n";
      return false;
    }
    else
    {
      std::cout << "Successful landing!\n";
    }
  }

  // Cleanup
  if (vehicle->getFwVersion() != Version::M100_31)
  {
    ACK::ErrorCode ack = vehicle->subscribe->removePackage(pkgIndex, timeout);
    if (ACK::getError(ack))
    {
      std::cout
        << "Error unsubscribing; please restart the drone/FC to get back "
           "to a clean state.\n";
    }
  }

  return true;
}

// Helper Functions

/*! Very simple calculation of local NED offset between two pairs of GPS
/coordinates.
    Accurate when distances are small.
!*/
void
localOffsetFromGpsOffset(Vehicle* vehicle, Telemetry::Vector3f& deltaNed,
                         void* target, void* origin)
{
  Telemetry::GPSFused*       subscriptionTarget;
  Telemetry::GPSFused*       subscriptionOrigin;
  Telemetry::GlobalPosition* broadcastTarget;
  Telemetry::GlobalPosition* broadcastOrigin;
  double                     deltaLon;
  double                     deltaLat;

  if (vehicle->getFwVersion() != Version::M100_31)
  {
    subscriptionTarget = (Telemetry::GPSFused*)target;
    subscriptionOrigin = (Telemetry::GPSFused*)origin;
    deltaLon   = subscriptionTarget->longitude - subscriptionOrigin->longitude;
    deltaLat   = subscriptionTarget->latitude - subscriptionOrigin->latitude;
    deltaNed.x = deltaLat * C_EARTH;
    deltaNed.y = deltaLon * C_EARTH * cos(subscriptionTarget->latitude);
    deltaNed.z = subscriptionTarget->altitude - subscriptionOrigin->altitude;
  }
  else
  {
    broadcastTarget = (Telemetry::GlobalPosition*)target;
    broadcastOrigin = (Telemetry::GlobalPosition*)origin;
    deltaLon        = broadcastTarget->longitude - broadcastOrigin->longitude;
    deltaLat        = broadcastTarget->latitude - broadcastOrigin->latitude;
    deltaNed.x      = deltaLat * C_EARTH;
    deltaNed.y      = deltaLon * C_EARTH * cos(broadcastTarget->latitude);
    deltaNed.z      = broadcastTarget->altitude - broadcastOrigin->altitude;
  }
}

Telemetry::Vector3f
toEulerAngle(void* quaternionData)
{
  Telemetry::Vector3f    ans;
  Telemetry::Quaternion* quaternion = (Telemetry::Quaternion*)quaternionData;

  double q2sqr = quaternion->q2 * quaternion->q2;
  double t0    = -2.0 * (q2sqr + quaternion->q3 * quaternion->q3) + 1.0;
  double t1 =
    +2.0 * (quaternion->q1 * quaternion->q2 + quaternion->q0 * quaternion->q3);
  double t2 =
    -2.0 * (quaternion->q1 * quaternion->q3 - quaternion->q0 * quaternion->q2);
  double t3 =
    +2.0 * (quaternion->q2 * quaternion->q3 + quaternion->q0 * quaternion->q1);
  double t4 = -2.0 * (quaternion->q1 * quaternion->q1 + q2sqr) + 1.0;

  t2 = (t2 > 1.0) ? 1.0 : t2;
  t2 = (t2 < -1.0) ? -1.0 : t2;

  ans.x = asin(t2);
  ans.y = atan2(t3, t4);
  ans.z = atan2(t1, t0);

  return ans;
}


void
setWaypointInitDefaults(WayPointInitSettings* fdata)
{
    fdata->maxVelocity    = 10;
    fdata->idleVelocity   = 5;
    fdata->finishAction   = 0;
    fdata->executiveTimes = 1;
    fdata->yawMode        = 0;
    fdata->traceMode      = 0;
    fdata->RCLostAction   = 1;
    fdata->gimbalPitch    = 0;
    fdata->latitude       = 0;
    fdata->longitude      = 0;
    fdata->altitude       = 0;
}

void
setWaypointDefaults(WayPointSettings* wp) {
  wp->damping = 0;
  wp->yaw = 0;
  wp->gimbalPitch = 0;
  wp->turnMode = 0;
  wp->hasAction = 0;
  wp->actionTimeLimit = 100;
  wp->actionNumber = 0;
  wp->actionRepeat = 0;
  for (int i = 0; i < 16; ++i) {
    wp->commandList[i] = 0;
    wp->commandParameter[i] = 0;
  }
  for (int i = 0; i < 8; ++i)
  {
    wp->reserved[i] = 0;
  }
}

std::vector<DJI::OSDK::WayPointSettings>
createWaypoints(DJI::OSDK::Vehicle* vehicle,Json::Value wp_array, float32_t start_alt){
  // Create Start Waypoint
  WayPointSettings start_wp;
  setWaypointDefaults(&start_wp);

  // Global position retrieved via subscription
  Telemetry::TypeMap<TOPIC_GPS_FUSED>::type subscribeGPosition;
  // Global position retrieved via broadcast
  Telemetry::GlobalPosition broadcastGPosition;

  if (vehicle->getFwVersion() != Version::M100_31)
  {
    subscribeGPosition = vehicle->subscribe->getValue<TOPIC_GPS_FUSED>();
    start_wp.latitude  = subscribeGPosition.latitude;
    start_wp.longitude = subscribeGPosition.longitude;
    start_wp.altitude  = start_alt;
    printf("Waypoint created at (LLA): %f \t%f \t%f\n",
           subscribeGPosition.latitude, subscribeGPosition.longitude,
           start_alt);
  }
  else
  {
    broadcastGPosition = vehicle->broadcast->getGlobalPosition();
    start_wp.latitude  = broadcastGPosition.latitude;
    start_wp.longitude = broadcastGPosition.longitude;
    start_wp.altitude  = start_alt;
    printf("Waypoint created at (LLA): %f \t%f \t%f\n",
           broadcastGPosition.latitude, broadcastGPosition.longitude,
           start_alt);
  }
  // Let's create a vector to store our waypoints in.
  std::vector<DJI::OSDK::WayPointSettings> wp_list;

  // First waypoint
  start_wp.index = 0;
  wp_list.push_back(start_wp);

  for (int i=0; i < wp_array.size(); i++){
    WayPointSettings  wp;
    setWaypointDefaults(&wp);
    wp.index     =  i + 1;
    wp.longitude = (float64_t) getRadFromDeg(wp_array[i][0].asDouble());
    wp.latitude = (float64_t) getRadFromDeg(wp_array[i][1].asDouble());
    wp.altitude = (float32_t) wp_array[i][2].asFloat();
    std::cout << "wp" << i << ":" << wp.longitude << "," << wp.latitude << ","<< wp.altitude << std::endl;
    wp_list.push_back(wp);
  }
  return wp_list;
}

void
uploadWaypoints(Vehicle*                                  vehicle,
                std::vector<DJI::OSDK::WayPointSettings>& wp_list,
                int                                       responseTimeout)
{
  for (std::vector<WayPointSettings>::iterator wp = wp_list.begin();
       wp != wp_list.end(); ++wp)
  {
    printf("Waypoint created at (LLA): %f \t%f \t%f\n ", wp->latitude,
           wp->longitude, wp->altitude);
    ACK::WayPointIndex wpDataACK =
            vehicle->missionManager->wpMission->uploadIndexData(&(*wp),
                                                                responseTimeout);

    ACK::getErrorCodeMessage(wpDataACK.ack, __func__);
  }
}


bool
setUpSubscription(DJI::OSDK::Vehicle* vehicle, int responseTimeout)
{
  // Telemetry: Verify the subscription
  ACK::ErrorCode subscribeStatus;

  subscribeStatus = vehicle->subscribe->verify(responseTimeout);
  if (ACK::getError(subscribeStatus) != ACK::SUCCESS)
  {
    ACK::getErrorCodeMessage(subscribeStatus, __func__);
    return false;
  }

  // Telemetry: Subscribe to flight status and mode at freq 10 Hz
  int       freq            = 10;
  TopicName topicList10Hz[] = { TOPIC_GPS_FUSED };
  int       numTopic        = sizeof(topicList10Hz) / sizeof(topicList10Hz[0]);
  bool      enableTimestamp = false;

  bool pkgStatus = vehicle->subscribe->initPackageFromTopicList(
          1, numTopic, topicList10Hz, enableTimestamp, freq);
  if (!(pkgStatus))
  {
    return pkgStatus;
  }

  // Start listening to the telemetry data
  subscribeStatus =
          vehicle->subscribe->startPackage(1, responseTimeout);
  if (ACK::getError(subscribeStatus) != ACK::SUCCESS)
  {
    ACK::getErrorCodeMessage(subscribeStatus, __func__);
    // Cleanup
    ACK::ErrorCode ack =
            vehicle->subscribe->removePackage(1, responseTimeout);
    if (ACK::getError(ack))
    {
      std::cout << "Error unsubscribing; please restart the drone/FC to get "
              "back to a clean state.\n";
    }
    return false;
  }
  return true;
}
