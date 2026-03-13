#include "handle_manager.h"
#include "http_request.h"
#include "data_handle.h"
#include "tcp_socket.h"
#include "udp_socket.h"
#include "ws_connection.h"
#include "linked_list.h"

HandleManager::HandleManager() {
    next_handle_ = 1;
}

void HandleManager::CleanHandles() {
    for (auto it = used_handles_.begin(); it != used_handles_.end(); it++) {
        DeleteHandlePointer(it->second);
    }
    used_handles_.clear();
}

HttpRequest* HandleManager::GetHttpRequest(int handle) {
    auto it = used_handles_.find(handle);
    if (it == used_handles_.end() || it->second.type != HANDLE_HTTP_REQUEST || it->second.closed)
        return nullptr;
    return static_cast<HttpRequest*>(it->second.pointer);
}

DataHandle* HandleManager::GetDataHandle(int handle) {
    auto it = used_handles_.find(handle);
    if (it == used_handles_.end() || it->second.type != HANDLE_JSON_VALUE || it->second.closed)
        return nullptr;
    return static_cast<DataHandle*>(it->second.pointer);
}

TcpSocket* HandleManager::GetTcpSocket(int handle) {
    auto it = used_handles_.find(handle);
    if (it == used_handles_.end() || it->second.type != HANDLE_TCP_SOCKET || it->second.closed)
        return nullptr;
    return static_cast<TcpSocket*>(it->second.pointer);
}

UdpSocket* HandleManager::GetUdpSocket(int handle) {
    auto it = used_handles_.find(handle);
    if (it == used_handles_.end() || it->second.type != HANDLE_UDP_SOCKET || it->second.closed)
        return nullptr;
    return static_cast<UdpSocket*>(it->second.pointer);
}

WsConnection* HandleManager::GetWsSocket(int handle) {
    auto it = used_handles_.find(handle);
    if (it == used_handles_.end() || it->second.type != HANDLE_WS_SOCKET || it->second.closed)
        return nullptr;
    return static_cast<WsConnection*>(it->second.pointer);
}

LinkedList* HandleManager::GetLinkedList(int handle) {
    auto it = used_handles_.find(handle);
    if (it == used_handles_.end() || it->second.type != HANDLE_LINKED_LIST || it->second.closed)
        return nullptr;
    return static_cast<LinkedList*>(it->second.pointer);
}

int HandleManager::CreateHandle(void* pointer, HandleType type) {
    int handle_number;
    Handle h;
    h.type = type;
    h.pointer = pointer;

    if (!freed_handles_.empty()) {
        handle_number = freed_handles_.front();
        freed_handles_.pop();
    } else if (next_handle_ == std::numeric_limits<int>::max()) {
        return 0;
    } else {
        handle_number = next_handle_++;
    }

    used_handles_[handle_number] = h;
    return handle_number;
}

void HandleManager::FreeHandle(int handle) {
    auto it = used_handles_.find(handle);
    if (it != used_handles_.end()) {
        freed_handles_.push(it->first);
        DeleteHandlePointer(it->second);
        used_handles_.erase(it);
    }
}

void HandleManager::MarkHandleClosed(int handle) {
    auto it = used_handles_.find(handle);
    if (it != used_handles_.end()) {
        it->second.closed = true;
    }
}

void HandleManager::DeleteHandlePointer(Handle h) {
    switch (h.type) {
        case HANDLE_HTTP_REQUEST:
            delete static_cast<HttpRequest*>(h.pointer);
            break;
        case HANDLE_JSON_VALUE:
            delete static_cast<DataHandle*>(h.pointer);
            break;
        case HANDLE_TCP_SOCKET:
            delete static_cast<TcpSocket*>(h.pointer);
            break;
        case HANDLE_UDP_SOCKET:
            delete static_cast<UdpSocket*>(h.pointer);
            break;
        case HANDLE_WS_SOCKET:
            delete static_cast<WsConnection*>(h.pointer);
            break;
        case HANDLE_LINKED_LIST:
            delete static_cast<LinkedList*>(h.pointer);
            break;
        default:
            break;
    }
}
