// libavstream
// (c) Copyright 2018-2022 Simul Software Ltd

#pragma once

#include <string>
#include <sstream>
#include "context_p.hpp"
#include <string.h>
#include <iostream>

#define __FILENAME__ (strrchr(__FILE__, '/') ? strrchr(__FILE__, '/') + 1 : __FILE__)
#define AVSLOG(Severity) std::cerr<<__FILENAME__<<"("<<__LINE__<<"): "<<#Severity<<": "
#define AVSLOGONCE(Severity) static bool done=false;bool do_now=!done;avs::Logger(((done=true)&&do_now)?avs::LogSeverity::Severity:avs::LogSeverity::Never)
#define AVSLOG_NOSPAM(Severity) static uint16_t ctr=1;ctr--;bool do_now=(ctr==0);avs::Logger(do_now?avs::LogSeverity::Severity:avs::LogSeverity::Never)
namespace avs
{

	class Logger
	{
	public:
		explicit Logger(LogSeverity severity)
			: m_severity(severity)
		{}

		template<typename T>
		Logger& operator<<(T arg)
		{
			if (m_severity == LogSeverity::Never)
				return *this;
			std::ostringstream stream;
			stream << arg;
			Context::instance()->d().log(m_severity, stream.str().c_str());
			return *this;
		}

	private:
		const LogSeverity m_severity;
	};

} // avs
