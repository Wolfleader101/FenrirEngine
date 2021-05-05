#pragma once

#ifdef FE_PLATFORM_WINDOWS

extern Fenrir::Application* Fenrir::CreateApplication();

int main(int argc, char** argv)
{
	Fenrir::Log::Init();
	FE_CORE_WARN("Initialized Core Log!");
	FE_INFO("Initialized Client Log!");

	auto app = Fenrir::CreateApplication();
	app->Run();
	delete app;
}

#endif // FE_PLATFORM_WINDOWS
