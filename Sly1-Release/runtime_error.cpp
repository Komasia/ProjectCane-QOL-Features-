#include "runtime_error.h"

#ifdef _WIN32
#include <Windows.h>
#else
#include <csignal>
#include <ctime>
#endif
#include <cstdarg>
#include <chrono>
#include <cstdlib>
#include <cstdio>
#include <exception>
#include <iostream>
#include <mutex>
#include <cstring>
#include <cstdint>

namespace
{
	std::mutex s_consoleMutex;
	bool s_consoleInitialized = false;
	std::mutex s_errorMutex;

	#ifdef _WIN32
	LONG WINAPI RuntimeUnhandledExceptionFilter(EXCEPTION_POINTERS* exceptionPointers)
	{
		EnsureRuntimeErrorConsole();
		const DWORD code = exceptionPointers != nullptr && exceptionPointers->ExceptionRecord != nullptr
			? exceptionPointers->ExceptionRecord->ExceptionCode : 0;
		const void* address = exceptionPointers != nullptr && exceptionPointers->ExceptionRecord != nullptr
			? exceptionPointers->ExceptionRecord->ExceptionAddress : nullptr;
		// Name the module the fault happened in (game, driver, SDK DLL...) and
		// the offset inside it; a bare address means nothing on another PC.
		char moduleName[MAX_PATH] = "unknown module";
		uintptr_t moduleOffset = 0;
		HMODULE module = nullptr;
		if (address != nullptr && GetModuleHandleExA(
				GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
				static_cast<LPCSTR>(address), &module) && module != nullptr)
		{
			char modulePath[MAX_PATH] = {};
			if (GetModuleFileNameA(module, modulePath, MAX_PATH) != 0)
			{
				const char* slash = std::strrchr(modulePath, '\\');
				std::snprintf(moduleName, sizeof(moduleName), "%s", slash != nullptr ? slash + 1 : modulePath);
			}
			moduleOffset = reinterpret_cast<uintptr_t>(address) - reinterpret_cast<uintptr_t>(module);
		}
		char report[768];
		std::snprintf(report, sizeof(report),
			"[FATAL] Unhandled Windows exception 0x%08lX at %p (%s+0x%llX) on thread %lu.\n",
			code, address, moduleName, static_cast<unsigned long long>(moduleOffset),
			GetCurrentThreadId());
		std::fprintf(stderr, "\n%s", report);
		std::fprintf(stderr,
			"Please copy this message and describe what was happening when the failure occurred.\n"
			"(Also saved to crash.log next to the game.)\n");
		std::fflush(stderr);

		// Keep a copy beside the executable, with the time.
		char exePath[MAX_PATH] = {};
		if (GetModuleFileNameA(nullptr, exePath, MAX_PATH) != 0)
		{
			if (char* slash = std::strrchr(exePath, '\\'))
				slash[1] = '\0';
			char logPath[MAX_PATH + 16] = {};
			std::snprintf(logPath, sizeof(logPath), "%scrash.log", exePath);
			FILE* file = nullptr;
			if (fopen_s(&file, logPath, "a") == 0 && file != nullptr)
			{
				SYSTEMTIME st{};
				GetLocalTime(&st);
				std::fprintf(file, "%04u-%02u-%02u %02u:%02u:%02u %s",
					st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, report);
				std::fclose(file);
			}
		}
		MessageBoxW(nullptr,
			L"ProjectCane encountered a fatal error.\n\n"
			L"The error details were printed in the diagnostic console.",
			L"ProjectCane - Fatal Error", MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
		return EXCEPTION_CONTINUE_SEARCH;
	}
	#else
	void RuntimeSignalHandler(int signal)
	{
		std::fprintf(stderr, "\n[FATAL] Received signal %d.\n", signal);
		std::fflush(stderr);
		std::signal(signal, SIG_DFL);
		raise(signal);
	}
	#endif
}

void EnsureRuntimeErrorConsole()
{
	std::lock_guard<std::mutex> consoleLock(s_consoleMutex);
	if (s_consoleInitialized)
		return;
	#ifdef _WIN32
	if (GetConsoleWindow() == nullptr && AllocConsole() == FALSE)
		return;

	FILE* stream = nullptr;
	freopen_s(&stream, "CONOUT$", "w", stdout);
	freopen_s(&stream, "CONOUT$", "w", stderr);
	std::cout.clear();
	std::cerr.clear();
	SetConsoleTitleW(L"ProjectCane - Runtime Diagnostics");

	if (HWND consoleWindow = GetConsoleWindow())
	{
		ShowWindow(consoleWindow, SW_SHOW);
		SetForegroundWindow(consoleWindow);
	}
	#endif
	s_consoleInitialized = true;
}

void ReportRuntimeError(const char* subsystem, const char* format, ...)
{
	EnsureRuntimeErrorConsole();
	std::lock_guard<std::mutex> lock(s_errorMutex);

	#ifdef _WIN32
	SYSTEMTIME time{};
	GetLocalTime(&time);
	std::fprintf(stderr, "[%02u:%02u:%02u.%03u] [ERROR] [%s] ",
		time.wHour, time.wMinute, time.wSecond, time.wMilliseconds,
		subsystem != nullptr ? subsystem : "Runtime");
	#else
	const auto now = std::chrono::system_clock::now();
	const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
		now.time_since_epoch()) % 1000;
	const std::time_t rawTime = std::chrono::system_clock::to_time_t(now);
	std::tm localTime{};
	localtime_r(&rawTime, &localTime);
	std::fprintf(stderr, "[%02d:%02d:%02d.%03lld] [ERROR] [%s] ",
		localTime.tm_hour, localTime.tm_min, localTime.tm_sec,
		static_cast<long long>(milliseconds.count()),
		subsystem != nullptr ? subsystem : "Runtime");
	#endif

	va_list arguments;
	va_start(arguments, format);
	std::vfprintf(stderr, format, arguments);
	va_end(arguments);
	std::fputc('\n', stderr);
	std::fflush(stderr);
}

void ReportFatalRuntimeError(const char* message)
{
	ReportRuntimeError("Fatal", "%s", message != nullptr ? message : "Unknown fatal error");
	#ifdef _WIN32
	MessageBoxW(nullptr,
		L"ProjectCane could not continue.\n\n"
		L"The error details were printed in the diagnostic console.",
		L"ProjectCane - Fatal Error", MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
	#endif
}

void InstallRuntimeErrorHandlers()
{
	#ifdef _WIN32
	SetUnhandledExceptionFilter(RuntimeUnhandledExceptionFilter);
	#else
	std::signal(SIGSEGV, RuntimeSignalHandler);
	std::signal(SIGABRT, RuntimeSignalHandler);
	std::signal(SIGFPE, RuntimeSignalHandler);
	std::signal(SIGILL, RuntimeSignalHandler);
	#endif
	std::set_terminate([]
	{
		const char* message = "Unhandled C++ exception on a worker thread";
		try
		{
			if (std::exception_ptr exception = std::current_exception())
				std::rethrow_exception(exception);
		}
		catch (const std::exception& exception)
		{
			ReportFatalRuntimeError(exception.what());
			std::abort();
		}
		catch (...)
		{
		}
		ReportFatalRuntimeError(message);
		std::abort();
	});
}
