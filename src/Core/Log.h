#pragma once

#include <Windows.h>

#include <string>

// Minimal thread-safe logger.
//
// Every message goes to the debugger via OutputDebugStringW (visible in
// DebugView / the VS output window). When DebugLog=yes, messages are also
// appended to kite.log next to the game executable.

namespace Kite
{
	// Verbosity, from kite.ini [Kite] LogLevel. An unknown value falls back to
	// Info, so a typo can never silence the log by accident.
	enum class LogLevel
	{
		Off = 0,
		Error,
		Warn,
		Info,
		Debug,
	};

	namespace Log
	{
		// Point the logger at a configuration. Called once during startup.
		void Init(LogLevel level, bool toFile);

		// Flush and close the log file, if one is open.
		void Shutdown();

		bool IsEnabled(LogLevel level);

		void Write(LogLevel level, const wchar_t* format, ...);
		void Write(LogLevel level, const char* format, ...);

		// "off" / "error" / "warn" / "info" / "debug", for the startup log line.
		const char* LevelName(LogLevel level);

		// Parses one of the names above (case-insensitive); anything else yields
		// Info. Used for kite.ini's LogLevel, so it takes the raw INI text.
		LogLevel ParseLevel(const wchar_t* value);
	}

	// Convenience wrappers. The variadic template stops after the first
	// argument, so the format string type selects the wide or narrow overload
	// of Log::Write without any ambiguity.
	template <typename... Args>
	inline void LogError(const wchar_t* format, Args... args)
	{
		Log::Write(LogLevel::Error, format, args...);
	}

	template <typename... Args>
	inline void LogError(const char* format, Args... args)
	{
		Log::Write(LogLevel::Error, format, args...);
	}

	template <typename... Args>
	inline void LogWarn(const wchar_t* format, Args... args)
	{
		Log::Write(LogLevel::Warn, format, args...);
	}

	template <typename... Args>
	inline void LogWarn(const char* format, Args... args)
	{
		Log::Write(LogLevel::Warn, format, args...);
	}

	template <typename... Args>
	inline void LogInfo(const wchar_t* format, Args... args)
	{
		Log::Write(LogLevel::Info, format, args...);
	}

	template <typename... Args>
	inline void LogInfo(const char* format, Args... args)
	{
		Log::Write(LogLevel::Info, format, args...);
	}

	template <typename... Args>
	inline void LogDebug(const wchar_t* format, Args... args)
	{
		Log::Write(LogLevel::Debug, format, args...);
	}

	template <typename... Args>
	inline void LogDebug(const char* format, Args... args)
	{
		Log::Write(LogLevel::Debug, format, args...);
	}
}
