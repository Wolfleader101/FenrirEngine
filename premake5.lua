workspace "FenrirEngine"
	architecture "x64"

	configurations
	{
		"Debug",
		"Release",
		"Dist"
	}

outputdir = "%{cfg.buildcfg}-%{cfg.system}-%{cfg.architecture}"

project "Fenrir"
	location "Fenrir"
	kind "SharedLib"
	language "C++"

	targetdir ("bin/" .. outputdir .. "/%{prj.name}")
	objdir ("bin-int/" .. outputdir .. "/%{prj.name}")

	files
	{
		"%{prj.name}/src/**.hpp",
		"%{prj.name}/src/**.h",
		"%{prj.name}/src/**.cpp"
	}
	
	includedirs
	{
		"%{prj.name}/vendor/spdlog/include"
	}

	filter "system:windows"
		cppdialect "c++17"
		staticruntime "On"
		systemversion "latest"

		defines
		{
			"FE_PLATFORM_WINDOWS",
			"FE_BUILD_DLL"
		}

		postbuildcommands
		{
			("{COPY} %{cfg.buildtarget.relpath} ../bin/" .. outputdir .. "/Sandbox")
		}

	filter "configurations:Debug"
		defines "FE_DEBUG"
		symbols "On"

	filter "configurations:Release"
		defines "FE_RELEASE"
		optimize "On"

	filter "configurations:Dist"
		defines "FE_DIST"
		optimize "On"

project "Sandbox"
	location "Sandbox"
	kind "ConsoleApp"
	language "C++"

	targetdir ("bin/" .. outputdir .. "/%{prj.name}")
	objdir ("bin-int/" .. outputdir .. "/%{prj.name}")

	files
	{
		"%{prj.name}/src/**.hpp",
		"%{prj.name}/src/**.h",
		"%{prj.name}/src/**.cpp"
	}
	
	includedirs
	{
		"Fenrir/vendor/spdlog/include",
		"Fenrir/src"
	}

	links
	{
		"Fenrir"
	}


	filter "system:windows"
		cppdialect "c++17"
		staticruntime "On"
		systemversion "latest"

		defines
		{
			"FE_PLATFORM_WINDOWS"
		}

	filter "configurations:Debug"
		defines "FE_DEBUG"
		symbols "On"

	filter "configurations:Release"
		defines "FE_RELEASE"
		optimize "On"

	filter "configurations:Dist"
		defines "FE_DIST"
		optimize "On"
