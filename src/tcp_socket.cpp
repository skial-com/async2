#include "tcp_socket.h"

TcpSocket::TcpSocket(IPluginContext* ctx, int ud)
    : handle_id(0), plugin_context(ctx),
      on_connect(0), on_data(0), on_error(0), on_close(0), on_accept(0),
      userdata(ud), max_chunk_size(4096),
      uv_handle(nullptr), state(TcpState::CREATED) {}

TcpSocket::~TcpSocket() {}
