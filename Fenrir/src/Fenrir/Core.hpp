#pragma once

#ifdef FE_PLATFORM_WINDOWS
	#ifdef FE_BUILD_DLL
		#define FENRIR_API __declspec(dllexport)
	#else
		#define FENRIR_API __declspec(dllimport)
	#endif

#else
#error FenrirEngine only supports Windows!
#endif

#ifdef FE_ENABLE_ASSERTS
#define FE_ASSERT(x, ...) { if(!(x)) { FE_ERROR("Assertion Failed: {0}", __VA_ARGS__); __debugbreak(); } }
#define FE_CORE_ASSERT(x, ...) { if(!(x)) { FE_CORE_ERROR("Assertion Failed: {0}", __VA_ARGS__); __debugbreak(); } }
#else
#define FE_ASSERT(x, ...)
#define FE_CORE_ASSERT(x, ...)
#endif

#define BIT(x) (1 << x)