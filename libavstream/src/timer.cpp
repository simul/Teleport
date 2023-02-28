// libavstream
// (c) Copyright 2018-2022 Simul Software Ltd

#include <libavstream/timer.hpp>
#include <string.h>

namespace avs 
{
	Timer::Timer()
	{
		memset(&(m_startTimestamp), 0, sizeof(Timestamp));
	}
	
	void Timer::Start()
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_startTimestamp = Platform::getTimestamp();
	}

	double Timer::GetElapsedTimeS() const
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		return Platform::getTimeElapsedInSeconds(m_startTimestamp, Platform::getTimestamp());
	}

	Timer TimerUtil::m_timer;

	void TimerUtil::Start()
	{
		m_timer.Start();
	}

	double TimerUtil::GetElapsedTimeS()
	{
		return m_timer.GetElapsedTimeS();
	}
} 