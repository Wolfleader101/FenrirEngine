#pragma once

#include "Core.hpp"
#include "Window.hpp"
#include "Fenrir/Events/ApplicationEvent.hpp"
#include "Events/Event.hpp"

namespace Fenrir {

	class FENRIR_API Application
	{
	public:
		Application();
		virtual ~Application();

		void Run();
		void OnEvent(Event& e);
	private:
		bool OnWindowClose(WindowCloseEvent& e);

		std::unique_ptr<Window> m_Window;
		bool m_Running = true;
	};

	// to be defined in CLIENT
	Application* CreateApplication();
}

