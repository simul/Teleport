// libavstream
// (c) Copyright 2018-2022 Simul Software Ltd

#include "networksink_p.hpp"
#include <network/packetformat.hpp>

#include <util/srtutil.h>

#include <iostream>
#include <cmath>

using namespace avs;

NetworkSink::NetworkSink()
	: PipelineNode(new NetworkSink::Private(this)), m_data((NetworkSink::Private*)(m_d))
{}

Result NetworkSink::configure(std::vector<NetworkSinkStream>&& streams, const char* local_bind, uint16_t localPort, const char* remote, uint16_t remotePort, const NetworkSinkParams& params)
{
	size_t numInputs = streams.size();
	if (numInputs == 0 || localPort == 0 || remotePort == 0)
	{
		return Result::Node_InvalidConfiguration;
	}
	if (!remote || !remote[0])
	{
		return Result::Node_InvalidConfiguration;
	}
	if (numInputs > (size_t)PacketFormat::MaxNumStreams)
	{
		return Result::Node_InvalidConfiguration;
	}
	try
	{
		srt_startup();
#if NDEBUG
		srt_logging::LogLevel::type loglevel = srt_logging::LogLevel::error;
#else
		srt_logging::LogLevel::type loglevel = srt_logging::LogLevel::debug;
#endif
		srt_setloglevel(loglevel);
		m_data->pollid = srt_epoll_create();
		m_data->m_socket = srt_create_socket();
		int no = 0;
		//int hundred = 100;
		int thousand = 1000;
		//int thirty_thousand = 30000;
		//int million = 1000000;
		srt_setsockflag(m_data->m_socket, SRTO_RCVSYN, &no, sizeof no);
		//srt_setsockflag(m_data->m_socket, SRTO_SNDSYN, &no, sizeof no);
		srt_setsockflag(m_data->m_socket, SRTO_SNDTIMEO, &thousand, sizeof thousand);
		srt_setsockflag(m_data->m_socket, SRTO_RCVTIMEO, &thousand, sizeof thousand);

		srt_setsockflag(m_data->m_socket, SRTO_PEERIDLETIMEO, &params.connectionTimeout, sizeof params.connectionTimeout);

		//SRT_TRANSTYPE tt = SRTT_LIVE;
		//CHECK_SRT_ERROR(srt_setsockopt(m_data->m_socket, 0, SRTO_TRANSTYPE, &tt, sizeof tt));
		//int32_t latency=params.requiredLatencyMs;
		//CHECK_SRT_ERROR(srt_setsockopt(m_data->m_socket, 0, SRTO_PEERLATENCY, &latency, sizeof latency));
		int payloadSize = avs::PacketFormat::MaxPacketSize;
		CHECK_SRT_ERROR(srt_setsockopt(m_data->m_socket, 0, SRTO_PAYLOADSIZE, &payloadSize, sizeof(payloadSize)));
		int events = SRT_EPOLL_IN | SRT_EPOLL_ERR | SRT_EPOLL_OUT;
		CHECK_SRT_ERROR(srt_epoll_add_usock(m_data->pollid, m_data->m_socket, &events));
		sockaddr_in local_bind_addr;

		// bind the socket to the local address and port.
		local_bind_addr.sin_family = AF_INET;
		local_bind_addr.sin_port = htons(localPort);
		if (local_bind)
		{
			if (inet_pton(AF_INET, local_bind, &local_bind_addr.sin_addr) != 1)
				return Result::Node_InvalidConfiguration;
		}
		else
		{
			local_bind_addr.sin_addr.s_addr = htonl(INADDR_ANY);
		}
		CHECK_SRT_ERROR(srt_bind(m_data->m_socket, (struct sockaddr*)&local_bind_addr, sizeof(sockaddr_in)));

		srt_listen(m_data->m_socket, 2);

		m_data->throttleRate = params.throttleToRateKpS;
		m_data->socketBufferSize = params.socketBufferSize;
	}
	catch (const std::exception& e)
	{
		AVSLOG(Error) << "NetworkSink: Failed to bind to UDP socket: " << e.what();
		return Result::Network_BindFailed;
	}

	setNumInputSlots(numInputs);

	m_data->m_streams = std::move(streams);

	// Called by the parser interface if the stream uses one
	auto onPacketParsed = [](PipelineNode* node, uint32_t inputNodeIndex, const char* buffer, size_t dataSize, size_t dataOffset, bool isLastPayload)->Result
	{
		NetworkSink* ns = static_cast<NetworkSink*>(node);
		return ns->packData((const uint8_t*)buffer + dataOffset, dataSize, inputNodeIndex);
	};
	if(m_data->m_streams.size()>=INT_MAX)
		return Result::Failed;
	for (uint32_t i = 0; i < m_data->m_streams.size(); ++i)
	{
		auto& stream = m_data->m_streams[i];
		stream.buffer.resize(stream.chunkSize);
		m_data->m_streamIndices.emplace(stream.id, i);

		if (stream.useParser)
		{
			auto parser = std::unique_ptr<StreamParserInterface>(avs::StreamParserInterface::Create(stream.parserType));
			parser->configure(this, onPacketParsed, i);
			m_data->m_parsers[i] = std::move(parser);
		}
	}

	m_data->m_EFPSender.reset(new ElasticFrameProtocolSender(PacketFormat::MaxPacketSize));
	// The callback will be called on the same thread calling 'packAndSendFromPtr'
	m_data->m_EFPSender->sendCallback = std::bind(&NetworkSink::sendOrCacheData, this, std::placeholders::_1);

	// 6 MB (48 mb) per second limit
	m_data->m_maxPacketsAllowedPerSecond = 6000000 / PacketFormat::MaxPacketSize;
	m_data->m_maxPacketsAllowed = 0;
	m_data->m_packetsSent = 0;

	m_data->m_params = params;

	m_data->m_statsTimeElapsed = 0;
	m_data->m_minBandwidthUsed = UINT32_MAX;

	setProcessingEnabled(true);

	return Result::OK;
}

