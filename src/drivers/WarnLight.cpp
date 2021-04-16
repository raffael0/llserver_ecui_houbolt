//
// Created by Markus on 2019-10-16.
//

#include "driver/WarnLight.h"
#include "utility/Config.h"

using json = nlohmann::json;

WarnLight::WarnLight(uint16_t id)
{
    this->id = id;
    std::string ip = std::get<std::string>(Config::getData("WARNLIGHT/ip"));
    int32_t port = std::get<int>(Config::getData("WARNLIGHT/port"));
    socket = new SocketOld(OnClose, ip, port, 1);
    Reset();
}

WarnLight::~WarnLight()
{
    delete socket;
}

void WarnLight::SendJson(nlohmann::json message)
{
    socket->Send(message.dump() + "\n");
}

void WarnLight::Reset()
{
    nlohmann::json message;
    message["type"] = "reset";
    SendJson(message);
}

void WarnLight::SetColor(uint8_t red, uint8_t green, uint8_t blue)
{
    nlohmann::json message = {
        {"type", "set-color"},
        {"content", {
            {"red", red},
            {"green", green},
            {"blue", blue},
        }}
    };
    SendJson(message);
}

void WarnLight::SetMode(std::string mode) {
    nlohmann::json message = {
        {"type", "set-mode"},
        {"content", {
            {"mode", mode}
        }}
    };
    SendJson(message);
}

void WarnLight::StopBuzzer()
{
    nlohmann::json message;
    message["type"] = "stop-buzzer";
    SendJson(message);
}

void WarnLight::StartBuzzerBeep(uint16_t time)
{
    nlohmann::json message = {
        {"type", "start-buzzer-beep"},
        {"content", {
            {"period", time}
        }}
    };
    SendJson(message);
}

void WarnLight::StartBuzzerContinuous()
{
    nlohmann::json message;
    message["type"] = "start-buzzer-continuous";
    SendJson(message);
}

void WarnLight::OnClose()
{

}
