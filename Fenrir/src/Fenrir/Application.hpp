#pragma once

#include "Core.hpp"
#include "Events/Event.hpp"

namespace Fenrir {

	class FENRIR_API Application
	{
	public:
		Application();
		virtual ~Application();

		void Run();
	};

	// to be defined in CLIENT
	Application* CreateApplication();
}

