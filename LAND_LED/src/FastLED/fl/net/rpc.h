#pragma once

/// @file fl/net/rpc.h
/// @brief fl::net::rpc — RPC transport type aliases
///
/// Provides RPC-related transport type aliases under fl::net::rpc.
/// The Remote RPC server itself lives in fl/remote/remote.h and
/// depends on RPC — not the other way around.

#include "http/stream_client.h"
#include "http/stream_server.h"

namespace fl {
namespace net {
namespace rpc {

using HttpStreamClient = ::fl::net::http::HttpStreamClient;
using HttpStreamServer = ::fl::net::http::HttpStreamServer;

} // namespace rpc
} // namespace net
} // namespace fl
