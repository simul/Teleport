// libavstream
// (c) Copyright 2018-2021 Simul Software Ltd

#pragma once

#include <libavstream/common.hpp>
#include <platform.hpp>
#include <mutex>

namespace avs
{
	/*!
	 * Timer for getting the elapsed between the start time and the current time.
	 */
	class AVSTREAM_API Timer
	{
	public:
		Timer();
		void Start();
		// Returns time in seconds.
		double GetElapsedTime();

	private:
		Timestamp m_startTimestamp;
		std::mutex m_mutex;
	};

	/*!
	 * Timer for getting the elapsed between the start time and the current time.
	 */
	class AVSTREAM_API TimerUtil
	{
	public:
		static void Start();
		// Returns time in seconds.
		static double GetElapsedTime();

	private:
		static Timer m_timer;
	};

} 