#pragma once

#include <array>
#include <complex>
#include <cstddef>
#include <cstdint>

class UserU220;

class ImitatorU220 {
public:
	explicit ImitatorU220(UserU220 & fpga);

	struct TargetParams {
		double az;
		double um;
		double distance;
		int16_t velocity;
		double attenuation;
	};

	// Four targets, both radios.
	// Angles in degrees, distances in metres.
	void set_params(const std::array<double, 4> & target_az,
	                const std::array<double, 4> & target_um,
	                const std::array<double, 4> & target_dist,
	                const std::array<int16_t, 4> & target_vel,
	                const std::array<double, 4> & target_osl);

	void set_params(const std::array<TargetParams, 4> & targets);

	void set_distance(const std::array<double, 4> & target_dist);
	void set_velocity(const std::array<int16_t, 4> & target_vel);
	void set_az(const std::array<double, 4> & target_az);

	void set_amplitude(const std::array<double, 4> & target_um,
	                   const std::array<double, 4> & target_dist,
	                   const std::array<double, 4> & target_osl);


	// Angle in degrees, rotation speed in degrees per second.
	// Zero speed holds the specified angle.
	void set_opu(double angle_degrees, double speed_degps = 0.0);

private:
	static constexpr double LIGHT_SPEED = 299792458.0;
	static constexpr double LAMBDA      = 0.032480222968581;
	static constexpr double FREQ        = 9.23e9;
	static constexpr double PT          = 80.0;
	static constexpr double GAIN_IF     = 3162.2776601683795;
	static constexpr double GAIN_SDR    = 10000000.0;
	static constexpr double U_REF       = 1.3;
	static constexpr double G_MAX       = 132.02;
	static constexpr int N_CHANNELS     = 8;
	static constexpr double ANT_DIST    = LAMBDA / 2.0;
	static const double ENERGY_CONST;
	static constexpr double CLOCK_RATE = 40e6;

	UserU220 & fpga_;

	static double attenuation_to_rcs(double attenuation_db);
	static uint32_t velocity_to_phase(int16_t velocity_mps);
	static std::complex<int16_t> calc_amplitude(uint16_t distance_m, size_t channel, uint16_t angle, double attenuation_db);
};
