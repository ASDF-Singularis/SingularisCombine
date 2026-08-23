#include "Types/SingularisCombineDependencyRegistry.h"

FSingularisCombineDependencyRegistry& FSingularisCombineDependencyRegistry::Get()
{
	static FSingularisCombineDependencyRegistry Instance;
	return Instance;
}
