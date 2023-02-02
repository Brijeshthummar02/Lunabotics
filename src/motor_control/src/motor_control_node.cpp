/*
 * motor_control_node.cpp
 * Sends raw canbus msgs according to motor config.
 * VERSION: 0.0.3
 * Last changed: January 2023
 * Original Author: Jude Sauve <sauve031@umn.edu>
 * Maintainer: Anthony Brogni <brogn002@umn.edu>
 * MIT License
 * Copyright (c) 2018 GOFIRST-Robotics
 */

// Import ROS 2 Libraries
#include "rclcpp/rclcpp.hpp"
#include "can_msgs/msg/frame.hpp"
#include "geometry_msgs/msg/twist.hpp"
#include "std_msgs/msg/string.hpp"

// Import Native C++ Libraries
#include <string>
#include <stdint.h>

typedef int32_t S32; // Signed 32-bit integer
typedef uint32_t U32; // Unsigned 32-bit integer

// Global Variables
void send_can(U32 id, S32 data);
void send_can_bool(U32 id, bool data);
double linear_vel_cmd = 0.0;
double angular_vel_cmd = 0.0;

bool digging = false;
bool offloading = false;

// counter for printing less CAN frames
int count = 0;

// Define CAN IDs Here //
U32 FRONT_LEFT_DRIVE = 0x001;
U32 BACK_LEFT_DRIVE = 0x002;
U32 FRONT_RIGHT_DRIVE = 0x003;
U32 BACK_RIGHT_DRIVE = 0x004;
U32 DIGGER_DEPTH_MOTOR = 0x005;
U32 DIGGER_ROTATION_MOTOR = 0x006;
U32 DIGGER_DRUM_BELT_MOTOR = 0x007;
U32 CONVEYOR_BELT_MOTOR = 0x008;
U32 OFFLOAD_BELT_MOTOR = 0x009;

// Define Motor Power/Speeds Here //
double DIGGER_ROTATION_POWER = 0.5;
double DIGGER_DEPTH_POWER = 0.5;
double DRUM_BELT_POWER = 0.5;
double CONVEYOR_BELT_POWER = 0.5;
double OFFLOAD_BELT_POWER = 0.5;

using namespace std::chrono_literals;
using std::placeholders::_1;

class PublishersAndSubscribers : public rclcpp::Node
{
  // Generic method for sending data over the CAN bus
  void send_can(U32 id, S32 data) {
    can_msgs::msg::Frame can_msg; // Construct a new CAN message

    can_msg.is_rtr = false;
    can_msg.is_error = false;
    can_msg.is_extended = true;

    can_msg.id = id; // Set the CAN ID for this message

    can_msg.dlc = 4U; // Size of the data array
    can_msg.data[0] = (data >> 24) & 0xFF;
    can_msg.data[1] = (data >> 16) & 0xFF;
    can_msg.data[2] = (data >> 8) & 0xFF;
    can_msg.data[3] = data & 0xFF;
    
    can_pub->publish(can_msg); // Publish our new CAN message to the ROS 2 topic
  }

  // Set the percent power of the motor between -1.0 and 1.0
  void vesc_set_duty_cycle(U32 id, double percentPower) { 
    // Safety check on power
    percentPower = std::min(percentPower, 1.0);
    percentPower = std::max(percentPower, -1.0);

    S32 data = percentPower * 100000.0; // Convert from percent power to a signed 32-bit integer

    send_can(id, data);
    RCLCPP_INFO(this->get_logger(), "Setting the duty cycle of CAN ID: %lu to %f", id, percentPower); // Print to the terminal
  }

  // Set the current draw of the motor in amps
  void vesc_set_current(U32 id, double current) { 
    S32 data = current * 1000.0; // Convert from current in amps to a signed 32-bit integer

    send_can(id, data);
    RCLCPP_INFO(this->get_logger(), "Setting the current draw of CAN ID: %lu to %f amps", id, current); // Print to the terminal
  }

