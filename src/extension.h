#ifndef _INCLUDE_ASYNC2_EXTENSION_H_
#define _INCLUDE_ASYNC2_EXTENSION_H_

#include <atomic>
#include "smsdk_ext.h"

// Debug logging — level: 0=off, 1=error, 2=info, 3=debug
extern std::atomic<int> g_log_level;

// Client → HTTP handle tracking for auto-cancel on disconnect
void TrackClientHandle(int client, int handle_id);
void UntrackClientHandle(int client, int handle_id);

class Async2Extension : public SDKExtension, public IRootConsoleCommand, public IClientListener
{
public:
    virtual bool SDK_OnLoad(char *error, size_t maxlength, bool late);
    virtual void SDK_OnUnload();
    virtual void OnRootConsoleCommand(const char *cmdname, const ICommandArgs *args);
    virtual void OnClientDisconnecting(int client) override;
};

#endif
