#pragma once

#include "SimulCasterServer/CasterContext.h"

#include "Pipelines/EncodePipelineInterface.h"

struct FUnrealCasterContext: public SCServer::CasterContext
{
	std::unique_ptr<IEncodePipeline> EncodePipeline;
};