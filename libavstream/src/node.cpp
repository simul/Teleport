// libavstream
// (c) Copyright 2018-2019 Simul Software Ltd

#include <optional>
#include <vector>
#include <node_p.hpp>

using namespace avs;

Node::Node(Node::Private* d_ptr)
	: m_d(d_ptr)
{}

Node::~Node()
{
	unlinkAll();
	delete m_d;
}

Result Node::link(Node& source, int sourceSlot, Node& target, int targetSlot)
{
	assert(sourceSlot >= 0);
	assert(targetSlot >= 0);

	if (size_t(sourceSlot) >= source.d().m_outputs.size())
	{
		return Result::Node_InvalidSlot;
	}
	if (size_t(targetSlot) >= target.d().m_inputs.size())
	{
		return Result::Node_InvalidSlot;
	}

	source.unlinkOutput(sourceSlot);
	target.unlinkInput(targetSlot);

	if (Result result = target.onInputLink(targetSlot, &source))
	{
		target.d().m_inputs[targetSlot] = { &source, sourceSlot };
	}
	else {
		return result;
	}

	if (Result result = source.onOutputLink(sourceSlot, &target))
	{
		source.d().m_outputs[sourceSlot] = { &target, targetSlot };
	}
	else {
		target.onInputUnlink(targetSlot, &source);
		target.d().m_inputs[targetSlot] = {};
		return result;
	}
	return Result::OK;
}

Result Node::link(Node& source, Node& target)
{
	std::optional<int> sourceSlot;
	for (int i = 0; i < (int)source.getNumOutputSlots(); ++i)
	{
		if (!source.isOutputLinked(i))
		{
			sourceSlot = i;
			break;
		}
	}
	if (!sourceSlot.has_value())
	{
		AVSLOG(Error) << "Node: Cannot link nodes: no free output slot found on source.\n";
		return Result::Node_LinkFailed;
	}

	std::optional<int> targetSlot;
	for (int i = 0; i < (int)target.getNumInputSlots(); ++i)
	{
		if (!target.isInputLinked(i))
		{
			targetSlot = i;
			break;
		}
	}
	if (!targetSlot.has_value())
	{
		AVSLOG(Error) << "Node: Cannot link nodes: no free source slot found";
		return Result::Node_LinkFailed;
	}

	return Node::link(source, *sourceSlot, target, *targetSlot);
}

Result Node::unlink(Node& source, int sourceSlot, Node& target, int targetSlot)
{
	assert(sourceSlot >= 0);
	assert(targetSlot >= 0);

	if (size_t(sourceSlot) >= source.d().m_outputs.size())
	{
		return Result::Node_InvalidSlot;
	}
	if (size_t(targetSlot) >= target.d().m_inputs.size())
	{
		return Result::Node_InvalidSlot;
	}

	Link& sourceOutputLink = source.d().m_outputs[sourceSlot];
	Link& targetInputLink = target.d().m_inputs[targetSlot];

	if (sourceOutputLink.targetNode != &target || sourceOutputLink.targetSlot != targetSlot ||
		targetInputLink.targetNode != &source || targetInputLink.targetSlot != sourceSlot)
	{
		return Result::Node_InvalidLink;
	}

	target.onInputUnlink(targetSlot, &source);
	targetInputLink = {};
	source.onOutputUnlink(sourceSlot, &target);
	sourceOutputLink = {};
	return Result::OK;
}

Result Node::unlinkInput(int slot)
{
	auto breakLink = [this](int inputSlot, Link& link)
	{
		if (link.targetNode)
		{
			assert(size_t(link.targetSlot) < link.targetNode->d().m_outputs.size());
			onInputUnlink(inputSlot, link.targetNode);
			link.targetNode->onOutputUnlink(link.targetSlot, this);
			link.targetNode->d().m_outputs[link.targetSlot] = {};
		}
		link = {};
	};

	if (slot >= 0)
	{
		if (size_t(slot) >= d().m_inputs.size())
		{
			return Result::Node_InvalidSlot;
		}
		breakLink(slot, d().m_inputs[slot]);
	}
	else {
		for (size_t i = 0; i < d().m_inputs.size(); ++i)
		{
			breakLink(int(i), d().m_inputs[i]);
		}
	}
	return Result::OK;
}

Result Node::unlinkOutput(int slot)
{
	auto breakLink = [this](int outputSlot, Link& link)
	{
		if (link.targetNode)
		{
			assert(size_t(link.targetSlot) < link.targetNode->d().m_inputs.size());
			onOutputUnlink(outputSlot, link.targetNode);
			link.targetNode->onInputUnlink(link.targetSlot, this);
			link.targetNode->d().m_inputs[link.targetSlot] = {};
		}
		link = {};
	};

	if (slot >= 0)
	{
		if (size_t(slot) >= d().m_outputs.size())
		{
			return Result::Node_InvalidSlot;
		}
		breakLink(slot, d().m_outputs[slot]);
	}
	else {
		for (size_t i = 0; i < d().m_outputs.size(); ++i)
		{
			breakLink(int(i), d().m_outputs[i]);
		}
	}
	return Result::OK;
}

void Node::unlinkAll()
{
	unlinkInput();
	unlinkOutput();
}

size_t Node::getNumInputSlots() const
{
	return d().m_inputs.size();
}

size_t Node::getNumOutputSlots() const
{
	return d().m_outputs.size();
}

Result Node::isInputLinked(int slot) const
{
	if (slot < 0 || size_t(slot) >= d().m_inputs.size())
	{
		return Result::Node_InvalidSlot;
	}
	return d().m_inputs[slot].targetNode ? Result::OK : Result::Node_NotLinked;
}

Result Node::isOutputLinked(int slot) const
{
	if (slot < 0 || size_t(slot) >= d().m_outputs.size())
	{
		return Result::Node_InvalidSlot;
	}
	return d().m_outputs[slot].targetNode ? Result::OK : Result::Node_NotLinked;
}

void Node::setNumInputSlots(size_t numSlots)
{
	for (size_t slot = numSlots; slot < d().m_inputs.size(); ++slot)
	{
		unlinkInput(static_cast<int>(slot));
	}
	d().m_inputs.resize(numSlots);
}

void Node::setNumOutputSlots(size_t numSlots)
{
	for (size_t slot = numSlots; slot < d().m_outputs.size(); ++slot)
	{
		unlinkOutput(int(slot));
	}
	d().m_outputs.resize(numSlots);
}

void Node::setNumSlots(size_t numInputSlots, size_t numOutputSlots)
{
	setNumInputSlots(numInputSlots);
	setNumOutputSlots(numOutputSlots);
}

Node* Node::getInput(int slot) const
{
	assert(size_t(slot) < d().m_inputs.size());
	return d().m_inputs[slot].targetNode;
}

Node* Node::getOutput(int slot) const
{
	assert(size_t(slot) < d().m_outputs.size());
	return d().m_outputs[slot].targetNode;
}

int Node::getInputIndex(const Node* node) const
{
	assert(node);
	for (size_t i = 0; i < d().m_inputs.size(); ++i)
	{
		if (d().m_inputs[i].targetNode == node)
		{
			return int(i);
		}
	}
	return -1;
}

int Node::getOutputIndex(const Node* node) const
{
	assert(node);
	for (size_t i = 0; i < d().m_outputs.size(); ++i)
	{
		if (d().m_outputs[i].targetNode == node)
		{
			return int(i);
		}
	}
	return -1;
}