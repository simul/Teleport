// libavstream
// (c) Copyright 2018-2024 Simul Software Ltd

#pragma once

#include <memory>

#include "common_p.hpp"
#include "node_p.hpp"
#include <libavstream/mesh.hpp>

namespace avs
{
	struct GeometrySource::Private final : public PipelineNode::Private
	{
		AVSTREAM_PRIVATEINTERFACE(GeometrySource, PipelineNode)
		GeometryRequesterBackendInterface *m_requesterBackend;
	};
	struct GeometryTarget::Private final : public PipelineNode::Private
	{
		AVSTREAM_PRIVATEINTERFACE(GeometryTarget, PipelineNode)
		GeometryTargetBackendInterface *m_backend;
	};
} // avs