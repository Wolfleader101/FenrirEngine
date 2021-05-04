#pragma once

#include "Core.hpp"

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

