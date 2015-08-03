// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/tools/quic/quic_client_session.h"
#include "net/quic/quic_session.h"

#include "base/logging.h"
#include "net/quic/crypto/crypto_protocol.h"
//#include "net/quic/crypto/proof_verifier_chromium.h"
#include "net/quic/crypto/proof_verifier.h"
#include "net/quic/quic_server_id.h"
#include "net/tools/quic/quic_simple_client_stream.h"

using std::string;

namespace net {

// ProofVerifyContextChromium is the implementation-specific information that a
// ProofVerifierChromium needs in order to log correctly.
struct ProofVerifyContextFake : public ProofVerifyContext {
   public:
     ProofVerifyContextFake() {}
};

namespace tools {

#if 0
class ProofFakeVerifier : public ProofVerifier {
  public:
    ProofFakeVerifier() {}
            
    virtual QuicAsyncStatus VerifyProof(const std::string& hostname,
                                      const std::string& server_config,
                                      const std::vector<std::string>& certs,
                                      const std::string& signature,
                                      const ProofVerifyContext* context,
                                      std::string* error_details,
                                      scoped_ptr<ProofVerifyDetails>* details,
                                      ProofVerifierCallback* callback) {
      DVLOG(1) << "!!!!!!!!!!!! ProofFakeVerifier::VerifyProof() => QUIC_SUCCESS";
      return QUIC_SUCCESS;
    }
};
#endif

QuicClientSession::QuicClientSession(const QuicConfig& config,
                                     QuicConnection* connection,
                                     const QuicServerId& server_id,
                                     QuicCryptoClientConfig* crypto_config)
    : QuicClientSessionBase(connection, config),
      crypto_stream_(new QuicCryptoClientStream(
          server_id,
          this,
          // TODO(dimm): original quic_client uses it only with HTTPS. Do we need it? ...
          new ProofVerifyContextFake(),
          crypto_config)),
      respect_goaway_(true) {
}

QuicClientSession::~QuicClientSession() {
}

void QuicClientSession::OnProofValid(
    const QuicCryptoClientConfig::CachedState& /*cached*/) {}

void QuicClientSession::OnProofVerifyDetailsAvailable(
    const ProofVerifyDetails& /*verify_details*/) {}

QuicSimpleClientStream* QuicClientSession::CreateOutgoingDynamicStream() {
  if (!crypto_stream_->encryption_established()) {
    DVLOG(1) << "Encryption not active so no outgoing stream created.";
    return nullptr;
  }
  if (GetNumOpenStreams() >= get_max_open_streams()) {
    DVLOG(1) << "Failed to create a new outgoing stream. "
             << "Already " << GetNumOpenStreams() << " open.";
    return nullptr;
  }
  if (goaway_received() && respect_goaway_) {
    DVLOG(1) << "Failed to create a new outgoing stream. "
             << "Already received goaway.";
    return nullptr;
  }
  QuicSimpleClientStream* stream = CreateClientStream();
  ActivateStream(stream);
  return stream;
}

QuicSimpleClientStream* QuicClientSession::CreateClientStream() {
  return new QuicSimpleClientStream(GetNextStreamId(), this);
}

QuicCryptoClientStream* QuicClientSession::GetCryptoStream() {
  return crypto_stream_.get();
}

void QuicClientSession::CryptoConnect() {
  DCHECK(flow_controller());
  crypto_stream_->CryptoConnect();
}

int QuicClientSession::GetNumSentClientHellos() const {
  return crypto_stream_->num_sent_client_hellos();
}

QuicSimpleClientStream* QuicClientSession::CreateIncomingDynamicStream(
    QuicStreamId id) {
  DLOG(ERROR) << "Server push not supported";
  return nullptr;
}


}  // namespace tools
}  // namespace net
