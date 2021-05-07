#include "pch.h"
#include "Application.hpp"

#include "Fenrir/Events/ApplicationEvent.hpp"
#include "Fenrir/Log.hpp"

namespace Fenrir
{
	Application::Application()
	{
		m_Window = std::unique_ptr<Window>(Window::Create());
	}

	Application::~Application()
	{
	}

	void Application::Run()
	{
		while (m_Running)
		{
			m_Window->OnUpdate();
		}
	}
}