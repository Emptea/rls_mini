#include "imitator_u220.h"

#include "../rls_fpga/rls.hpp"
#include "rlso_const.hpp"
#include "u220.hpp"
#include "u220_group.h"
#include "user_u220.h"

#include <cmath>
#include <stdexcept>

const double ImitatorU220::ENERGY_CONST =
	std::sqrt(PT * (LAMBDA * LAMBDA) * GAIN_IF * GAIN_SDR * 50.0 / (4.0 * std::pow(M_PI, 3)) * G_MAX * (1 << 12) / U_REF);

ImitatorU220::ImitatorU220(U220Group & group): group_(group) {}

double ImitatorU220::attenuation_to_rcs(double attenuation_db) {
	const double rcs = std::pow(10.0, -attenuation_db / 10.0);
	return rcs;
}

uint32_t ImitatorU220::velocity_to_phase(int16_t velocity_mps) {
	constexpr double period_s = static_cast<double>(rls::N_SAMPS_IN_PACK) / 5000000.0;
	const double phase_turns  = -2.0 * velocity_mps * FREQ * period_s / LIGHT_SPEED;
	const int32_t phase_units = std::lround(phase_turns * 4294967296.0);
	return static_cast<uint32_t>(phase_units);
}

std::complex<int16_t> ImitatorU220::calc_amplitude(double distance_m, size_t channel, double um_deg, double attenuation_db) {
	const double um        = um_deg * deg2rad;
	const double rcs       = attenuation_to_rcs(attenuation_db);
	distance_m             = 1e3;
	const double magnitude = ENERGY_CONST * std::sqrt(rcs) * std::cos(um) / (distance_m * distance_m);
	const double phase     = (2.0 * M_PI / LAMBDA) * ((-N_CHANNELS - 1) / 2.0 + static_cast<double>(channel)) * ANT_DIST * std::sin(um);
	const std::complex<double> gain = magnitude * std::exp(std::complex<double>(0.0, phase));

	const auto to_q14               = [](double component) -> int16_t {
        const double scaled = component * 16384.0;

        if (scaled <= -32768.0) return -32768;
        if (scaled >= 32767.0) return 32767;

        return static_cast<int16_t>(std::lround(scaled));
	};

	return {to_q14(gain.real()), to_q14(gain.imag())};
}

void ImitatorU220::set_distance(const std::array<double, 4> & target_dist) {
	std::array<uint32_t, 4> delays{};
	for (size_t k = 0; k < 4; ++k) {
		const double bias = (2.0 * target_dist[k] / LIGHT_SPEED) * CLOCK_RATE;
		uint32_t delay    = static_cast<uint32_t>(std::lround(bias));
		delays[k]         = delay;
	}

	for (const auto board_index: group_.active_boards()) {
		auto * fpga = group_.fpga(board_index);

		for (size_t radio = 0; radio < 2; ++radio)
			fpga->set_rx_playback_delay(radio, delays);
	}
}

void ImitatorU220::set_velocity(const std::array<int16_t, 4> & target_vel) {
	std::array<uint32_t, 4> phases{};

	for (size_t k = 0; k < 4; ++k) {
		phases[k] = velocity_to_phase(target_vel[k]);
	}

	for (const auto board_index: group_.active_boards()) {
		auto * fpga = group_.fpga(board_index);

		for (size_t radio = 0; radio < 2; ++radio)
			fpga->set_rx_playback_phase(radio, phases);
	}
}

void ImitatorU220::set_az(const std::array<double, 4> & target_az) {
	std::array<uint32_t, 4> offsets{};

	for (size_t k = 0; k < 4; ++k) {
		const double angle   = std::fmod(target_az[k], 360.0);
		const uint16_t fixed = static_cast<uint16_t>(std::llround(angle * (65536.0 / 360.0)));
		offsets[k]           = fixed >> 4;
	}

	for (const auto board_index: group_.active_boards()) {
		auto * fpga = group_.fpga(board_index);

		for (size_t radio = 0; radio < 2; ++radio)
			fpga->set_rx_playback_dn_offset(radio, offsets);
	}
}

void ImitatorU220::set_amplitude(const std::array<double, 4> & target_um,
                                 const std::array<double, 4> & target_dist,
                                 const std::array<double, 4> & target_osl) {
	for (const auto board_index: group_.active_boards()) {
		for (size_t radio = 0; radio < 2; ++radio) {
			std::array<uint32_t, 4> amplitudes{};

			const size_t channel = 2 * board_index + radio + 1;

			for (size_t k = 0; k < 4; ++k) {
				const auto amplitude = calc_amplitude(target_dist[k], channel, target_um[k], target_osl[k]);

				amplitudes[k] =
					static_cast<uint16_t>(amplitude.real()) | (static_cast<uint32_t>(static_cast<uint16_t>(amplitude.imag())) << 16);
			}

			group_.fpga(board_index)->set_rx_playback_amplitude(radio, amplitudes);
		}
	}
}