NetworkSink::~NetworkSink()
{
	deconfigure();
}

Result NetworkSink::deconfigure()
{
	while (!m_data->m_dataQueue.empty())
	{
		m_data->m_dataQueue.pop();
	}

	m_data->m_EFPSender.reset();
	m_data->m_parsers.clear();
	m_data->m_streams.clear();

	if (getNumInputSlots() <= 0)
		return Result::OK;
	setNumInputSlots(0);

	m_data->m_remote = {};

	m_data->m_counters = {};
	m_data->m_statsTimeElapsed = 0;
	m_data->m_minBandwidthUsed = UINT32_MAX;
	

	srt_close(m_data->m_remote_socket);
	srt_close(m_data->m_socket);
	srt_epoll_release(m_data->pollid);
	m_data->bConnected = false;
	m_data->pollid = 0;
	m_data->m_socket = 0;
	m_data->m_remote_socket = 0;
	
	srt_cleanup();

	m_data->debugStream = 0;
	m_data->doChecksums = false;

	m_data->m_params = {};

	return Result::OK;
}

Result NetworkSink::process(uint64_t timestamp, uint64_t deltaTime)
{
	if (getNumInputSlots() == 0 || !m_data->m_socket)
	{
		return Result::Node_NotConfigured;
	}

	// Don't continue if srt already had an error while connected to prevent CUDT exception spamming.
	if (!m_data->m_processingEnabled)
	{
		return Result::Network_Disconnection;
	}

	int srtrfdslen = 2;
	int srtwfdslen = 2;
	SRTSOCKET srtrwfds[4] = { SRT_INVALID_SOCK, SRT_INVALID_SOCK, SRT_INVALID_SOCK, SRT_INVALID_SOCK };
	int sysrfdslen = 2;
	SYSSOCKET sysrfds[2];
	if (srt_epoll_wait(m_data->pollid,
		&srtrwfds[0], &srtrfdslen, &srtrwfds[2], &srtwfdslen,
		100,
		&sysrfds[0], &sysrfdslen, 0, 0) >= 0)
	{
		for (size_t i = 0; i < sizeof(srtrwfds) / sizeof(SRTSOCKET); i++)
		{
			SRTSOCKET s = srtrwfds[i];
			if (s == SRT_INVALID_SOCK)
				continue;
			SRT_SOCKSTATUS status = srt_getsockstate(s);
			if (!m_data->m_remote_socket)
			{
				switch (status)
				{
				case SRTS_LISTENING:
				{
					//AVSLOG(Info) << "SRTS_LISTENING \n"; too much spam.
					socklen_t sa_len = sizeof(sockaddr_in);
					SRTSOCKET rem = srt_accept(m_data->m_socket, (struct sockaddr*)(&m_data->remote_addr), (int*)&sa_len);
					if (rem > 0)
					{
						m_data->m_remote_socket = rem;
						m_data->bConnected = true;
					}
					else
					{
						setProcessingEnabled(false);
						return Result::Network_Disconnection;
					}
				}
				break;
				case SRTS_CONNECTED:
					if (s > 0)
					{
						int events = SRT_EPOLL_IN | SRT_EPOLL_ERR;
						CHECK_SRT_ERROR(srt_epoll_add_usock(m_data->pollid, m_data->m_remote_socket, &events));
						m_data->bConnected = true;
						m_data->m_remote_socket = s;
						//AVSLOG(Info) << "SRTS_CONNECTED"<<"\n";
					}
					break;
				default:
					AVSLOG(Info) << "SRTS_? \n";
					break;
				};
			}
			bool issource = (s == m_data->m_remote_socket);
			if (!issource)
			{
				continue;
			}
			switch (status)
			{
			case SRTS_LISTENING:
				AVSLOG(Info) << "SRTS_LISTENING \n";
				break;
			case SRTS_BROKEN:
				AVSLOG(Error) << "SRTS_BROKEN \n";
				closeConnection();
				break;
			case SRTS_NONEXIST:
				AVSLOG(Error) << "SRTS_NONEXIST \n";
				closeConnection();
				break;
			case SRTS_CLOSED:
				AVSLOG(Error) << "SRTS_CLOSED \n";
				closeConnection();
				break;
			case SRTS_CONNECTED:
				AVSLOG(Info) << "SRTS_CONNECTED \n";
				m_data->bConnected = true;
			default:
				break;
			};
		}
	}
	else if (m_data->bConnected)
	{
		closeConnection();
		setProcessingEnabled(false);
		return Result::Network_Disconnection;
	}

	if (!m_data->m_remote_socket || !m_data->bConnected)
		return Result::Network_NoConnection;

	SRT_SOCKSTATUS stat = srt_getsockstate(m_data->m_remote_socket);
	if (stat != SRT_SOCKSTATUS::SRTS_CONNECTED)
	{
		if (stat == SRT_SOCKSTATUS::SRTS_CLOSED)
		{
			closeConnection();
		}
		return Result::Network_Disconnection;
	}

	const size_t numInputs = getNumInputSlots();
	if (numInputs == 0 || !m_data->m_EFPSender)
	{
		return Result::Node_NotConfigured;
	}

	// Called to get the data from the input nodes
	auto readInput = [this](uint32_t inputNodeIndex, size_t& numBytesRead) -> Result
	{
		PipelineNode* node = getInput(inputNodeIndex);
		auto& stream = m_data->m_streams[inputNodeIndex];
		
		assert(node);
		assert(stream.buffer.size() >= stream.chunkSize);

		if (IOInterface* nodeIO = dynamic_cast<IOInterface*>(node))
		{
			size_t bufferSize = stream.buffer.size(); 
			Result result = nodeIO->read(this, stream.buffer.data(), bufferSize, numBytesRead);
			if (result == Result::IO_Retry)
			{
				stream.buffer.resize(bufferSize);
				result = nodeIO->read(this, stream.buffer.data(), bufferSize, numBytesRead);
			}
	
			numBytesRead = std::min(bufferSize, numBytesRead);

			return result;
		}
		else
		{
			assert(false);
			return Result::Node_Incompatible;
		}
	};

	m_data->m_packetsSent = 0;

	m_data->m_maxPacketsAllowed = size_t((double)deltaTime * 0.001 * (double)m_data->m_maxPacketsAllowedPerSecond);

	for (int i = 0; i < (int)getNumInputSlots(); ++i)
	{
		const NetworkSinkStream& stream = m_data->m_streams[i];

		if (stream.isDataLimitPerFrame)
		{
			while (!m_data->m_dataQueue.empty() && m_data->m_packetsSent < m_data->m_maxPacketsAllowed)
			{
				sendData(m_data->m_dataQueue.front());
				m_data->m_dataQueue.pop();
			}
		}

		size_t numBytesRead = 0;
		try
		{
			if (Result result = readInput(i, numBytesRead); result != Result::OK)
			{
				if (result != Result::IO_Empty)
				{
					AVSLOG(Error) << "NetworkSink: Failed to read from input node: " << i << "\n";
					continue;
				}
			}
		}
		catch (const std::bad_alloc&)
		{
			return Result::IO_OutOfMemory;
		}

		if (numBytesRead == 0)
		{
			continue;
		}
		Result res = Result::OK;
		if (stream.useParser && m_data->m_parsers.find(i) != m_data->m_parsers.end())
		{
			res = m_data->m_parsers[i]->parse((const char*)stream.buffer.data(), numBytesRead);
		}
		else
		{
			res = packData(stream.buffer.data(), numBytesRead, i);
		}
		if (!res)
		{
			return res;
		}
	}

	updateCounters(timestamp, deltaTime);

	return Result::OK;
}

