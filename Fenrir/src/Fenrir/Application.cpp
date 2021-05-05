#include "Application.hpp"

#include "Fenrir/Events/ApplicationEvent.hpp"
#include "Fenrir/Log.hpp"

namespace Fenrir
{
	Application::Application()
	{
	}

	Application::~Application()
	{
	}

	void Application::Run()
	{
		WindowResizeEvent e(1280, 720);
		if (e.IsInCategory(EventCategoryApplication))
		{
			FE_TRACE(e);
		}
		if (e.IsInCategory(EventCategoryInput))
		{
			FE_TRACE(e);
		}
		while (true);
	}
}