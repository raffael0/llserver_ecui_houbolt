//
// Created by Markus on 31.03.21.
//

#include "can/CANDriver.h"
#include "can_houbolt/can_cmds.h"
#include "utility/utils.h"

#include <utility>
#include <string>

CANDriver::CANDriver(std::function<void(uint8_t &, uint32_t &, uint8_t *, uint32_t &, uint64_t &)> onInitRecvCallback,
                     std::function<void(uint8_t &, uint32_t &, uint8_t *, uint32_t &, uint64_t &)> onRecvCallback,
                     std::function<void(std::string *)> onErrorCallback,
                     CANParams arbitrationParams,
                     CANParams dataParams)
                     : onRecvCallback(std::move(onInitRecvCallback)),
                     seqRecvCallback(std::move(onRecvCallback)),
                     onErrorCallback(std::move(onErrorCallback)),
                     arbitrationParams(arbitrationParams), dataParams(dataParams)
{
    canStatus stat;
    for (size_t i = 0; i < CAN_CHANNELS; i++)
    {
        stat = InitializeCANChannel(i);
        if (stat < 0) {
            std::ostringstream stringStream;
            stringStream << "CANDriver - Constructor: CAN-Channel " << i << ": " << CANError(stat);
            throw std::runtime_error(stringStream.str());
        }
    }
    
}

CANDriver::~CANDriver()
{
    // Empty transfer queues (not strictly necessary but recommended by Kvaser)
    for (size_t i = 0; i < CAN_CHANNELS; i++)
    {
        (void) canWriteSync(canHandles[i], 5);
        (void) canBusOff(canHandles[i]);
        (void) canClose(canHandles[i]);
    }
    canUnloadLibrary();
}

void CANDriver::InitDone(void)
{
    //TODO: MP mutual exclusion needed if implemented this way
    onRecvCallback = seqRecvCallback;
}

void CANDriver::SendCANMessage(uint32_t canChannelID, uint32_t canID, uint8_t *payload, uint32_t payloadLength)
{
    if (payloadLength > MAX_DATA_SIZE)
    {
        throw std::runtime_error("CANDriver - SendCANMessage: payload length " + std::to_string(payloadLength) + " exceeds supported can fd msg data size " + std::to_string(MAX_DATA_SIZE));
    }
    //convert to dlc
    uint32_t dlc = -1;
    uint32_t dlcBytes = -1;
    for (auto it = dlcToCANFDMsgLength.begin(); it != dlcToCANFDMsgLength.end(); it++)
    {
        auto it2 = it;
        it2++;
        if (it->second < payloadLength && payloadLength <= (it2)->second)
        {
            dlc = it2->first;
            dlcBytes = it2->second;
            break;
        }
    }
    if (dlc == -1)
    {
        throw std::runtime_error("CANDriver - SendCANMessage: correct dlc couldn't be found");
    }
    // Flags mean that the message is a FD message (FDF, BRS) and that an extended id is used (EXT)
    uint8_t msg[64] = {0};
    std::copy_n(payload, payloadLength, msg);
    canStatus stat = canWrite(canHandles[canChannelID], canID, (void *) msg, dlcBytes, canFDMSG_FDF | canFDMSG_BRS);

    if(stat < 0) {
        throw std::runtime_error("CANDriver - SendCANMessage: " + CANError(stat));
    }
}

std::map<std::string, bool> CANDriver::GetCANStatusReadable(uint32_t canBusChannelID)
{
    std::map<std::string, bool> canState;
    uint64_t flags;
    canStatus stat = canReadStatus(canHandles[canBusChannelID], &flags);

    if(stat == canOK) {
        std::vector<std::string> names = {"ERROR_PASSIVE", "BUS_OFF", "ERROR_WARNING", "ERROR_ACTIVE",
                             "TX_PENDING", "RX_PENDING", "RESERVED_1", "TXERR", "RXERR", "HW_OVERRUN",
                             "SW_OVERRUN", "OVERRUN"};

        for(uint8_t i = 0; i < names.size(); i++){
            canState[names[i]] = (bool)(stat & (1 << i));
        }
    }

    return canState;
}

