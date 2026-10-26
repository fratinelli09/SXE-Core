#pragma once
#include <atomic>
#include <chrono>
#include <exception>
#include <future>
#include <string>

#include "Logger.h"

class ThreadBase
{
public:
	ThreadBase():
	_bAlive(false),
	_bInitFailed(false)
	{
	}
	virtual ~ThreadBase(){}

	virtual void run() = 0;

	void start()
	{
		if(_bInitFailed)
		{
			_log().Error("Initialization failed, thread not started (see the previous errors)");
			return;
		}

		_bAlive = true;
		_thread = std::async(std::launch::async, &ThreadBase::_loopExec, this);
	}

	void run_here()
	{
		if(_bInitFailed)
		{
			_log().Error("Initialization failed, loop not started (see the previous errors)");
			return;
		}

		_bAlive = true;
		_loopExec();
	}

	void stop()
	{
		_bAlive = false;
	}

	void wait()
	{
		if(_thread.valid())
			_thread.wait();
	}

	bool is_done() const
	{
		using namespace std::chrono_literals;
		if(!_thread.valid()) return true;
		return _thread.wait_for(0ms) == std::future_status::ready;
	}

	bool is_alive() const { return _bAlive; }

protected:
	virtual Logger& _log() { return Logger::Get(); }

	void _loopExec()
	{
		try
		{
			while(_bAlive)
			{
				run();
			}
		}
		catch(const std::exception& e)
		{
			_log().Error(std::string("Exception in the main loop, thread stopped: ") + e.what());
		}
		catch(...)
		{
			_log().Error("Unknown exception in the main loop, thread stopped");
		}
		_bAlive = false;
	}

protected:
	std::future<void> _thread;
	std::atomic<bool> _bAlive;
	bool _bInitFailed;
};
