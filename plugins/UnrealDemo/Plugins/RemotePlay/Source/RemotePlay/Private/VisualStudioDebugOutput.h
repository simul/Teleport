#pragma once
#ifdef _MSC_VER
#include <windows.h>
#include <direct.h>
#else
#define OutputDebugString
#define _getcwd
#endif
#ifndef _MAX_PATH
#define _MAX_PATH 500
#endif
#include <fstream>
#include <iostream>
#include <sstream>
#include <time.h>
#include <cerrno>

#ifndef _MSC_VER
#define __stdcall
#endif
typedef void (__stdcall *DebugOutputCallback)(const char *);

class vsBufferedStringStreamBuf : public std::streambuf
{
public:
	vsBufferedStringStreamBuf(int bufferSize) 
	{
		if (bufferSize)
		{
			char *ptr = new char[bufferSize];
			setp(ptr, ptr + bufferSize);
		}
		else
			setp(0, 0);
	}
	virtual ~vsBufferedStringStreamBuf() 
	{
		//sync();
		delete[] pbase();
	}
	virtual void writeString(const std::string &str) = 0;
private:
	int	overflow(int c)
	{
		sync();

		if (c != EOF)
		{
			if (pbase() == epptr())
			{
				std::string temp;
				temp += char(c);
				writeString(temp);
			}
			else
				sputc((char)c);
		}

		return 0;
	}

	int	sync()
	{
		if (pbase() != pptr())
		{
			int len = int(pptr() - pbase());
			std::string temp(pbase(), len);
			writeString(temp);
			setp(pbase(), epptr());
		}
		return 0;
	}
};

class VisualStudioDebugOutput : public vsBufferedStringStreamBuf
{
public:
    VisualStudioDebugOutput(bool send_to_output_window=true,size_t bufsize=(size_t)16
							,DebugOutputCallback c=NULL)
		:vsBufferedStringStreamBuf((int)bufsize)
		,old_cout_buffer(NULL)
		,old_cerr_buffer(NULL)
		,callback(c)
	{
		to_output_window=send_to_output_window;
		old_cout_buffer=std::cout.rdbuf(this);
		old_cerr_buffer=std::cerr.rdbuf(this);
	}
	virtual ~VisualStudioDebugOutput()
	{
		std::cout.rdbuf(old_cout_buffer);
		std::cerr.rdbuf(old_cerr_buffer);
	}
	void setCallback(DebugOutputCallback c)
	{
		callback=c;
	}
    virtual void writeString(const std::string &str)
    {
		if(callback)
		{
			callback(str.c_str());
		}
		if(to_output_window)
		{
#ifdef UNICODE
			std::wstring wstr(str.length(), L' '); // Make room for characters
			// Copy string to wstring.
			std::copy(str.begin(), str.end(), wstr.begin());
			OutputDebugString(wstr.c_str());
#else
	        OutputDebugString(str.c_str());
#endif
		}
    }
protected:
	bool to_output_window;
	std::streambuf *old_cout_buffer;
	std::streambuf *old_cerr_buffer;
	DebugOutputCallback callback;
};