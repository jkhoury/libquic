// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_TOOLS_QUIC_QUIC_SPDY_SERVER_STREAM_H_
#define NET_TOOLS_QUIC_QUIC_SPDY_SERVER_STREAM_H_

#include <string>

#include "base/basictypes.h"
#include "net/quic/reliable_quic_stream.h"
#include "net/quic/quic_protocol.h"

namespace net {

namespace tools {

class QuicServerSession;
static const QuicPriority kDefaultPriority = 3;

class FileDownloaderServerStream : public ReliableQuicStream {
 public:
  FileDownloaderServerStream(QuicStreamId id, QuicServerSession* session);
  ~FileDownloaderServerStream() override;

  // ReliableQuicStream implementation called by the session when there's
  // data for us.
  uint32 ProcessRawData(const char* data, uint32 data_len) override;
  void OnCanWrite() override;
  void OnFinRead() override;
  QuicPriority EffectivePriority() const override { return kDefaultPriority; }

  static void SetHomeDir(const std::string& home_dir) {
    HomeDir = home_dir;
  }

 protected:
  bool MapFileIntoMemory();
  void StartFileDownload();
  void SendNextFileBlock();

 private:
  static std::string HomeDir;

  std::string request_;

  // TODO(dimm): Consider using Chromium's base/files/memory_mapped_file.h.
  int fd_;
  void* file_mmap_address_;
  size_t file_size_;
  size_t sent_bytes_;

  DISALLOW_COPY_AND_ASSIGN(FileDownloaderServerStream);
};

}  // namespace tools
}  // namespace net

#endif  // NET_TOOLS_QUIC_QUIC_SPDY_SERVER_STREAM_H_