//TODO: MP maybe create 2 callbacks one for until init done and one for afterwards to eliminate bus channel id search
// if not needed
void CANDriver::OnCANCallback(int handle, void *driver, unsigned int event)
{
    CANDriver *canDriver = (CANDriver *) driver;

    canStatus stat;
    int64_t id;
    uint32_t dlc, flags;
    uint8_t data[64];
    uint64_t timestamp;
    
    // As the callback only gets entered when the receive queue was empty, empty it in here

    stat = canRead(handle, &id, data, &dlc, &flags, &timestamp); //TODO: is dlc the length code or the actual length?

    switch(event) {
        case canNOTIFY_ERROR:
        {
            if(stat != canOK) {
                std::string errorMsg = "canNOTIFY_ERROR: " + canDriver->CANError(stat);
                canDriver->onErrorCallback(&errorMsg);
            }
            break;   
        }
        case canNOTIFY_STATUS:
        {
            uint64_t statFlags = 0;
            stat = canReadStatus(handle, &statFlags);
            Debug::print("canNOTIFY_STATUS changed");
            if(statFlags & canSTAT_OVERRUN) {
                Debug::print("canNOTIFY_STATUS: buffer overflow");
            }
            break;
        }
        case canNOTIFY_RX:
        {
            while (stat == canOK) {
                if (id < 0)
                {
                    Debug::error("CANDriver - OnCANCallback: id negative");
                    return;
                }
                uint8_t canBusChannelID = -1;
                for (int i = 0; i < CAN_CHANNELS; i++)
                {
                    if (handle == canDriver->canHandles[i])
                    {
                        canBusChannelID = i;
                    }
                }
                if (canBusChannelID == (uint8_t)-1)
                {
                    Debug::error("CANDriver - OnCANCallback: can handle not found");
                    return;
                }
                //TODO: wrap a try except around
                //TODO: switch timestamp to current unix time
                uint64_t softwareTime = utils::getCurrentTimestamp();
                uint64_t statFlags = 0;
                //TODO: MP flag is canok but it seems that its actuall canERR_NOMSG, further debugging needed to remove this dlc check
                if (dlc > 0)
                {
                    canDriver->onRecvCallback(canBusChannelID, (uint32_t &) id, data, dlc, softwareTime);
                }
                else
                {
                    stat = canReadStatus(handle, &statFlags);
                    if (statFlags & canSTAT_SW_OVERRUN)
                    {
                        Debug::error("CANDriver - OnCANCallback: Software Overrun...");
                    }
                    else if (statFlags & canSTAT_HW_OVERRUN)
                    {
                        Debug::error("CANDriver - OnCANCallback: Hardware Overrun...");
                    }
                    else
                    {
                        Debug::error("CANDriver - OnCANCallback: canID: %d, dlc: %d, invalid msg detected, ignoring...", id, dlc);
                    }
                    Debug::print("\t\tCAN Status Flags: 0x%016x", statFlags);
                }
                
                stat = canRead(handle, &id, data, &dlc, &flags, &timestamp);
            }
            // stat is either canERR_NOMSG or any different error code
            if(stat != canOK && stat != canERR_NOMSG) {
                std::string errorMsg = "canNOTIFY_RX: " +  canDriver->CANError(stat);
                canDriver->onErrorCallback(&errorMsg);
            }
            break;
        }
        
        default:
            //TODO: MP since this thread is managed by the canlib, should we really throw exceptions?
            throw std::runtime_error("Callback got called with neither ERROR nor RX, gigantic UFF");
        break;
    }
}


std::string CANDriver::CANError(canStatus status) {
    char msg[64];
    canGetErrorText(status, msg, sizeof msg);
    return std::string(msg);
}

canStatus CANDriver::InitializeCANChannel(uint32_t canBusChannelID) {
    canStatus stat;
    canInitializeLibrary();

    // TODO: Might want to remove canOPEN_ACCEPT_VIRTUAL later (DB)
    canHandles[canBusChannelID] = canOpenChannel(canBusChannelID, canOPEN_CAN_FD | canOPEN_ACCEPT_LARGE_DLC | canOPEN_ACCEPT_VIRTUAL);
    if(canHandles[canBusChannelID] < 0){
        return (canStatus)canHandles[canBusChannelID];
    }

    int timeScale = 1; //1us precision
    stat = canIoCtl(canHandles[canBusChannelID], canIOCTL_SET_TIMER_SCALE, &timeScale, sizeof(timeScale));
    if (stat != canOK)
    {
        return stat;
    }

    stat = canSetBusParams(canHandles[canBusChannelID],
            arbitrationParams.bitrate,
            arbitrationParams.timeSegment1,
            arbitrationParams.timeSeqment2,
            arbitrationParams.syncJumpWidth,
            arbitrationParams.noSamplingPoints, 0); //sycmode Unsupported and ignored.
    if(stat < 0){
        return stat;

    }

    stat = canSetBusParamsFd(canHandles[canBusChannelID],
            dataParams.bitrate,
            dataParams.timeSegment1,
            dataParams.timeSeqment2,
            dataParams.syncJumpWidth);

    if(stat < 0){
        return stat;
    }

    /*stat = canSetBusParamsFdTq(canHandles[canBusChannelID], arbitrationParams, dataParams);
    if(stat < 0) {
        return stat;
    }*/

    stat = canSetBusOutputControl(canHandles[canBusChannelID], canDRIVER_NORMAL);
    if(stat < 0) {
        return stat;
    }

    stat = canBusOn(canHandles[canBusChannelID]);
    if(stat < 0) {
        return stat;
    }

    // Register callback for receiving a msg when the rcv buffer has been empty or when an error frame got received
    stat = kvSetNotifyCallback(canHandles[canBusChannelID], (kvCallback_t) &CANDriver::OnCANCallback, (void *) this, canNOTIFY_RX | canNOTIFY_ERROR | canNOTIFY_STATUS);
    if(stat < 0) {
        return stat;
    }

    // Filter messages with direction=0
    /* stat = canAccept(canHandles[canChannelID], 0x1, canFILTER_SET_MASK_EXT);
    if(stat < 0) {
        throw new std::runtime_error(CANError(stat));
    }

    stat = canAccept(canHandles[canChannelID], 0x1, canFILTER_SET_CODE_EXT);
    if(stat < 0) {
        throw new std::runtime_error(CANError(stat));
    } */
}
