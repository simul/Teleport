#pragma once
#define ABSL_USES_STD_OPTIONAL 0
// (c) Copyright 2018-2023 Simul Software Ltd

#include "networksink.h"
#include <libavstream/common.hpp>
#include <libavstream/node.hpp>
#include <unordered_map>
#include <libavstream/stream/parser_interface.hpp>

namespace avs
{
	/*!
	 * Network sink node `[passive, 0/1]`
	 *
	 * Reads data for each stream from a corresponding avs::Queue input node
	 * , assembles the data into payloads of network packets and sends the data
	 * to the client.
	 *
	 * If data throttling is enabled for a stream, the seding of data may be spread
	 * over time to reduce network congestion.
	 */
	class AVSTREAM_API WebRtcNetworkSink final : public NetworkSink
	{
		AVSTREAM_PUBLICINTERFACE(WebRtcNetworkSink)
		Private* m_data = nullptr;
	public:
		WebRtcNetworkSink();
		virtual ~WebRtcNetworkSink();

		/*!
		 * Configure network sink and bind to local UDP endpoint.
		 * \param streams Collection of configurations for each stream.
		 * \param localPort Local UDP endpoint port number.
		 * \param remote Remote UDP endpoint name or IP address.
		 * \param remotePort Remote UDP endpoint port number.
		 * \param params Additional network sink parameters.
		 * \return
		 *  - Result::OK on success.
		 *  - Result::Node_InvalidConfiguration if numInputs, localPort, or remotePort is zero, or if remote is either nullptr or empty string.
		 *  - Result::Network_BindFailed if failed to bind to local UDP socket.
		 */
		Result configure(std::vector<NetworkSinkStream>&& streams, const char* local_bind_addr, uint16_t localPort, const char* remote, uint16_t remotePort, const NetworkSinkParams& params = {});

		/*!
		 * Deconfigure network sink and release all associated resources.
		 * \return Always returns Result::OK.
		 */
		Result deconfigure() override;

		/*!
		 * Send data for all streams to remote UDP endpoint.
		 * \return
		 *  - Result::OK on success.
		 *  - Result::Node_NotConfigured if network sink has not been configured.
		 *  - Result::Network_ResolveFailed if failed to resolve the name of remote UDP endpoint.
		 *  - Result::Network_SendFailed on general network send failure.
		 */
		Result process(uint64_t timestamp, uint64_t deltaTime) override;

		//! Get node display name (for reporting & profiling).
		const char* getDisplayName() const override { return "WebRtcNetworkSink"; }

		NetworkSinkCounters getCounters() const;
		void setProcessingEnabled(bool enable);
		bool isProcessingEnabled() const;

		//! IF there is a message to send reliably to the peer, this will fill it in.
		bool getNextStreamingControlMessage(std::string &msg) override;
		void receiveStreamingControlMessage(const std::string&) override;

		// std::function targets
		void OnDataChannelStateChange(uint64_t data_stream_index);
		void OnDataChannelBufferedAmountChange(uint64_t data_stream_index, uint64_t previous);

		void SendConfigMessage(const std::string& str);
		
	protected:
		Result packData(const uint8_t* buffer, size_t bufferSize, uint32_t inputNodeIndex);
		Result sendData(const std::vector<uint8_t>& subPacket);
		std::vector<NetworkSinkStream> m_streams;
		NetworkSinkCounters m_counters;
		mutable std::mutex m_countersMutex;
		bool enabled = true;

		// map from the stream indices to the channels.
		void receiveAnswer(const std::string& answer);
		void receiveCandidate(const std::string& candidate, const std::string& mid,int mlineindex);
		std::vector<std::string> messagesToSend;
		mutable std::mutex messagesMutex;
	//	std::unordered_map<uint32_t, int> m_streamNodeMap;
		NetworkSinkParams  m_params;
	};

} // avs