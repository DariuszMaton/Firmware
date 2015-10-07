/****************************************************************************
 *
 *   Copyright (c) 2015 PX4 Development Team. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name PX4 nor the names of its contributors may be
 *    used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 ****************************************************************************/
/**
 * @file RunwayTakeoff.cpp
 * Runway takeoff handling for fixed-wing UAVs with steerable wheels.
 *
 * @author Roman Bapst <roman@px4.io>
 * @author Andreas Antener <andreas@uaventure.com>
 */

#include <stdbool.h>
#include <stdint.h>
#include <math.h>

#include "RunwayTakeoff.h"
#include <controllib/blocks.hpp>
#include <controllib/block/BlockParam.hpp>
#include <mavlink/mavlink_log.h>
#include <mathlib/mathlib.h>

namespace runwaytakeoff
{

RunwayTakeoff::RunwayTakeoff() :
	SuperBlock(NULL, "RWTO"),
	_state(),
	_initialized(false),
	_initialized_time(0),
	_init_yaw(0),
	_climbout(false),
	_runway_takeoff_enabled(this, "TKOFF"),
	_heading_mode(this, "HDG"),
	_nav_alt(this, "NAV_ALT"),
	_takeoff_throttle(this, "MAX_THR"),
	_runway_pitch_sp(this, "PSP"),
	_max_takeoff_pitch(this, "MAX_PITCH"),
	_max_takeoff_roll(this, "MAX_ROLL"),
	_min_airspeed_scaling(this, "AIRSPD_SCL"),
	_airspeed_min(this, "FW_AIRSPD_MIN", false),
	_climbout_diff(this, "FW_CLMBOUT_DIFF", false)
{

	updateParams();
}

RunwayTakeoff::~RunwayTakeoff()
{

}

void RunwayTakeoff::init(float yaw)
{
	_init_yaw = yaw;
	_initialized = true;
	_state = RunwayTakeoffState::THROTTLE_RAMP;
	_initialized_time = hrt_absolute_time();
	_climbout = true;
}

void RunwayTakeoff::update(float airspeed, float alt_agl, int mavlink_fd)
{

	switch (_state) {
	case RunwayTakeoffState::THROTTLE_RAMP:
		if (hrt_elapsed_time(&_initialized_time) > 1e6) {
			_state = RunwayTakeoffState::CLAMPED_TO_RUNWAY;
		}

		break;

	case RunwayTakeoffState::CLAMPED_TO_RUNWAY:
		if (airspeed > _airspeed_min.get() * _min_airspeed_scaling.get()) {
			_state = RunwayTakeoffState::TAKEOFF;
			mavlink_log_info(mavlink_fd, "#Takeoff airspeed reached");
		}

		break;

	case RunwayTakeoffState::TAKEOFF:
		if (alt_agl > _nav_alt.get()) {
			_state = RunwayTakeoffState::CLIMBOUT;
			mavlink_log_info(mavlink_fd, "#Climbout");
		}

		break;

	case RunwayTakeoffState::CLIMBOUT:
		if (alt_agl > _climbout_diff.get()) {
			_climbout = false;
			_state = RunwayTakeoffState::FLY;
			mavlink_log_info(mavlink_fd, "#Navigating to waypoint");
		}

		break;

	default:
		return;
	}
}

bool RunwayTakeoff::controlYaw()
{
	// keep controlling yaw directly until we start navigation
	return _state < RunwayTakeoffState::CLIMBOUT;
}

float RunwayTakeoff::getPitch(float tecsPitch)
{
	if (_state <= RunwayTakeoffState::CLAMPED_TO_RUNWAY) {
		return math::radians(_runway_pitch_sp.get());
	}

	return tecsPitch;
}

float RunwayTakeoff::getRoll(float navigatorRoll)
{
	// until we have enough ground clearance, set roll to 0
	if (_state < RunwayTakeoffState::CLIMBOUT) {
		return 0.0f;
	}

	// allow some roll during climbout if waypoint heading is targeted
	else if (_state < RunwayTakeoffState::FLY) {
		if (_heading_mode.get() == 0) {
			// otherwise stay at 0 roll
			return 0.0f;

		} else if (_heading_mode.get() == 1) {
			return math::constrain(navigatorRoll,
					       math::radians(-_max_takeoff_roll.get()),
					       math::radians(_max_takeoff_roll).get());
		}
	}

	return navigatorRoll;
}

float RunwayTakeoff::getYaw(float navigatorYaw)
{
	if (_state < RunwayTakeoffState::FLY) {
		if (_heading_mode.get() == 0) {
			// fix heading in the direction the airframe points
			return _init_yaw;

		} else if (_heading_mode.get() == 1) {
			// or head into the direction of the takeoff waypoint
			// XXX this needs a check if the deviation from actual heading is too big (else we do a full throttle wheel turn on the ground)
			return navigatorYaw;
		}
	}

	return navigatorYaw;
}

float RunwayTakeoff::getThrottle(float tecsThrottle)
{
	switch (_state) {
	case RunwayTakeoffState::THROTTLE_RAMP: {
			float throttle = hrt_elapsed_time(&_initialized_time) / (float)2000000 *
					 _takeoff_throttle.get();
			return throttle < _takeoff_throttle.get() ?
			       throttle :
			       _takeoff_throttle.get();
		}

	case RunwayTakeoffState::CLAMPED_TO_RUNWAY:
		return _takeoff_throttle.get();

	default:
		return tecsThrottle;
	}
}

bool RunwayTakeoff::resetIntegrators()
{
	// reset integrators if we're still on runway
	return _state < RunwayTakeoffState::TAKEOFF;
}

float RunwayTakeoff::getMinPitch(float sp_min, float climbout_min, float min)
{
	if (_climbout) {
		return math::max(sp_min, climbout_min);
	}

	else {
		return min;
	}
}

float RunwayTakeoff::getMaxPitch(float max)
{
	if (_climbout && _max_takeoff_pitch.get() > 0.1f) {
		return _max_takeoff_pitch.get();
	}

	else {
		return max;
	}
}

void RunwayTakeoff::reset()
{
	_initialized = false;
	_state = RunwayTakeoffState::THROTTLE_RAMP;
}

}