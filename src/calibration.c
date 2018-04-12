#include "calibration.h"

/**
 * @brief Run a full linear profile test.
 *
 * The robot will accelerate and maintain the target speed for 0.5 m and then
 * decelerate back to zero speed. There is no angular speed in this test, only
 * linear movement. During all this test information about the relevant linear
 * speed variables is logged periodically for later analysis.
 */
void run_linear_speed_profile(void)
{
	int32_t start_micrometers;

	disable_walls_control();
	enable_motor_control();
	each(10, log_linear_speed, 1000);
	set_target_angular_speed(0.);
	set_target_linear_speed(get_max_linear_speed());
	start_micrometers = get_encoder_average_micrometers();
	while (get_encoder_average_micrometers() - start_micrometers < 500000) {
		log_linear_speed();
		sleep_ticks(1);
	}
	set_target_angular_speed(0.);
	set_target_linear_speed(0.);
	each(1, log_linear_speed, 2000);
	reset_motion();
}

/**
 * @brief Run a full angular profile test.
 *
 * The robot will accelerate, maintain the target speed for a while and then
 * decelerate back to zero speed. There is no linear speed in this test, only
 * rotational movement. During all this test information about the relevant
 * angular speed variables is logged periodically for later analysis.
 *
 * The target turn is 3 * PI radians.
 */
void run_angular_speed_profile(void)
{
	float target_angular_speed = 4 * PI;

	disable_walls_control();
	enable_motor_control();
	each(10, log_angular_speed, 1000);
	set_target_angular_speed(target_angular_speed);
	set_target_linear_speed(0.);
	each(10, log_angular_speed, 1000 * (3 * PI) / target_angular_speed);
	set_target_angular_speed(0.);
	set_target_linear_speed(0.);
	each(10, log_angular_speed, 2000);
	reset_motion();
}

/**
 * @brief Run a profiling test for sensors distance calculation.
 *
 * This test executes 1000 times the function that reads and transforms sensors
 * input to distance. Then, it logs the total time on clock ticks.
 */
void run_distances_profiling(void)
{
	uint32_t clock_tick_start, clock_tick_end;
	uint16_t i;

	clock_tick_start = get_clock_ticks();
	for (i = 0; i < 1000; i++)
		update_distance_readings();
	clock_tick_end = get_clock_ticks();
	LOG_INFO("Clock ticks %" PRIu32, (clock_tick_end - clock_tick_start));
}

/**
 * @brief Run a static 90-degree right turn speed profile.
 *
 * The robot will accelerate to reach the target angular speed and decelerate
 * just in time to complete the 90 degree turn.
 */
void run_static_turn_right_profile(void)
{
	float target_angular_speed = 4 * PI;

	disable_walls_control();
	enable_motor_control();
	each(10, log_angular_speed, 1000);
	set_target_angular_speed(target_angular_speed);
	set_target_linear_speed(0.);
	each(10, log_angular_speed, 1000 * (PI / 2.) / target_angular_speed);
	set_target_angular_speed(0);
	set_target_linear_speed(0.);
	each(10, log_angular_speed, 200);
	reset_motion();
}

/**
 * @brief Micrometers per count calibration.
 *
 * Assumes the robot is positioned at the start of a cell, with its tail
 * touching the back wall. It will accelerate in a straight line and will stop
 * touching its nose with last cell's front wall.
 *
 * Side walls control is activated so it is recommended to always have side
 * walls when running this test.
 *
 * @param[in] cells Number of cells to run across before stopping.
 */
void run_micrometers_per_count_calibration(unsigned int cells)
{
	float linear_acceleration = get_linear_acceleration();
	float linear_deceleration = get_linear_deceleration();
	float max_linear_speed = get_max_linear_speed();

	set_linear_acceleration(4.);
	set_linear_deceleration(4.);
	set_max_linear_speed(0.4);

	reset_motion();
	side_sensors_calibration();
	enable_motor_control();

	side_sensors_control(true);
	front_sensors_control(false);

	accelerate(get_encoder_average_micrometers(),
		   CELL_DIMENSION * cells - WALL_WIDTH / 2. - MOUSE_TAIL);
	disable_walls_control();
	decelerate(get_encoder_average_micrometers(),
		   CELL_DIMENSION - WALL_WIDTH / 2. - MOUSE_HEAD, 0.);
	reset_control_errors();

	set_linear_acceleration(linear_acceleration);
	set_linear_deceleration(linear_deceleration);
	set_max_linear_speed(max_linear_speed);

	reset_motion();
}

/**
 * @brief Front sensors calibration funtion.
 *
 * Assumes the robot is positioned at the start of a cell, with its tail
 * touching the back wall. It will accelerate in a straight line and will stop
 * touching its nose with the next cell's front wall.
 *
 * During this movement the robot will be logging information about the front
 * sensors.
 */
void run_front_sensors_calibration(void)
{
	float linear_acceleration = get_linear_acceleration();
	int32_t target_micrometers;
	int32_t micrometers_to_stop;

	disable_walls_control();
	enable_motor_control();

	set_linear_acceleration(4.);

	target_micrometers = get_encoder_average_micrometers() +
			     1.3 * CELL_DIMENSION * MICROMETERS_PER_METER;
	set_target_angular_speed(0.);
	set_target_linear_speed(.3);
	micrometers_to_stop = (int32_t)required_micrometers_to_speed(0.);
	while (get_encoder_average_micrometers() <
	       target_micrometers - micrometers_to_stop)
		log_front_sensors_calibration();
	set_target_angular_speed(0.);
	set_target_linear_speed(0.);
	each(2, log_front_sensors_calibration, 200);

	set_linear_acceleration(linear_acceleration);

	reset_motion();
}
