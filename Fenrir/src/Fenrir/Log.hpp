#pragma once

#include "Core.hpp"
#include "spdlog/spdlog.h"
#include "spdlog/fmt/ostr.h"

namespace Fenrir
{
	class FENRIR_API Log
	{

	public:
		static void Init();

		inline static std::shared_ptr<spdlog::logger>& GetCoreLogger() { return s_CoreLogger; }
		inline static std::shared_ptr<spdlog::logger>& GetClientLogger() { return s_ClientLogger; }

	private:
		static std::shared_ptr<spdlog::logger> s_CoreLogger;
		static std::shared_ptr<spdlog::logger> s_ClientLogger;

	};
}

//CORE LOGGING MACROS
#define FE_CORE_TRACE(...)   ::Fenrir::Log::GetCoreLogger()->trace(__VA_ARGS__)
#define FE_CORE_INFO(...)    ::Fenrir::Log::GetCoreLogger()->info(__VA_ARGS__)
#define FE_CORE_WARN(...)    ::Fenrir::Log::GetCoreLogger()->warn(__VA_ARGS__)
#define FE_CORE_ERROR(...)   ::Fenrir::Log::GetCoreLogger()->error(__VA_ARGS__)
#define FE_CORE_FATAL(...)   ::Fenrir::Log::GetCoreLogger()->fatal(__VA_ARGS__)


//CIENT LOGGING MACROS
#define FE_TRACE(...)   ::Fenrir::Log::GetClientLogger()->trace(__VA_ARGS__)
#define FE_INFO(...)    ::Fenrir::Log::GetClientLogger()->info(__VA_ARGS__)
#define FE_WARN(...)    ::Fenrir::Log::GetClientLogger()->warn(__VA_ARGS__)
#define FE_ERROR(...)   ::Fenrir::Log::GetClientLogger()->error(__VA_ARGS__)
#define FE_FATAL(...)   ::Fenrir::Log::GetClientLogger()->fatal(__VA_ARGS__)

