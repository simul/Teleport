// (C) Copyright 2018 Simul.co

#include <VrApi_Types.h>
#include <Kernel/OVR_LogUtils.h>
#include <string>
#include <OVR_Input.h>
#include <VrApi_Input.h>

#include "SessionClient.h"

enum RemotePlaySessionChannel {
    RPCH_Control  = 0,
    RPCH_HeadPose = 1,
    RPCH_NumChannels,
};

struct RemotePlayInputState {
    uint32_t buttonsPressed;
    uint32_t buttonsReleased;
    float trackpadAxisX;
    float trackpadAxisY;
};

SessionClient::SessionClient(SessionCommandInterface* commandInterface)
    : mCommandInterface(commandInterface)
    , mClientHost(nullptr)
    , mServerPeer(nullptr)
    , mPrevControllerState({})
{}

SessionClient::~SessionClient()
{
    Disconnect(0);
}

bool SessionClient::Connect(const char* ipAddress, uint16_t port, uint timeout)
{
    ENetAddress address;
    enet_address_set_host_ip(&address, ipAddress);
    address.port = port;

    mClientHost = enet_host_create(nullptr, 1, RPCH_NumChannels, 0, 0);
    if(!mClientHost) {
        FAIL("Failed to create ENET client host");
        return false;
    }

    mServerPeer = enet_host_connect(mClientHost, &address, RPCH_NumChannels, 0);
    if(!mServerPeer) {
        WARN("Failed to initiate connection to the server");
        enet_host_destroy(mClientHost);
        mClientHost = nullptr;
        return false;
    }

    ENetEvent event;
    if(enet_host_service(mClientHost, &event, timeout) > 0 && event.type == ENET_EVENT_TYPE_CONNECT) {
        LOG("Connected to session server: %s:%d", ipAddress, port);
        return true;
    }

    WARN("Failed to connect to remote session server");

    enet_host_destroy(mClientHost);
    mClientHost = nullptr;
    mServerPeer = nullptr;
    return false;
}

void SessionClient::Disconnect(uint timeout)
{
    if(mClientHost && mServerPeer) {
        if (timeout == 0) {
            enet_peer_disconnect_now(mServerPeer, 0);
        } else {
            enet_peer_disconnect(mServerPeer, 0);

            bool isPeerConnected = true;
            ENetEvent event;
            while (isPeerConnected && enet_host_service(mClientHost, &event, timeout) > 0) {
                switch (event.type) {
                    case ENET_EVENT_TYPE_RECEIVE:
                        enet_packet_destroy(event.packet);
                        break;
                    case ENET_EVENT_TYPE_DISCONNECT:
                        isPeerConnected = false;
                        break;
                }
            }

            if (isPeerConnected) {
                enet_peer_reset(mServerPeer);
            }
        }
        mServerPeer = nullptr;
    }

    if(mClientHost) {
        enet_host_destroy(mClientHost);
        mClientHost = nullptr;
    }
}

void SessionClient::Frame(const OVR::ovrFrameInput& vrFrame, const ControllerState& controllerState)
{
    if(mClientHost && mServerPeer) {

        SendHeadPose(vrFrame.Tracking.HeadPose);
        SendInput(controllerState);

        ENetEvent event;
        while(enet_host_service(mClientHost, &event, 0) > 0) {
            switch(event.type) {
                case ENET_EVENT_TYPE_RECEIVE:
                    DispatchEvent(event);
                    break;
                case ENET_EVENT_TYPE_DISCONNECT:
                    Disconnect(0);
                    return;
            }
        }
    }

    mPrevControllerState = controllerState;
}

void SessionClient::DispatchEvent(const ENetEvent& event)
{
    switch(event.channelID) {
        case RPCH_Control:
            ParseCommandPacket(event.packet);
            break;
        default:
            WARN("Received packet on output-only channel: %d", event.channelID);
            break;
    }

    enet_packet_destroy(event.packet);
}

void SessionClient::ParseCommandPacket(ENetPacket* packet)
{
    // TODO: Sanitize!
    const std::string command(reinterpret_cast<const char*>(packet->data), packet->dataLength);
    WARN("CMD: %s", command.c_str());
    if(command[0] == 'v') {
        int port, width, height;
        sscanf(command.c_str(), "v %d %d %d", &port, &width, &height);
        if(width == 0 && height == 0) {
            mCommandInterface->OnVideoStreamClosed();
        }
        else {
            mCommandInterface->OnVideoStreamChanged(port, width, height);
        }
    }
    else {
        WARN("Invalid command: %c", command[0]);
    }
}

void SessionClient::SendHeadPose(const ovrRigidBodyPosef& pose)
{
    // TODO: Use compact representation with only 3 float values for wire format.
    const ovrQuatf orientation = pose.Pose.Orientation;
    ENetPacket* packet = enet_packet_create(&orientation, sizeof(orientation), 0);
    enet_peer_send(mServerPeer, RPCH_HeadPose, packet);
}

void SessionClient::SendInput(const ControllerState& controllerState)
{
    RemotePlayInputState inputState = {};

    const uint32_t buttonsDiffMask = mPrevControllerState.mButtons ^ controllerState.mButtons;
    auto updateButtonState = [&inputState, &controllerState, buttonsDiffMask](uint32_t button)
    {
        if(buttonsDiffMask & button) {
            if(controllerState.mButtons & button) inputState.buttonsPressed |= button;
            else inputState.buttonsReleased |= button;
        }
    };

    // We need to update trackpad axis on the server whenever:
    // (1) User is currently touching the trackpad.
    // (2) User was touching the trackpad previous frame.
    bool updateTrackpadAxis =    controllerState.mTrackpadStatus
                              || controllerState.mTrackpadStatus != mPrevControllerState.mTrackpadStatus;

    bool stateDirty =  updateTrackpadAxis || buttonsDiffMask > 0;
    if(stateDirty) {
        updateButtonState(ovrButton_A);
        updateButtonState(ovrButton_Enter); // FIXME: Currently not getting down event for this button.
        updateButtonState(ovrButton_Back);

        // Trackpad axis should be non-zero only if the user is currently touching the trackpad.
        if(controllerState.mTrackpadStatus) {
            // Remap axis value to [-1,1] range.
            inputState.trackpadAxisX = 2.0f * controllerState.mTrackpadX - 1.0f;
            inputState.trackpadAxisY = 2.0f * controllerState.mTrackpadY - 1.0f;
        }

        ENetPacket* packet = enet_packet_create(&inputState, sizeof(inputState), ENET_PACKET_FLAG_RELIABLE);
        enet_peer_send(mServerPeer, RPCH_Control, packet);
    }
}