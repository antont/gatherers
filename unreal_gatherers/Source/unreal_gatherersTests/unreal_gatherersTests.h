#pragma once

#include "Modules/ModuleInterface.h"

class Funreal_gatherersTestsModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
