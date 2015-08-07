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
 * @file tiltrotor.cpp
 *
 * @author Roman Bapst 		<bapstroman@gmail.com>
 *
*/

#include "tiltrotor.h"
#include "vtol_att_control_main.h"

#define ARSP_BLEND_START 8.0f	// airspeed at which we start blending mc/fw controls

Tiltrotor::Tiltrotor(VtolAttitudeControl *attc) :
VtolType(attc),
_rear_motors(ENABLED),
_tilt_control(0.0f),
_roll_weight_mc(1.0f),
_yaw_weight_mc(1.0f)
{
	_vtol_schedule.flight_mode = MC_MODE;
	_vtol_schedule.transition_start = 0;

	_params_handles_tiltrotor.front_trans_dur = param_find("VT_F_TRANS_DUR");
	_params_handles_tiltrotor.back_trans_dur = param_find("VT_B_TRANS_DUR");
	_params_handles_tiltrotor.tilt_mc = param_find("VT_TILT_MC");
	_params_handles_tiltrotor.tilt_transition = param_find("VT_TILT_TRANS");
	_params_handles_tiltrotor.tilt_fw = param_find("VT_TILT_FW");
	_params_handles_tiltrotor.airspeed_trans = param_find("VT_ARSP_TRANS");
	_params_handles_tiltrotor.elevons_mc_lock = param_find("VT_ELEV_MC_LOCK");
 }

Tiltrotor::~Tiltrotor()
{

}

int
Tiltrotor::parameters_update()
{
	float v;
	int l;

	/* vtol duration of a front transition */
	param_get(_params_handles_tiltrotor.front_trans_dur, &v);
	_params_tiltrotor.front_trans_dur = math::constrain(v,1.0f,5.0f);

	/* vtol duration of a back transition */
	param_get(_params_handles_tiltrotor.back_trans_dur, &v);
	_params_tiltrotor.back_trans_dur = math::constrain(v,0.0f,5.0f);

	/* vtol tilt mechanism position in mc mode */
	param_get(_params_handles_tiltrotor.tilt_mc, &v);
	_params_tiltrotor.tilt_mc = v;

	/* vtol tilt mechanism position in transition mode */
	param_get(_params_handles_tiltrotor.tilt_transition, &v);
	_params_tiltrotor.tilt_transition = v;

	/* vtol tilt mechanism position in fw mode */
	param_get(_params_handles_tiltrotor.tilt_fw, &v);
	_params_tiltrotor.tilt_fw = v;

	/* vtol airspeed at which it is ok to switch to fw mode */
	param_get(_params_handles_tiltrotor.airspeed_trans, &v);
	_params_tiltrotor.airspeed_trans = v;

	/* vtol lock elevons in multicopter */
	param_get(_params_handles_tiltrotor.elevons_mc_lock, &l);
	_params_tiltrotor.elevons_mc_lock = l;

	return OK;
}

void Tiltrotor::update_vtol_state()
{
 	parameters_update();

 	/* simple logic using a two way switch to perform transitions.
	 * after flipping the switch the vehicle will start tilting rotors, picking up
	 * forward speed. After the vehicle has picked up enough speed the rotors are tilted
	 * forward completely. For the backtransition the motors simply rotate back.
 	*/

	if (_manual_control_sp->aux1 < 0.0f) {
		// plane is in multicopter mode
		switch (_vtol_schedule.flight_mode) {
			case MC_MODE:
				_tilt_control = _params_tiltrotor.tilt_mc;
				break;
			case FW_MODE:
				_vtol_schedule.flight_mode 	= TRANSITION_BACK;
				_vtol_schedule.transition_start = hrt_absolute_time();
				break;
			case TRANSITION_FRONT_P1:
				// failsafe into multicopter mode
				_vtol_schedule.flight_mode = MC_MODE;
				break;
			case TRANSITION_FRONT_P2:
				// failsafe into multicopter mode
				_vtol_schedule.flight_mode = MC_MODE;
				break;
			case TRANSITION_BACK:
				if (_tilt_control <= _params_tiltrotor.tilt_mc) {
					_vtol_schedule.flight_mode = MC_MODE;
					_tilt_control = _params_tiltrotor.tilt_mc;
				}
				break;
		}

	} else {
		switch (_vtol_schedule.flight_mode) {
			case MC_MODE:
				// initialise a front transition
				_vtol_schedule.flight_mode 	= TRANSITION_FRONT_P1;
				_vtol_schedule.transition_start = hrt_absolute_time();
				break;
			case FW_MODE:
				_tilt_control = _params_tiltrotor.tilt_fw;
				break;
			case TRANSITION_FRONT_P1:
				// check if we have reached airspeed to switch to fw mode
				if (_airspeed->true_airspeed_m_s >= _params_tiltrotor.airspeed_trans) {
					_vtol_schedule.flight_mode = TRANSITION_FRONT_P2;
					_vtol_schedule.transition_start = hrt_absolute_time();
				}
				break;
			case TRANSITION_FRONT_P2:
				// if the rotors have been tilted completely we switch to fw mode
				if (_tilt_control >= _params_tiltrotor.tilt_fw) {
					_vtol_schedule.flight_mode = FW_MODE;
					_tilt_control = _params_tiltrotor.tilt_fw;
				}
				break;
			case TRANSITION_BACK:
				break;
		}

	}

	// map tiltrotor specific control phases to simple control modes
	switch(_vtol_schedule.flight_mode) {
		case MC_MODE:
			_vtol_mode = ROTARY_WING;
			break;
		case FW_MODE:
			_vtol_mode = FIXED_WING;
			break;
		case TRANSITION_FRONT_P1:
		case TRANSITION_FRONT_P2:
		case TRANSITION_BACK:
			_vtol_mode = TRANSITION;
			break;
	}
}

