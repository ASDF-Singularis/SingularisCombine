#pragma once

#include "Modules/ModuleManager.h"

/**
 * 引力奇点化合模块 (Singularis Combine Module)
 * 提供基于 GameplayTags 的运行时化合管线系统，用于动态评估与反应 Actor 状态标签组合。
 */
class FSingularisCombineModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
