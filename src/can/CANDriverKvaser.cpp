#ifndef NO_CANLIB

#include "can/CANDriverKvaser.h"
#include <utility>
#include <string>
#include "can_houbolt/can_cmds.h"
#include "utility/utils.h"
#include "utility/Config.h"

CANDriverKvaser::CANDriverKvaser(std::function<void(uint8_t &, uint32_t &, uint8_t *, uint32_t &, uint64_t &, CANDriver *driver)> onRecvCallback,
								 std::function<void(std::string *)> onErrorCallback, std::vector<uint32_t> &canBusChannelIDs) :
	CANDriver(onRecvCallback, onErrorCallback)
{
    //arbitration bus parameters
    int32_t bitrate = std::get<int>(Config::getData("CAN/BUS/ARBITRATION/bitrate"));
    int32_t tseg1 = std::get<int>(Config::getData("CAN/BUS/ARBITRATION/time_segment_1"));
    int32_t tseg2 = std::get<int>(Config::getData("CAN/BUS/ARBITRATION/time_segment_2"));
    int32_t sjw = std::get<int>(Config::getData("CAN/BUS/ARBITRATION/sync_jump_width"));
    int32_t noSamp = std::get<int>(Config::getData("CAN/BUS/ARBITRATION/no_sampling_points"));
    arbitrationParams = {bitrate, tseg1, tseg2, sjw, noSamp};

    //data bus parameters
    int64_t bitrateData = std::get<int>(Config::getData("CAN/BUS/DATA/bitrate"));
    int32_t tseg1Data = std::get<int>(Config::getData("CAN/BUS/DATA/time_segment_1"));
    int32_t tseg2Data = std::get<int>(Config::getData("CAN/BUS/DATA/time_segment_2"));
    int32_t sjwData = std::get<int>(Config::getData("CAN/BUS/DATA/sync_jump_width"));
    dataParams = {bitrateData, tseg1Data, tseg2Data, sjwData};

    blockingTimeout = std::get<int>(Config::getData("CAN/blocking_timeout"));

    canStatus stat;
    for (auto &channelID : canBusChannelIDs)
    {
        stat = InitializeCANChannel(channelID);
        if (stat < 0) {
            std::ostringstream stringStream;
            stringStream << "CANDriver - Constructor: CAN-Channel " << channelID << ": " << CANError(stat);
            throw std::runtime_error(stringStream.str());
        }
    }
}

CANDriverKvaser::~CANDriverKvaser()
{
    // Empty transfer queues (not strictly necessary but recommended by Kvaser)
    for (auto &handle : canHandlesMap)
    {
        (void) canWriteSync(handle.second, 5);
        (void) canBusOff(handle.second);
        (void) canClose(handle.second);
    }
    canUnloadLibrary();
}


void CANDriverKvaser::SendCANMessage(uint32_t canChannelID, uint32_t canID, uint8_t *payload, uint32_t payloadLength, bool blocking)
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
    if (dlc == (uint32_t)(-1))
    {
        throw std::runtime_error("CANDriver - SendCANMessage: correct dlc couldn't be found");
    }
    // Flags mean that the message is a FD message with bit rate switching (FDF, BRS)
    uint8_t msg[64] = {0};
    std::copy_n(payload, payloadLength, msg);
    
    canStatus stat;
    if (blocking)
    {
        stat = canWriteWait(canHandlesMap[canChannelID], canID, (void *) msg, dlcBytes, canFDMSG_FDF | canFDMSG_BRS, blockingTimeout);
    }
    else
    {
        stat = canWrite(canHandlesMap[canChannelID], canID, (void *) msg, dlcBytes, canFDMSG_FDF | canFDMSG_BRS);
    }
    
    if(stat < 0) {
        throw std::runtime_error("CANDriver - SendCANMessage: " + CANError(stat));
    }
}

std::map<std::string, bool> CANDriverKvaser::GetCANStatusReadable(uint32_t canBusChannelID)
{
    std::map<std::string, bool> canState;
    uint64_t flags;
    canStatus stat = canReadStatus(canHandlesMap[canBusChannelID], &flags);

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
void CANDriverKvaser::OnCANCallback(int handle, void *driver, unsigned int event)
{
    CANDriverKvaser *canDriver = (CANDriverKvaser *) driver;

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
                const auto &handleFindIt = canDriver->canHandlesMap.find(handle);
                if (handleFindIt != canDriver->canHandlesMap.end())
                {
                    canBusChannelID = handleFindIt->first;
                }
                else
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
                    try
                    {
                        canDriver->onRecvCallback(canBusChannelID, (uint32_t &) id, data, dlc, softwareTime, canDriver);
                    }
                    catch(const std::exception& e)
                    {
                        Debug::error("%s", e.what());
                    }
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


std::string CANDriverKvaser::CANError(canStatus status) {
    char msg[64];
    canGetErrorText(status, msg, sizeof msg);
    return std::string(msg);
}


canStatus CANDriverKvaser::InitializeCANChannel(uint32_t canBusChannelID) {
    canStatus stat;
    canInitializeLibrary();

    // TODO: Might want to remove canOPEN_ACCEPT_VIRTUAL later (DB)
    canHandlesMap[canBusChannelID] = canOpenChannel(canBusChannelID, canOPEN_CAN_FD | canOPEN_ACCEPT_LARGE_DLC | canOPEN_ACCEPT_VIRTUAL);
    if(canHandlesMap[canBusChannelID] < 0){
        return (canStatus)canHandlesMap[canBusChannelID];
    }

    int timeScale = 1; //1us precision
    stat = canIoCtl(canHandlesMap[canBusChannelID], canIOCTL_SET_TIMER_SCALE, &timeScale, sizeof(timeScale));
    if (stat != canOK)
    {
        return stat;
    }

    stat = canSetBusParams(canHandlesMap[canBusChannelID],
            arbitrationParams.bitrate,
            arbitrationParams.timeSegment1,
            arbitrationParams.timeSeqment2,
            arbitrationParams.syncJumpWidth,
            arbitrationParams.noSamplingPoints, 0); //sycmode Unsupported and ignored.
    if(stat < 0){
        return stat;

    }

    stat = canSetBusParamsFd(canHandlesMap[canBusChannelID],
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

    stat = canSetBusOutputControl(canHandlesMap[canBusChannelID], canDRIVER_NORMAL);
    if(stat < 0) {
        return stat;
    }

    stat = canBusOn(canHandlesMap[canBusChannelID]);
    if(stat < 0) {
        return stat;
    }

    // Register callback for receiving a msg when the rcv buffer has been empty or when an error frame got received
    stat = kvSetNotifyCallback(canHandlesMap[canBusChannelID], (kvCallback_t) &CANDriverKvaser::OnCANCallback, (void *) this, canNOTIFY_RX | canNOTIFY_ERROR | canNOTIFY_STATUS);
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
    return canOK;
}


#endif //NO_CANLIB