void Tiltrotor::update_mc_state()
{
	// adjust max pwm for rear motors to spin up
	if (_rear_motors != ENABLED) {
		set_rear_motor_state(ENABLED);
	}

	// set idle speed for rotary wing mode
	if (!flag_idle_mc) {
		set_idle_mc();
		flag_idle_mc = true;
	}
}

 void Tiltrotor::update_fw_state()
{
	/* in fw mode we need the rear motors to stop spinning, in backtransition
	 * mode we let them spin in idle
	 */
	if (_rear_motors != DISABLED) {
		set_rear_motor_state(DISABLED);
	}

	// adjust idle for fixed wing flight
	if (flag_idle_mc) {
		set_idle_fw();
		flag_idle_mc = false;
	}
 }

void Tiltrotor::update_transition_state()
{
	if (_vtol_schedule.flight_mode == TRANSITION_FRONT_P1) {
		// for the first part of the transition the rear rotors are enabled
		if (_rear_motors != ENABLED) {
			set_rear_motor_state(ENABLED);
		}
		// tilt rotors forward up to certain angle
		if (_tilt_control <= _params_tiltrotor.tilt_transition) {
			_tilt_control = _params_tiltrotor.tilt_mc +  fabsf(_params_tiltrotor.tilt_transition - _params_tiltrotor.tilt_mc)*(float)hrt_elapsed_time(&_vtol_schedule.transition_start)/(_params_tiltrotor.front_trans_dur*1000000.0f);
		}

		// do blending of mc and fw controls
		if (_airspeed->true_airspeed_m_s >= ARSP_BLEND_START) {
			_roll_weight_mc = 1.0f - (_airspeed->true_airspeed_m_s - ARSP_BLEND_START) / (_params_tiltrotor.airspeed_trans - ARSP_BLEND_START);
		} else {
			// at low speeds give full weight to mc
			_roll_weight_mc = 1.0f;
		}

		// disable mc yaw control once the plane has picked up speed
		_yaw_weight_mc = 1.0f;
		if (_airspeed->true_airspeed_m_s > 5.0f) {
			_yaw_weight_mc = 0.0f;
		}

	} else if (_vtol_schedule.flight_mode == TRANSITION_FRONT_P2) {
		_tilt_control = _params_tiltrotor.tilt_transition +  fabsf(_params_tiltrotor.tilt_fw - _params_tiltrotor.tilt_transition)*(float)hrt_elapsed_time(&_vtol_schedule.transition_start)/(0.5f*1000000.0f);
		_roll_weight_mc = 0.0f;
	} else if (_vtol_schedule.flight_mode == TRANSITION_BACK) {
		if (_rear_motors != IDLE) {
			set_rear_motor_state(IDLE);
		}
		// tilt rotors back
		if (_tilt_control > _params_tiltrotor.tilt_mc) {
			_tilt_control = _params_tiltrotor.tilt_fw -  fabsf(_params_tiltrotor.tilt_fw - _params_tiltrotor.tilt_mc)*(float)hrt_elapsed_time(&_vtol_schedule.transition_start)/(_params_tiltrotor.back_trans_dur*1000000.0f);
		}

		_roll_weight_mc = 0.0f;

	}

	_roll_weight_mc = math::constrain(_roll_weight_mc, 0.0f, 1.0f);
	_yaw_weight_mc = math::constrain(_yaw_weight_mc, 0.0f, 1.0f);
}

void Tiltrotor::update_external_state()
{

}

