// libavstream
// (c) Copyright 2018-2019 Simul Software Ltd

#include <common_p.hpp>
#include <node_p.hpp>
#include <libavstream/nullsink.hpp>

namespace avs {

NullSink::NullSink()
	: Node(new Node::Private(this))
{}

Result NullSink::configure(size_t numInputs)
{
	setNumInputSlots(numInputs);
	return Result::OK;
}
	
Result NullSink::deconfigure()
{
	setNumInputSlots(0);
	return Result::OK;
}

Result NullSink::read(Node* reader, void* buffer, size_t& bufferSize, size_t& bytesRead)
{
	AVSLOG(Warning) << "Attempted to read from null sink node";
	return Result::Node_NotSupported;
}
	
Result NullSink::write(Node* writer, const void* buffer, size_t bufferSize, size_t& bytesWritten)
{
	// Do nothing.
	return Result::OK;
}

Result NullSink::amend(Node* writer, const void* buffer, size_t bufferSize, size_t& bytesWritten)
{
	// Do nothing.
	return Result::OK;
}
	
Result NullSink::readPacket(Node* reader, void* buffer, size_t& bufferSize, int )
{
	AVSLOG(Warning) << "Attempted to read packets from null sink node";
	return Result::Node_NotSupported;
}
	
Result NullSink::writePacket(Node* writer, const void* buffer, size_t bufferSize, int )
{
	// Do nothing.
	return Result::OK;
}

} // avs