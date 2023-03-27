#include "Log.h"
#include <iostream>
#include <stdarg.h>
#ifdef __ANDROID__
#include <android/log.h>
class AndroidStreambuf : public std::streambuf
{
public:
	enum
	{
		bufsize=128
	}; // ... or some other suitable buffer size
	android_LogPriority logPriority;
	AndroidStreambuf(android_LogPriority p=ANDROID_LOG_INFO)
	{
		logPriority=p;
		this->setp(buffer, buffer+bufsize-1);
	}

private:
	int overflow(int c)
	{
		if(c==traits_type::eof())
		{
			*this->pptr()=traits_type::to_char_type(c);
			this->sbumpc();
		}
		return this->sync() ? traits_type::eof() : traits_type::not_eof(c);
	}
	std::string str;
	int sync()
	{
		int rc=0;
		if(this->pbase()!=this->pptr())
		{
			char writebuf[bufsize+1];
			memcpy(writebuf, this->pbase(), this->pptr()-this->pbase());
			writebuf[this->pptr()-this->pbase()]='\0';
			str+=writebuf;
			for(size_t pos=0;pos<str.length();pos++)
			{
				if(str[pos]=='\n')
				{
					str[pos]=0;
					rc=__android_log_write(logPriority, "TeleportVR", str.c_str())>0;
					if(str.length()>pos+1)
						str=str.substr(pos+1,str.length()-pos-1);
					else
						str.clear();
				}
			}
			this->setp(buffer, buffer+bufsize-1);
		}
		return rc;
	}

	char buffer[bufsize];
};

AndroidStreambuf androidCout;
AndroidStreambuf androidCerr(ANDROID_LOG_WARN);

void RedirectStdCoutCerr()
{
	auto *oldout = std::cout.rdbuf(&androidCout);
	auto *olderr = std::cerr.rdbuf(&androidCerr);
	if (oldout != &androidCout)
	{
		__android_log_write(ANDROID_LOG_DEBUG, "TeleportVR", "redirected cout");
	}
	std::cout<<"Testing cout redirect."<<std::endl;
	if (olderr != &androidCerr)
	{
		__android_log_write(ANDROID_LOG_DEBUG, "TeleportVR", "redirected cerr");
	}
	std::cerr<<"Testing cerr redirect."<<std::endl;
}
#endif

#include <sstream>
void ClientLog(const char* fileTag, int lineno, ClientLogPriority prio, const char* format_str, ...)
{
#ifdef __ANDROID__
	RedirectStdCoutCerr();
#endif
	int size = (int)strlen(format_str) + 100;
	static std::string str;
	va_list ap;
	int n = -1;
	while (n < 0 || n >= size)
	{
		str.resize(size);
		va_start(ap, format_str);
		//n = vsnprintf_s((char *)str.c_str(), size, size,format_str, ap);
		n = vsnprintf((char*)str.c_str(), size, format_str, ap);
		va_end(ap);
		if (n > -1 && n < size)
		{
			str.resize(n);
		}
		if (n > -1)
			size = n + 1;
		else
			size *= 2;
	}
	std::stringstream sstr;
	const char *typestr="info";
	if(prio==ClientLogPriority::WARNING)
		typestr="warning";
	if(prio==ClientLogPriority::LOG_ERROR)
		typestr="error";
	sstr << "Teleport: "<<fileTag << "(" << lineno << "): " << typestr << ": " << str.c_str();
	if(prio>=ClientLogPriority::WARNING)
		std::cerr<<sstr.str();
	else
		std::cout<<sstr.str();
}
