#pragma once
#include "pch.h"

namespace OpenGlass
{
	UINT OpenClassicStartMenuMessage();

	class OpenGlassApp : public wxApp
	{
	public:
		bool OnInit() override;
		void OnInitCmdLine(wxCmdLineParser& parser) override;
	private:
		wxSingleInstanceChecker m_singleInstanceChecker;
		bool m_openClassicStartMenu{ false };
	};
}

DECLARE_APP(OpenGlass::OpenGlassApp)
