#pragma once

#include "Core.hpp"
#include "Window.hpp"
#include "Events/Event.hpp"

namespace Fenrir {

	class FENRIR_API Application
	{
	public:
		Application();
		virtual ~Application();

		void Run();

	private:
		std::unique_ptr<Window> m_Window;
		bool m_Running = true;
	};

	// to be defined in CLIENT
	Application* CreateApplication();
}

