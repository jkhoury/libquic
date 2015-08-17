// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include "net/tools/quic/file_downloader_client_stream.h"

#include "base/logging.h"
#include "base/stl_util.h"
#include "base/strings/string_number_conversions.h"
#include "net/tools/quic/quic_client_session.h"

using base::StringPiece;
using std::string;
using base::StringToInt;

namespace net {
namespace tools {

FileDownloaderClientStream::FileDownloaderClientStream(QuicStreamId id,
                                           QuicClientSession* session)
    : ReliableQuicStream(id, session),
      visitor_(nullptr),
      fd_(-1) {
}

FileDownloaderClientStream::~FileDownloaderClientStream() {
}

void FileDownloaderClientStream::OnStreamFrame(const QuicStreamFrame& frame) {
  if (!write_side_closed()) {
    DVLOG(1) << "Got a response before the request was complete.  "
             << "Aborting request.";
    CloseWriteSide();
  }
  ReliableQuicStream::OnStreamFrame(frame);
}

void FileDownloaderClientStream::OnDataAvailable() {
  DCHECK(fd_ != -1);

  while (sequencer()->HasBytesToRead()) {
    struct iovec iov;
    if (sequencer()->GetReadableRegions(&iov, 1) == 0) {
      // No more data to read.
      break;
    }

    ssize_t saved_bytes = write(fd_, static_cast<char*>(iov.iov_base),
                                iov.iov_len);
    if (saved_bytes != static_cast<ssize_t>(iov.iov_len)) {
      LOG(ERROR) << "*** Client processed " << saved_bytes << " bytes "
                    "out of expected " << iov.iov_len << " bytes";
    } else {
      DVLOG(1) << "*** Client processed " << iov.iov_len << " bytes for stream " << id();
    }

    sequencer()->MarkConsumed(iov.iov_len);
  }

  if (sequencer()->IsClosed()) {
    OnFinRead();
  } else {
    sequencer()->SetUnblocked();
  }
}

bool FileDownloaderClientStream::SendRequest(const std::string& request, bool fin) {
  if (!OpenFile(request)) {
    return false;
  }
  WriteOrBufferData(request, fin, nullptr);
  return true;
}

bool FileDownloaderClientStream::OpenFile(const std::string& filename) {
  std::string path = "_" + filename;
  fd_ = open(path.c_str(), O_WRONLY|O_CREAT|O_TRUNC|O_APPEND, 0644);
  if (fd_ == -1) {
    LOG(ERROR) << "Failed to create " << path;
    return false;
  }
  return true;
}

void FileDownloaderClientStream::OnClose() {
  ReliableQuicStream::OnClose();
  if (fd_ != -1) {
    close(fd_);
  }

  if (visitor_) {
    Visitor* visitor = visitor_;
    // Calling Visitor::OnClose() may result the destruction of the visitor,
    // so we need to ensure we don't call it again.
    visitor_ = nullptr;
    visitor->OnClose(this);
  }
}

}  // namespace tools
}  // namespace net
