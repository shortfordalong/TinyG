/*
 * settings_DIYLILCNC - Chris Reilly's CIY LIL CNC project - settings profile
 * Part of TinyG project
 *
 * Copyright (c) 2011 Alden S. Hart Jr.
 *
 * TinyG is free software: you can redistribute it and/or modify it 
 * under the terms of the GNU General Public License as published by 
 * the Free Software Foundation, either version 3 of the License, 
 * or (at your option) any later version.
 *
 * TinyG is distributed in the hope that it will be useful, but 
 * WITHOUT ANY WARRANTY; without even the implied warranty of 
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. 
 * See the GNU General Public License for details.
 *
 * You should have received a copy of the GNU General Public License 
 * along with TinyG  If not, see <http://www.gnu.org/licenses/>.
 */

/***********************************************************************/
/**** DIY LIL CNC profile **********************************************/
/***********************************************************************/

//Note: DIYLILCNC is typically run in inches, but these settings are in mm

#define JERK_MAX 			(20000000 * 25.4)	// $xJM
#define CORNER_DELTA 		(0.002 * 25.4)		// $xCD
#define CORNER_ACCELERATION (2000 * 25.4)		// $JA

// motor values
#define M1_MOTOR_MAP X				// $1MA - motor maps to axis
#define M2_MOTOR_MAP Y
#define M3_MOTOR_MAP Z
#define M4_MOTOR_MAP A

#define M1_STEP_ANGLE 1.8			// degrees per whole step
#define M2_STEP_ANGLE 1.8
#define M3_STEP_ANGLE 1.8
#define M4_STEP_ANGLE 1.8

#define M1_TRAVEL_PER_REV (1.6 * 25.4) 
#define M2_TRAVEL_PER_REV (1.6 * 25.4)
#define M3_TRAVEL_PER_REV (0.0625 * 25.4)
#define M4_TRAVEL_PER_REV 18		// degrees traveled per motor rev

#define M1_MICROSTEPS 2				// one of: 8, 4, 2, 1
#define M2_MICROSTEPS 2
#define M3_MICROSTEPS 4
#define M4_MICROSTEPS 8

#define M1_POLARITY 0				// 0=normal, 1=reversed
#define M2_POLARITY 1				// Y is inverted
#define M3_POLARITY 1				// Z is inverted
#define M4_POLARITY 0

#define M1_POWER_MODE FALSE			// TRUE=low power idle enabled 
#define M2_POWER_MODE FALSE
#define M3_POWER_MODE TRUE
#define M4_POWER_MODE TRUE

// axis values
#define X_AXIS_MODE AXIS_STANDARD	// see gcode.h for valid values
#define Y_AXIS_MODE AXIS_STANDARD
#define Z_AXIS_MODE AXIS_STANDARD
//#define Z_AXIS_MODE AXIS_INHIBITED // Z kill

#define A_AXIS_MODE AXIS_RADIUS
#define B_AXIS_MODE AXIS_RADIUS
#define C_AXIS_MODE AXIS_RADIUS

#define X_SEEK_RATE_MAX (60 * 25.4) // G0 max seek rate in mm/min
#define Y_SEEK_RATE_MAX (60 * 25.4)
#define Z_SEEK_RATE_MAX (17 * 25.4)	// Z axis won't move as fast

#define M4_STEPS_PER_SEC 2000 		// motor characteristic
#define A_SEEK_RATE_MAX ((M4_STEPS_PER_SEC * M4_STEP_ANGLE * 60) / M4_TRAVEL_PER_REV)
#define B_SEEK_RATE_MAX A_SEEK_RATE_MAX
#define C_SEEK_RATE_MAX A_SEEK_RATE_MAX

// G1 max feed rate in mm/min
#define X_FEED_RATE_MAX (59 * 25.4)
#define Y_FEED_RATE_MAX (59 * 25.4)
#define Z_FEED_RATE_MAX (17 * 25.4)

#define A_FEED_RATE_MAX A_SEEK_RATE_MAX
#define B_FEED_RATE_MAX B_SEEK_RATE_MAX
#define C_FEED_RATE_MAX C_SEEK_RATE_MAX

#define X_JERK JERK_MAX
#define Y_JERK JERK_MAX
#define Z_JERK JERK_MAX
#define A_JERK JERK_MAX
#define B_JERK JERK_MAX
#define C_JERK JERK_MAX

#define X_CORNER_DELTA CORNER_DELTA
#define Y_CORNER_DELTA CORNER_DELTA
#define Z_CORNER_DELTA CORNER_DELTA
#define A_CORNER_DELTA CORNER_DELTA
#define B_CORNER_DELTA CORNER_DELTA
#define C_CORNER_DELTA CORNER_DELTA

