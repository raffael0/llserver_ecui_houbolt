//
// Created by Markus on 2019-09-27.
//

#ifndef TXV_ECUI_LLSERVER_SEQUENCEMANAGER_H
#define TXV_ECUI_LLSERVER_SEQUENCEMANAGER_H

#include "common.h"

#include "utility/json.hpp"
#include "utility/Logging.h"
#include "utility/Timer.h"

#include "EventManager.h"

#include "StateController.h"

typedef struct point_s
{
    int64_t x;
    int64_t y;
} Point;

typedef enum class interpolation_e
{
    NONE,
    LINEAR
} Interpolation;

class SequenceManager : public Singleton<LLInterface>
{
    friend class Singleton;

private:
    bool isRunning = false;
    bool isAutoAbort = true;
    bool isAbort = false;
    bool isAbortRunning = false;

    std::mutex syncMtx;
    Timer* timer;

    nlohmann::json jsonSequence = nlohmann::json::object();
    nlohmann::json jsonAbortSequence = nlohmann::json::object();
    string comments = "";
    string currentDirPath = "";
    string logFileName = "";
    string lastDir = "";

    std::atomic_bool initialized = false;

    //config variables
    int32_t timerSyncInterval = 0;
    //----

    std::map<std::string, Interpolation> interpolationMap;
    std::map<int64_t, std::map<std::string, double[2]>> sensorsNominalRangeTimeMap;
    std::map<std::string, std::map<int64_t, double[2]>> sensorsNominalRangeMap;
    std::map<std::string, std::map<int64_t, std::vector<double>>> deviceMap;

    LLInterface *llInterface = LLInterface::Instance();
    EventManager *eventManager = EventManager::Instance();

    void SetupLogging();

    void LoadInterpolationMap();
    bool LoadSequence(nlohmann::json jsonSeq);

    // void LogSensors(int64_t microTime, std::vector<double > sensors);
    // void StopGetSensors();
    void CheckSensors(int64_t microTime);

    double GetTimestamp(nlohmann::json obj);
    void Tick(int64_t microTime);

    void StopAbortSequence();
    void StartAbortSequence();


    void plotMaps(uint8_t option);

    ~SequenceManager();

public:

    void Init();

    void AbortSequence(std::string abortMsg="abort");
    void StopSequence();
    void StartSequence(nlohmann::json jsonSeq, nlohmann::json jsonAbortSeq, std::string comments);
    void WritePostSeqComment(std::string msg);

    bool IsSequenceRunning();

};


#endif //TXV_ECUI_LLSERVER_SEQUENCEMANAGER_H