  // eRPM = "electrical RPM" = RPM * (number of poles the motor has / 2)
  void vesc_set_eRPM(U32 id, double erpm) {
    S32 data = erpm;

    send_can(id, data);
    RCLCPP_INFO(this->get_logger(), "Setting the eRPM of CAN ID: %lu to %f", id, erpm); // Print to the terminal
  }

public:
  PublishersAndSubscribers()
  : Node("publishers_and_subscribers")
  {
    can_pub = this->create_publisher<can_msgs::msg::Frame>("CAN/can0/transmit", 1); // The name of this topic is determined by our CAN_bridge node
    can_sub = this->create_subscription<can_msgs::msg::Frame>("CAN/can1/receive", 1, std::bind(&PublishersAndSubscribers::CAN_callback, this, _1)); // The name of this topic is determined by our CAN_bridge node
    cmd_vel_sub = this->create_subscription<geometry_msgs::msg::Twist>("cmd_vel", 1, std::bind(&PublishersAndSubscribers::velocity_callback, this, _1));
    actuators_sub = this->create_subscription<std_msgs::msg::String>("cmd_actuators", 1, std::bind(&PublishersAndSubscribers::actuators_callback, this, _1));
    timer = this->create_wall_timer(500ms, std::bind(&PublishersAndSubscribers::timer_callback, this));
  }

private:
  void velocity_callback(const geometry_msgs::msg::Twist::SharedPtr msg) const
  {
    linear_vel_cmd = msg->linear.x;
    angular_vel_cmd = msg->angular.x;
  }
  // Listen for status frames sent by our VESC motor controllers
  void CAN_callback(const can_msgs::msg::Frame::SharedPtr can_msg) const
  {
    U32 id = can_msg->id & 0xFF;
    std::array<unsigned char, 8> data = can_msg->data; // bytes 0-3 = eRPM, bytes 4-5 = average current, bytes 6-7 = latest duty cycle
    count++;

    U32 eRPM = (data[0]<<24) + (data[1]<<16) + (data[2]<<8) + data[3];
    U32 avgMotorCurrent =((data[4]<<8) + data[5]) / 10;
    U32 dutyCycleNow = ((data[6]<<8) + data[7]) / 1000;

    if(count >= 60) {
      RCLCPP_INFO(this->get_logger(), "Recieved status frame from CAN ID %lu with the following data:", id);
      RCLCPP_INFO(this->get_logger(), "eRPM: %lu average motor current: %lu latest duty cycle: %lu", eRPM, avgMotorCurrent, dutyCycleNow);
      count = 0;
    }
  }
  void actuators_callback(const std_msgs::msg::String::SharedPtr msg) const
  {
    //RCLCPP_INFO(this->get_logger(), "I heard this actuator_cmd: '%s'", msg->data.c_str());
    
    // Determine whether the digging should be on or off right now
    if(msg->data == "STOP_ALL_ACTUATORS") {
      digging = false;
      offloading = false;
    }
    else if(msg->data == "DIGGER_ON") {
      digging = true;
      offloading = false;
    } else if(msg->data == "OFFLOADING_ON") {
      offloading = true;
      digging = false;
    }
  }
  void timer_callback()
  {
    // Send drivetrain CAN messages
    vesc_set_duty_cycle(FRONT_LEFT_DRIVE, linear_vel_cmd - angular_vel_cmd);
    vesc_set_duty_cycle(BACK_LEFT_DRIVE, linear_vel_cmd - angular_vel_cmd);
    vesc_set_duty_cycle(FRONT_RIGHT_DRIVE, (linear_vel_cmd + angular_vel_cmd) * -1.0); // Multiply by -1.0 to invert motor direction
    vesc_set_duty_cycle(BACK_RIGHT_DRIVE, (linear_vel_cmd + angular_vel_cmd) * -1.0); // Multiply by -1.0 to invert motor direction

    // Send digging CAN messages
    vesc_set_duty_cycle(DIGGER_ROTATION_MOTOR, digging ? DIGGER_ROTATION_POWER : 0.0);
    vesc_set_duty_cycle(DIGGER_DRUM_BELT_MOTOR, digging ? DRUM_BELT_POWER : 0.0);
    vesc_set_duty_cycle(CONVEYOR_BELT_MOTOR, digging ? CONVEYOR_BELT_POWER : 0.0);

    // Send offloader CAN messages
    vesc_set_duty_cycle(OFFLOAD_BELT_MOTOR, offloading ? CONVEYOR_BELT_POWER : 0.0);

    // Send digger depth messages
    // TODO: We are gonna have to be careful about this; use the encoder to extend/retract to the right positions
  }
  rclcpp::TimerBase::SharedPtr timer;
  rclcpp::Publisher<can_msgs::msg::Frame>::SharedPtr can_pub;
  rclcpp::Subscription<can_msgs::msg::Frame>::SharedPtr can_sub;
  rclcpp::Subscription<geometry_msgs::msg::Twist>::SharedPtr cmd_vel_sub;
  rclcpp::Subscription<std_msgs::msg::String>::SharedPtr actuators_sub;
};

// Main method for the node
int main(int argc, char** argv){
  // Initialize ROS 2
  rclcpp::init(argc, argv);

  // Spin the node
  rclcpp::spin(std::make_shared<PublishersAndSubscribers>());

  // Free up any resources being used by the node
  rclcpp::shutdown();
  return 0;
}