void NetworkSink::setProcessingEnabled(bool enable)
{
	m_data->m_processingEnabled = enable;
}

bool NetworkSink::isProcessingEnabled() const
{
	return m_data->m_processingEnabled;
}


void NetworkSink::updateCounters(uint64_t timestamp, uint32_t deltaTime)
{
	if (m_data->m_params.bandwidthInterval == 0)
	{
		return;
	}

	m_data->m_statsTimeElapsed += deltaTime;

	// Update network stats
	std::lock_guard<std::mutex> lock(m_data->m_countersMutex);

	if (m_data->m_packetsSent > 0)
	{
		m_data->m_counters.networkPacketsSent += m_data->m_packetsSent;
		m_data->m_counters.bytesSent = m_data->m_counters.networkPacketsSent * PacketFormat::MaxPacketSize;
	}

	if (m_data->m_statsTimeElapsed > m_data->m_params.bandwidthInterval)
	{
		m_data->m_statsTimeElapsed = 0;

		SRT_TRACEBSTATS perf;
		// returns 0 if there's no error during execution and -1 if there is
		if (srt_bstats(m_data->m_remote_socket, &perf, true) != 0)
		{
			return;
		}
		m_data->m_counters.bandwidth = perf.mbpsBandwidth;

		m_data->m_counters.avgBandwidthUsed = perf.mbpsSendRate;

		if (m_data->m_counters.avgBandwidthUsed > 0 && m_data->m_counters.avgBandwidthUsed < m_data->m_minBandwidthUsed)
		{
			m_data->m_minBandwidthUsed = (uint32_t)(m_data->m_counters.avgBandwidthUsed);
			m_data->m_counters.minBandwidthUsed = m_data->m_minBandwidthUsed;
		}

		if (m_data->m_counters.avgBandwidthUsed > m_data->m_counters.maxBandwidthUsed)
		{
			m_data->m_counters.maxBandwidthUsed = m_data->m_counters.avgBandwidthUsed;
		}
	}
}

