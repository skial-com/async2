#ifndef ASYNC2_HANDLE_MANAGER_H
#define ASYNC2_HANDLE_MANAGER_H

#include <unordered_map>
#include <queue>
#include <limits>

class HttpRequest;
class DataHandle;
class TcpSocket;
class UdpSocket;
class WsConnection;
class LinkedList;

enum HandleType {
    HANDLE_HTTP_REQUEST,
    HANDLE_JSON_VALUE,
    HANDLE_TCP_SOCKET,
    HANDLE_UDP_SOCKET,
    HANDLE_WS_SOCKET,
    HANDLE_LINKED_LIST,
    HANDLE_NONE,
};

struct Handle {
    HandleType type;
    void* pointer;
    bool closed = false;
};

typedef std::unordered_map<int, Handle> HandleMapType;

class HandleManager {
    int next_handle_;
    std::queue<int> freed_handles_;
    HandleMapType used_handles_;

public:
    HandleManager();
    void CleanHandles();
    HttpRequest* GetHttpRequest(int handle);
    DataHandle* GetDataHandle(int handle);
    TcpSocket* GetTcpSocket(int handle);
    UdpSocket* GetUdpSocket(int handle);
    WsConnection* GetWsSocket(int handle);
    LinkedList* GetLinkedList(int handle);
    int CreateHandle(void* pointer, HandleType type);
    void FreeHandle(int handle);
    void MarkHandleClosed(int handle);

    const HandleMapType& GetHandles() const { return used_handles_; }

private:
    void DeleteHandlePointer(Handle h);
};

#endif
