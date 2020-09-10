//
// Created by Markus on 2019-10-15.
//

#include <thread>

#include "Socket.h"

#include "EcuiSocket.h"

#include "Config.h"

using namespace std;

Socket* EcuiSocket::socket;

bool EcuiSocket::connectionActive = false;
bool EcuiSocket::shallClose = false;

std::thread* EcuiSocket::asyncListenThread;
std::function<void()> EcuiSocket::onCloseCallback;

void EcuiSocket::Init(std::function<void(json)> onMsgCallback, std::function<void()> onCloseCallback)
{
    EcuiSocket::onCloseCallback = onCloseCallback;

    string ip = std::get<std::string>(Config::getData("WEBSERVER/ip"));
    int32 port = std::get<int>(Config::getData("WEBSERVER/port"));

    socket = new Socket(Close, ip, port);
    connectionActive = true;
    asyncListenThread = new thread(AsyncListen, onMsgCallback);
    asyncListenThread->detach();

}

void EcuiSocket::AsyncListen(std::function<void(json)> onMsgCallback)
{
    string delimiter = "\n";
    string msg = "";
    while(!shallClose)
    {
        size_t pos = 0;
        msg.append(socket->Recv());
        while ((pos = msg.find(delimiter)) == std::string::npos)
        {
            msg.append(socket->Recv());
        }

        string token;
        while ((pos = msg.find(delimiter)) != std::string::npos)
        {
            token = msg.substr(0, pos);
            std::cout << "-------&&&-----\n" << token << "\n----------&&&------" <<  std::endl;
            json jsonMsg = json::parse(token);
            onMsgCallback(jsonMsg);
            msg.erase(0, pos + delimiter.length());
        }

        std::cout << "---------------\n" << token << "\n-------------------" <<  std::endl;
        json jsonMsg = json::parse(token);
        onMsgCallback(jsonMsg);
        msg.erase(0, pos + delimiter.length());

        this_thread::yield();
    }
}

void EcuiSocket::SendAsync(std::string msg)
{
    socket->Send(msg);
}

void EcuiSocket::SendJson(std::string type)
{
    SendJson(type, nullptr);
}

void EcuiSocket::SendJson(std::string type, json content)
{

    if (connectionActive)
    {

        json jsonMsg = json::object();
        jsonMsg["type"] = type;

        if (content != nullptr)
        {
            jsonMsg["content"] = content;
        }
        else
        {
            jsonMsg["content"] = json::object();
        }
        //    cout << "Content: "<< content.dump() << endl;
        //    cout << "Msg: "<< jsonMsg.dump() << endl;
        string msg = jsonMsg.dump() + "\n";

//        std::thread sendThread(SendAsync, msg);
//
//        sendThread.detach();
        socket->Send(msg);
    }
    else
    {
        Debug::error("no connection active");
    }

}

void EcuiSocket::SendJson(std::string type, float content)
{
    if (connectionActive)
    {

        json jsonMsg = json::object();
        jsonMsg["type"] = type;

        jsonMsg["content"] = content;

        //	cout << "Content: "<< content.dump() << endl;
        //	cout << "Msg: "<< jsonMsg.dump() << endl;
        string msg = jsonMsg.dump() + "\n";


//        std::thread sendThread(SendAsync, msg);
//        sendThread.detach();
        socket->Send(msg);
    }
    else
    {
        Debug::error("no connection active");
    }
}

void EcuiSocket::Close()
{
    Destroy();
    onCloseCallback();
}

void EcuiSocket::Destroy()
{
    if (connectionActive)
    {
        shallClose = true;
        delete asyncListenThread;
        delete socket;
        connectionActive = false;
    }

}
