#pragma once

#ifdef FE_PLATFORM_WINDOWS

extern Fenrir::Application* Fenrir::CreateApplication();

int main(int argc, char** argv)
{
	auto app = Fenrir::CreateApplication();
	app->Run();
	delete app;
}

#endif // FE_PLATFORM_WINDOWS
