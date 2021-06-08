#pragma once

#include "Core.hpp"
#include "Window.hpp"
#include "Fenrir/LayerStack.hpp"
#include "Fenrir/Events/Event.hpp"
#include "Fenrir/Events/ApplicationEvent.hpp"

namespace Fenrir {

	class FENRIR_API Application
	{
	public:
		Application();
		virtual ~Application();

		void Run();
		void OnEvent(Event& e);

		void PushLayer(Layer* layer);
		void PushOverlay(Layer* layer);
	private:
		bool OnWindowClose(WindowCloseEvent& e);

		std::unique_ptr<Window> m_Window;
		bool m_Running = true;
		LayerStack m_LayerStack;
	};

	// to be defined in CLIENT
	Application* CreateApplication();
}