/**
* Write data to actuator output topic.
*/
void Tiltrotor::fill_actuator_outputs()
{
	switch(_vtol_schedule.flight_mode) {
		case MC_MODE:
			_actuators_out_0->control[0] = _actuators_mc_in->control[0];
			_actuators_out_0->control[1] = _actuators_mc_in->control[1];
			_actuators_out_0->control[2] = _actuators_mc_in->control[2];
			_actuators_out_0->control[3] = _actuators_mc_in->control[3];
			_actuators_out_1->control[0] = 0;
			_actuators_out_1->control[1] = 0;
			_actuators_out_1->control[4] = _tilt_control;
			break;
		case FW_MODE:
			_actuators_out_0->control[0] = 0;
			_actuators_out_0->control[1] = 0;
			_actuators_out_0->control[2] = 0;
			_actuators_out_0->control[3] = _actuators_fw_in->control[3];

			_actuators_out_1->control[0] = -_actuators_fw_in->control[0];	// roll elevon
			_actuators_out_1->control[1] = _actuators_fw_in->control[1] + _params->fw_pitch_trim;	// pitch elevon
			_actuators_out_1->control[2] = _actuators_fw_in->control[2];	// yaw
			_actuators_out_1->control[3] = _actuators_fw_in->control[3];	// throttle
			_actuators_out_1->control[4] = _tilt_control;
			break;
		case TRANSITION_FRONT_P1:
			_actuators_out_0->control[0] = _actuators_mc_in->control[0] * _roll_weight_mc;
			_actuators_out_0->control[1] = _actuators_mc_in->control[1];
			_actuators_out_0->control[2] = _actuators_mc_in->control[2] * _yaw_weight_mc;
			_actuators_out_0->control[3] = _actuators_mc_in->control[3];
			_actuators_out_1->control[0] = -_actuators_fw_in->control[0] * (1.0f - _roll_weight_mc);	//roll elevon
			_actuators_out_1->control[1] = (_actuators_fw_in->control[1] + _params->fw_pitch_trim);		//pitch elevon
			_actuators_out_1->control[4] = _tilt_control;	// for tilt-rotor control
			break;
		case TRANSITION_FRONT_P2:
			_actuators_out_0->control[0] = 0;
			_actuators_out_0->control[1] = 0;
			_actuators_out_0->control[2] = 0;
			_actuators_out_0->control[3] = _actuators_fw_in->control[3];

			_actuators_out_1->control[0] = -_actuators_fw_in->control[0];	// roll elevon
			_actuators_out_1->control[1] = _actuators_fw_in->control[1] + _params->fw_pitch_trim;	// pitch elevon
			_actuators_out_1->control[2] = _actuators_fw_in->control[2];	// yaw
			_actuators_out_1->control[3] = _actuators_fw_in->control[3];	// throttle
			_actuators_out_1->control[4] = _tilt_control;					// tilt

			break;
		case TRANSITION_BACK:
			_actuators_out_0->control[0] = _actuators_mc_in->control[0] * _roll_weight_mc;
			_actuators_out_0->control[1] = _actuators_mc_in->control[1];
			_actuators_out_0->control[2] = _actuators_mc_in->control[2] * _yaw_weight_mc;
			_actuators_out_0->control[3] = _actuators_fw_in->control[3];

			_actuators_out_1->control[0] = -_actuators_fw_in->control[0];	// roll elevon
			_actuators_out_1->control[1] = _actuators_fw_in->control[1] + _params->fw_pitch_trim;	// pitch elevon
			_actuators_out_1->control[2] = _actuators_fw_in->control[2];	// yaw
			_actuators_out_1->control[3] = _actuators_fw_in->control[3];	// throttle
			_actuators_out_1->control[4] = _tilt_control;					// tilt
		}

}


/**
* Set state of rear motors.
*/

void Tiltrotor::set_rear_motor_state(rear_motor_state state) {
	int pwm_value;

	// map desired rear rotor state to max allowed pwm signal
	switch (state) {
		case ENABLED:
			pwm_value = 2000;
			_rear_motors = ENABLED;
		case DISABLED:
			pwm_value = 950;
			_rear_motors = DISABLED;
		case IDLE:
			pwm_value = 1250;
			_rear_motors = IDLE;
	}

	int ret;
	unsigned servo_count;
	char *dev = PWM_OUTPUT0_DEVICE_PATH;
	int fd = open(dev, 0);

	if (fd < 0) {err(1, "can't open %s", dev);}

	ret = ioctl(fd, PWM_SERVO_GET_COUNT, (unsigned long)&servo_count);
	struct pwm_output_values pwm_values;
	memset(&pwm_values, 0, sizeof(pwm_values));

	for (int i = 0; i < _params->vtol_motor_count; i++) {
		if (i == 2 || i == 3) {
			pwm_values.values[i] = pwm_value;
		} else {
			pwm_values.values[i] = 2000;
		}
		pwm_values.channel_count = _params->vtol_motor_count;
	}

	ret = ioctl(fd, PWM_SERVO_SET_MAX_PWM, (long unsigned int)&pwm_values);

	if (ret != OK) {errx(ret, "failed setting max values");}

	close(fd);

}