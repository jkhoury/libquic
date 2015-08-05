// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_TOOLS_QUIC_QUIC_SPDY_CLIENT_STREAM_H_
#define NET_TOOLS_QUIC_QUIC_SPDY_CLIENT_STREAM_H_

#include <sys/types.h>
#include <string>

#include "base/basictypes.h"
#include "base/strings/string_piece.h"
#include "net/quic/reliable_quic_stream.h"
#include "net/quic/quic_protocol.h"

namespace net {
namespace tools {

static const QuicPriority kDefaultPriority = 3;
class QuicClientSession;

class FileDownloaderClientStream : public ReliableQuicStream /*ReliableQuicStream*/ {
 public:
  // Visitor receives callbacks from the stream.
  class NET_EXPORT_PRIVATE Visitor {
   public:
    Visitor() {}

    // Called when the stream is closed.
    virtual void OnClose(FileDownloaderClientStream* stream) = 0;

   protected:
    virtual ~Visitor() {}

   private:
    DISALLOW_COPY_AND_ASSIGN(Visitor);
  };

  FileDownloaderClientStream(QuicStreamId id, QuicClientSession* session);
  ~FileDownloaderClientStream() override;

  bool SendRequest(const std::string& request, bool fin);

  // Override the base class to close the write side as soon as we get a
  // response.
  void OnStreamFrame(const QuicStreamFrame& frame) override;

  // ReliableQuicStream implementation called by the session when there's
  // data for us.
  // ReliableQuicStream implementation
  void OnClose() override;
  uint32 ProcessRawData(const char* data, uint32 data_len) override;

  QuicPriority EffectivePriority() const override { return kDefaultPriority; }

  void set_visitor(Visitor* visitor) { visitor_ = visitor; }

 private:
  bool OpenFile(const std::string& filename);

  Visitor* visitor_;
  int fd_;

  DISALLOW_COPY_AND_ASSIGN(FileDownloaderClientStream);
};

}  // namespace tools
}  // namespace net

#endif  // NET_TOOLS_QUIC_QUIC_SPDY_CLIENT_STREAM_H_
