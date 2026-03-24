#ifndef ASYNC2_HANDLE_MANAGER_H
#define ASYNC2_HANDLE_MANAGER_H

#include <unordered_map>
#include <unordered_set>
#include <queue>
#include <vector>
#include <limits>

namespace SourcePawn { class IPluginContext; }
using SourcePawn::IPluginContext;

class HttpRequest;
class DataHandle;
class TcpSocket;
class UdpSocket;
class WsConnection;
class LinkedList;
class DataIterator;

enum HandleType {
    HANDLE_HTTP_REQUEST,
    HANDLE_JSON_VALUE,
    HANDLE_TCP_SOCKET,
    HANDLE_UDP_SOCKET,
    HANDLE_WS_SOCKET,
    HANDLE_LINKED_LIST,
    HANDLE_ITERATOR,
    HANDLE_NONE,
};

struct Handle {
    HandleType type;
    void* pointer;
    IPluginContext* owner = nullptr;  // cleanup ownership only; callbacks use the object's plugin_context
    bool closed = false;
};

typedef std::unordered_map<int, Handle> HandleMapType;

class HandleManager {
    int next_handle_;
    std::queue<int> freed_handles_;
    HandleMapType used_handles_;
    std::unordered_map<IPluginContext*, std::unordered_set<int>> plugin_handles_;

public:
    HandleManager();
    void CleanHandles();
    HttpRequest* GetHttpRequest(int handle);
    DataHandle* GetDataHandle(int handle);
    TcpSocket* GetTcpSocket(int handle);
    UdpSocket* GetUdpSocket(int handle);
    WsConnection* GetWsSocket(int handle);
    LinkedList* GetLinkedList(int handle);
    DataIterator* GetDataIterator(int handle);
    int CreateHandle(void* pointer, HandleType type, IPluginContext* owner = nullptr);
    void FreeHandle(int handle);
    void MarkHandleClosed(int handle);
    bool TransferHandle(int handle, IPluginContext* new_owner);
    std::vector<std::pair<int, Handle>> TakePluginHandles(IPluginContext* ctx);

    const HandleMapType& GetHandles() const { return used_handles_; }

private:
    void RemoveFromPluginSet(IPluginContext* owner, int handle);
    void DeleteHandlePointer(Handle h);
};

#endif