void ImitatorU220::set_opu(double angle_degrees, double speed_degps) {
	if (!std::isfinite(angle_degrees) || !std::isfinite(speed_degps)) {
		piCout << "Error: invalid OPU parameters";
		return;
	}

	double wrapped = std::fmod(angle_degrees, 360.0);
	if (wrapped < 0.0) wrapped += 360.0;

	const uint16_t start              = static_cast<uint16_t>(wrapped * (4096.0 / 360.0));

	constexpr double capture_period_s = static_cast<double>(rls::N_SAMPS_IN_PACK) / 5000000.0;

	const double rounded_step         = std::round(speed_degps * capture_period_s * (1048576.0 / 360.0));

	if (!std::isfinite(rounded_step) || rounded_step < -524288.0 || rounded_step > 524287.0) {
		piCout << "Error: OPU rotation speed exceeds signed 20-bit step range";
		return;
	}

	opu_speed_degps_ = speed_degps;

	for (const auto board_index: group_.active_boards()) {
		group_.fpga(board_index)->set_rx_playback_dn_counter(start, static_cast<int32_t>(rounded_step));
	}
}

float ImitatorU220::get_opu_az() {
	auto status = group_.fpga(0)->get_rx_playback_status(0, 0);
	// opu_running_ = status.playing;
	auto az     = status.getDnDeg();
	return az;
}

float ImitatorU220::get_opu_azago(double ago_sec) {
	auto status = group_.fpga(0)->get_rx_playback_status(0, 0);
	// opu_running_ = status.playing;
	auto cur_az = status.getDnDeg();
	auto az_ago = cur_az - opu_speed_degps_ * ago_sec;
	az_ago      = std::fmod(az_ago, 360.0);
	if (az_ago < 0.0) az_ago += 360.0;

	return az_ago;
}

void ImitatorU220::set_params(const std::array<double, 4> & target_az,
                              const std::array<double, 4> & target_um,
                              const std::array<double, 4> & target_dist,
                              const std::array<int16_t, 4> & target_vel,
                              const std::array<double, 4> & target_osl) {
	for (size_t k = 0; k < 4; ++k) {
		const double dist = target_dist[k];
		const double rcs  = std::pow(10.0, -target_osl[k] / 10.0);

		if (!std::isfinite(target_az[k]) || !std::isfinite(target_um[k]) || !std::isfinite(target_osl[k]) || !std::isfinite(rcs) ||
		    !std::isfinite(dist) || dist < 0.5 || dist >= 65535.5) {
			piCout << "Error: invalid imitator target parameters, target=" << k << " az=" << target_az[k] << " um=" << target_um[k]
				   << " osl=" << target_osl[k] << " rcs=" << rcs << " dist=" << dist;

			if (!std::isfinite(target_az[k])) piCout << "Invalid az";
			if (!std::isfinite(target_um[k])) piCout << "Invalid um";
			if (!std::isfinite(target_osl[k])) piCout << "Invalid osl";
			if (!std::isfinite(rcs)) piCout << "Invalid rcs";
			if (!std::isfinite(dist) || dist < 0.5 || dist >= 65535.5) piCout << "Invalid distance";

			return;
		}
	}

	set_distance(target_dist);
	set_velocity(target_vel);
	set_az(target_az);
	set_amplitude(target_um, target_dist, target_osl);
}

void ImitatorU220::set_params(const std::array<TargetParams, 4> & targets) {
	std::array<double, 4> az{};
	std::array<double, 4> um{};
	std::array<double, 4> distances{};
	std::array<int16_t, 4> velocities{};
	std::array<double, 4> attenuations{};

	for (size_t k = 0; k < targets.size(); ++k) {
		az[k]           = targets[k].az;
		um[k]           = targets[k].um;
		distances[k]    = targets[k].distance;
		velocities[k]   = targets[k].velocity;
		attenuations[k] = targets[k].attenuation;
	}

	set_params(az, um, distances, velocities, attenuations);
}

void ImitatorU220::apply(double delay_seconds) {
	// if (!std::isfinite(delay_seconds) || delay_seconds < 0.0)
	// 	throw std::invalid_argument("Imitator apply delay must be finite and non-negative");

	// const auto & active_boards = group_.active_boards();

	// if (active_boards.isEmpty()) return;

	// auto first_usrp       = group_.board(active_boards[0])->get_usrp();
	// const auto apply_time = first_usrp->get_time_now() + uhd::time_spec_t(delay_seconds);

	// for (const auto board_index: active_boards) {
	// 	auto usrp = group_.board(board_index)->get_usrp();

	// 	usrp->set_command_time(apply_time);

	// 	try {
	// 		group_.fpga(board_index)->apply_rx_playback();
	// 	} catch (...) {
	// 		usrp->clear_command_time();
	// 		throw;
	// 	}

	// 	usrp->clear_command_time();
	// }

	for (const auto board_index: group_.active_boards()) {
		group_.fpga(board_index)->apply_rx_playback();
	}
}

void ImitatorU220::set_targets_enabled(uint8_t mask) {
	mask &= 0x0F; // 4 цели

	for (const auto board_index: group_.active_boards()) {
		auto * fpga = group_.fpga(board_index);

		for (size_t radio = 0; radio < 2; ++radio) {
			fpga->set_rx_playback_enable(radio, mask);
		}
	}
}