Result NetworkSink::packData(const uint8_t* buffer, size_t bufferSize, uint32_t inputNodeIndex)
{
	uint32_t code = 0;
	ElasticFrameContent dataContent;

	auto& stream = m_data->m_streams[inputNodeIndex];

	switch (stream.dataType)
	{
	case NetworkDataType::Geometry:
	case NetworkDataType::Audio:
	case NetworkDataType::VideoTagData:
		dataContent = ElasticFrameContent::privatedata;
		break;
	case NetworkDataType::H264:
		code = EFP_CODE('A', 'N', 'X', 'B');
		dataContent = ElasticFrameContent::h264;
		break;
	case NetworkDataType::HEVC:
		code = EFP_CODE('A', 'N', 'X', 'B');
		dataContent = ElasticFrameContent::h265;
		break;
	default:
		AVSLOG(Error) << "NetworkSink: Invalid stream data type. Cannot send data. \n";
		return Result::NetworkSink_InvalidStreamDataType;
	}

	// Total number of EFP superframes created since the start for this stream.
	stream.counter++;

	auto efpResult = m_data->m_EFPSender->packAndSendFromPtr(buffer,
		bufferSize,
		dataContent, 
		stream.counter, // pts
		stream.counter, // dts
		code,
		stream.id,
		NO_FLAGS);

	if (efpResult != ElasticFrameMessages::noError)
	{
		AVSLOG(Error) << "NetworkSink: An error occured in EFP trying to pack data. \n";
		return Result::NetworkSink_PackingDataFailed;
	}

	return Result::OK;
}

void NetworkSink::sendOrCacheData(const std::vector<uint8_t>& subPacket)
{
	uint8_t id = subPacket[1];
	auto index = m_data->m_streamIndices[id];

	// streamID is second byte for all EFP packet types
	const auto& stream = m_data->m_streams[index];

	if (stream.isDataLimitPerFrame && m_data->m_packetsSent >= m_data->m_maxPacketsAllowed)
	{
		m_data->m_dataQueue.push(subPacket);
		return;
	}

	sendData(subPacket);
}

void NetworkSink::sendData(const std::vector<uint8_t> &subPacket)
{
	const char* buffer = (const char*)subPacket.data();
	const size_t bufferSize = subPacket.size();

	SRT_MSGCTRL mctrl;
	srt_msgctrl_init(&mctrl);
	int r = srt_sendmsg2(m_data->m_remote_socket, buffer, (int)bufferSize, &mctrl);
	if (r < 0)
	{
		closeConnection();
		m_data->q_ptr()->setProcessingEnabled(false);
		return;
	}
	m_data->m_packetsSent++;
}

void NetworkSink::closeConnection()
{
	if (m_data->bConnected)
	{
		m_data->bConnected = false;
		srt_close(m_data->m_remote_socket);
		m_data->m_remote_socket = 0;
	}
}

NetworkSinkCounters NetworkSink::getCounters() const
{
	std::lock_guard<std::mutex> lock(m_data->m_countersMutex);
	return m_data->m_counters;
}

void NetworkSink::setDebugStream(uint32_t s)
{
	m_data->debugStream = s;
}

void NetworkSink::setDoChecksums(bool s)
{
	m_data->doChecksums = s;
}

void NetworkSink::setDebugNetworkPackets(bool s)
{
	m_data->mDebugNetworkPackets = s;
}

void NetworkSink::setEstimatedDecodingFrequency(uint8_t estimatedDecodingFrequency)
{
	m_data->estimatedDecodingFrequency = estimatedDecodingFrequency;
}