#define A_RADIUS 10					// radius in mm
#define B_RADIUS 10					// (XYZ values are not defined)
#define C_RADIUS 10

#define X_LINEAR_JERK_MAX MAX_LINEAR_JERK
#define Y_LINEAR_JERK_MAX MAX_LINEAR_JERK
#define Z_LINEAR_JERK_MAX MAX_LINEAR_JERK
#define A_LINEAR_JERK_MAX MAX_LINEAR_JERK
#define B_LINEAR_JERK_MAX MAX_LINEAR_JERK
#define C_LINEAR_JERK_MAX MAX_LINEAR_JERK

#define X_TANGENT_JERK_VELOCITY 100
#define Y_TANGENT_JERK_VELOCITY 100
#define Z_TANGENT_JERK_VELOCITY 0
#define A_TANGENT_JERK_VELOCITY 0
#define B_TANGENT_JERK_VELOCITY 0
#define C_TANGENT_JERK_VELOCITY 0

#define X_TRAVEL_HARD_LIMIT 400		// travel between switches or crashes
#define Y_TRAVEL_HARD_LIMIT 175
#define Z_TRAVEL_HARD_LIMIT 75
#define A_TRAVEL_HARD_LIMIT -1		// -1 is no limit (typ for rotary axis)
#define B_TRAVEL_HARD_LIMIT -1
#define C_TRAVEL_HARD_LIMIT -1

#define SOFT_LIMIT_FACTOR (0.95)
#define X_TRAVEL_SOFT_LIMIT (X_TRAVEL_HARD_LIMIT * SOFT_LIMIT_FACTOR)
#define Y_TRAVEL_SOFT_LIMIT (Y_TRAVEL_HARD_LIMIT * SOFT_LIMIT_FACTOR)
#define Z_TRAVEL_SOFT_LIMIT (Z_TRAVEL_HARD_LIMIT * SOFT_LIMIT_FACTOR)
#define A_TRAVEL_SOFT_LIMIT -1
#define B_TRAVEL_SOFT_LIMIT -1
#define C_TRAVEL_SOFT_LIMIT -1

#define X_LIMIT_MODE TRUE			// 1=limit switches present and enabled
#define Y_LIMIT_MODE TRUE
#define Z_LIMIT_MODE TRUE
#define A_LIMIT_MODE TRUE
#define B_LIMIT_MODE TRUE
#define C_LIMIT_MODE TRUE

// homing settings
//#define HOMING_MODE TRUE			// global: set TRUE for power-on-homing
#define HOMING_MODE FALSE			// global: set TRUE for power-on-homing

#define X_HOMING_ENABLE 1			// 1=enabled for that axis
#define Y_HOMING_ENABLE 1
#define Z_HOMING_ENABLE 1
#define A_HOMING_ENABLE 1
#define B_HOMING_ENABLE 0
#define C_HOMING_ENABLE 0

#define X_HOMING_OFFSET -(X_TRAVEL_HARD_LIMIT/2) // offset to zero from axis minimum
#define Y_HOMING_OFFSET -(Y_TRAVEL_HARD_LIMIT/2)
#define Z_HOMING_OFFSET -(Z_TRAVEL_HARD_LIMIT/2)
#define A_HOMING_OFFSET -(A_TRAVEL_HARD_LIMIT/2)
#define B_HOMING_OFFSET -(A_TRAVEL_HARD_LIMIT/2)
#define C_HOMING_OFFSET -(A_TRAVEL_HARD_LIMIT/2)

#define X_HOMING_SEEK_RATE X_FEED_RATE_MAX
#define Y_HOMING_SEEK_RATE Y_FEED_RATE_MAX
#define Z_HOMING_SEEK_RATE Z_FEED_RATE_MAX
#define A_HOMING_SEEK_RATE A_FEED_RATE_MAX
#define B_HOMING_SEEK_RATE B_FEED_RATE_MAX
#define C_HOMING_SEEK_RATE C_FEED_RATE_MAX

#define X_HOMING_CLOSE_RATE 10		// mm/min
#define Y_HOMING_CLOSE_RATE 10
#define Z_HOMING_CLOSE_RATE 10
#define A_HOMING_CLOSE_RATE 360		// degrees per minute
#define B_HOMING_CLOSE_RATE 360
#define C_HOMING_CLOSE_RATE 360

#define X_HOMING_BACKOFF 5			// mm
#define Y_HOMING_BACKOFF 5
#define Z_HOMING_BACKOFF 5
#define A_HOMING_BACKOFF 5			// degrees
#define B_HOMING_BACKOFF 5
#define C_HOMING_BACKOFF 5