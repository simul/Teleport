// (C) Copyright 2018 Simul.co

#include "VideoStreamClient.h"

#include <Kernel/OVR_LogUtils.h>
#include <sched.h>

VideoStreamClient::VideoStreamClient(avs::Queue *recvQueue)
    : mRecvQueue(recvQueue)
    , mIsReceiving(false)
{
    assert(mRecvQueue);
}

VideoStreamClient::~VideoStreamClient()
{
    StopReceiving();
}

bool VideoStreamClient::StartReceiving(const std::string &address, uint16_t port)
{
    if(mIsReceiving.load()) {
        WARN("VideoStreamClient: Already receiving");
        return false;
    }

    mIsReceiving.store(true);
    mRecvThread = std::thread(&VideoStreamClient::RecvThreadMain, this, address, port);
    return true;
}

void VideoStreamClient::StopReceiving()
{
    mIsReceiving.store(false);
    if(mRecvThread.joinable()) {
        mRecvThread.join();
    }
}

void VideoStreamClient::RecvThreadMain(std::string address, uint16_t port)
{
    avs::NetworkSourceParams params = {};
    params.gcTTL = (1000/60) * 2; // TTL = 2 * expected frame time
    params.jitterDelay = 0;

    avs::NetworkSource networkSource;
    if(!networkSource.configure(1, port, address.c_str(), port, params)) {
        WARN("VideoStreamClient: Failed to configure network source");
        return;
    }

    avs::Forwarder forwarder;
    forwarder.configure(1, 1, 64 * 1024);

    avs::Pipeline pipeline;
    pipeline.add( { &networkSource, &forwarder, mRecvQueue} );

    while(mIsReceiving.load()) {
        pipeline.process();
        sched_yield();
    }
}