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